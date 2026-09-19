//! Safe Rust access to shared-memory payloads allocated by `shared_buffer`.
//!
//! A payload is deliberately uninitialized when allocated. The public Rust
//! types make that state explicit:
//!
//! `UninitializedBuffer -> WriteAccess -> InitializedWriteAccess -> Buffer -> ReadAccess`
//!
//! The native write lease is one-shot. Dropping a write access finalizes the
//! allocation; there is no second write access for the resulting `Buffer`.

use std::fmt;
use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::{Deref, DerefMut};
use std::os::raw::c_int;
use std::ptr::NonNull;
use std::slice;

mod ffi {
    use super::c_int;
    use std::os::raw::c_uchar;

    #[repr(C)]
    pub struct Buffer {
        _private: [u8; 0],
    }

    #[repr(C)]
    pub struct ReadAccess {
        _private: [u8; 0],
    }

    #[repr(C)]
    pub struct WriteAccess {
        _private: [u8; 0],
    }

    pub const OK: c_int = 0;

    unsafe extern "C" {
        pub fn shared_buffer_c_buffer_new(byte_count: usize, out: *mut *mut Buffer) -> c_int;
        pub fn shared_buffer_c_buffer_destroy(buffer: *mut Buffer);
        pub fn shared_buffer_c_buffer_size(buffer: *const Buffer) -> usize;

        pub fn shared_buffer_c_read_access_new(
            buffer: *const Buffer,
            out: *mut *mut ReadAccess,
        ) -> c_int;
        pub fn shared_buffer_c_read_access_destroy(access: *mut ReadAccess);
        pub fn shared_buffer_c_read_access_data(access: *const ReadAccess) -> *const c_uchar;
        pub fn shared_buffer_c_read_access_size(access: *const ReadAccess) -> usize;

        pub fn shared_buffer_c_write_access_new(
            buffer: *mut Buffer,
            out: *mut *mut WriteAccess,
        ) -> c_int;
        pub fn shared_buffer_c_write_access_destroy(access: *mut WriteAccess);
        pub fn shared_buffer_c_write_access_data(access: *mut WriteAccess) -> *mut c_uchar;
        pub fn shared_buffer_c_write_access_size(access: *const WriteAccess) -> usize;
    }
}

/// Errors returned by shared-buffer allocation and access acquisition.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    /// A zero-sized allocation or invalid native argument was requested.
    InvalidArgument,
    /// The native allocator could not allocate the requested block.
    AllocationFailed,
    /// The requested read/write lease could not be acquired.
    AccessFailed,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidArgument => f.write_str("invalid shared-buffer argument"),
            Self::AllocationFailed => f.write_str("shared-buffer allocation failed"),
            Self::AccessFailed => f.write_str("shared-buffer access could not be acquired"),
        }
    }
}

impl std::error::Error for Error {}

fn map_status(status: c_int) -> Result<(), Error> {
    match status {
        ffi::OK => Ok(()),
        1 => Err(Error::InvalidArgument),
        2 => Err(Error::AllocationFailed),
        _ => Err(Error::AccessFailed),
    }
}

/// Common owner for the native allocation.
struct OwnedBuffer {
    raw: NonNull<ffi::Buffer>,
    // The C++ Buffer and its handle state are not declared thread-safe.
    _not_thread_safe: PhantomData<std::rc::Rc<()>>,
}

impl Drop for OwnedBuffer {
    fn drop(&mut self) {
        // SAFETY: raw is the allocation returned by the matching C ABI.
        unsafe { ffi::shared_buffer_c_buffer_destroy(self.raw.as_ptr()) }
    }
}

/// An owned allocation whose payload has not been initialized yet.
pub struct UninitializedBuffer {
    owner: OwnedBuffer,
}

impl UninitializedBuffer {
    /// Allocate `byte_count` uninitialized bytes from the shared-buffer pool.
    pub fn new(byte_count: usize) -> Result<Self, Error> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: raw points to valid output storage for the C ABI.
        unsafe { map_status(ffi::shared_buffer_c_buffer_new(byte_count, &mut raw))? };
        let raw = NonNull::new(raw).ok_or(Error::AllocationFailed)?;
        Ok(Self {
            owner: OwnedBuffer {
                raw,
                _not_thread_safe: PhantomData,
            },
        })
    }

    /// Return the payload size in bytes.
    pub fn len(&self) -> usize {
        // SAFETY: owner.raw is valid for the lifetime of self.
        unsafe { ffi::shared_buffer_c_buffer_size(self.owner.raw.as_ptr()) }
    }

    /// Return whether the payload has no bytes.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Consume the allocation and acquire its one-shot uninitialized write lease.
    pub fn write(self) -> Result<WriteAccess, Error> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: owner.raw is valid and raw is valid output storage.
        unsafe {
            map_status(ffi::shared_buffer_c_write_access_new(
                self.owner.raw.as_ptr(),
                &mut raw,
            ))?;
        }
        let raw = match NonNull::new(raw) {
            Some(raw) => raw,
            None => return Err(Error::AccessFailed),
        };
        let UninitializedBuffer { owner } = self;
        Ok(WriteAccess {
            access: Some(raw),
            owner: Some(owner),
        })
    }
}

/// A fully initialized shared-memory payload.
pub struct Buffer {
    owner: OwnedBuffer,
}

impl Buffer {
    /// Return the payload size in bytes.
    pub fn len(&self) -> usize {
        // SAFETY: owner.raw is valid for the lifetime of self.
        unsafe { ffi::shared_buffer_c_buffer_size(self.owner.raw.as_ptr()) }
    }

    /// Return whether the payload has no bytes.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Acquire read-only access to the initialized payload.
    pub fn read(&self) -> Result<ReadAccess<'_>, Error> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: owner.raw is valid and raw is valid output storage.
        unsafe {
            map_status(ffi::shared_buffer_c_read_access_new(
                self.owner.raw.as_ptr(),
                &mut raw,
            ))?;
        }
        let raw = NonNull::new(raw).ok_or(Error::AccessFailed)?;
        Ok(ReadAccess {
            raw,
            _buffer: PhantomData,
        })
    }
}

/// A scoped write lease over an uninitialized payload.
pub struct WriteAccess {
    access: Option<NonNull<ffi::WriteAccess>>,
    owner: Option<OwnedBuffer>,
}

impl WriteAccess {
    /// Borrow the uninitialized payload for initialization.
    pub fn as_mut_slice(&mut self) -> &mut [MaybeUninit<u8>] {
        // SAFETY: the native access owns a valid exclusive write lease and
        // reports its matching payload size until Drop releases the lease.
        unsafe {
            slice::from_raw_parts_mut(
                ffi::shared_buffer_c_write_access_data(self.access_ptr()) as *mut MaybeUninit<u8>,
                self.access_size(),
            )
        }
    }

    /// Fill every byte and transition to [`InitializedWriteAccess`].
    pub fn fill(mut self, value: u8) -> InitializedWriteAccess {
        for byte in self.as_mut_slice() {
            byte.write(value);
        }
        self.into_initialized()
    }

    /// Copy an exactly-sized initialized slice.
    ///
    /// If the source length is wrong, the returned error owns the write access
    /// so the caller can recover it and retry without losing the allocation.
    pub fn write_from_slice(
        mut self,
        source: &[u8],
    ) -> Result<InitializedWriteAccess, WriteFromSliceError> {
        let expected = self.access_size();
        if source.len() != expected {
            return Err(WriteFromSliceError {
                access: self,
                expected,
                actual: source.len(),
            });
        }
        for (destination, source) in self.as_mut_slice().iter_mut().zip(source) {
            destination.write(*source);
        }
        Ok(self.into_initialized())
    }

    /// Treat the payload as initialized and transition to
    /// [`InitializedWriteAccess`].
    ///
    /// # Safety
    ///
    /// Every byte in the payload must have been initialized through
    /// [`Self::as_mut_slice`] before calling this method.
    pub unsafe fn assume_init(self) -> InitializedWriteAccess {
        self.into_initialized()
    }

    fn into_initialized(mut self) -> InitializedWriteAccess {
        let access = self.access.take().expect("write access must be present");
        let owner = self.owner.take().expect("buffer owner must be present");
        std::mem::forget(self);
        InitializedWriteAccess {
            access: Some(access),
            owner: Some(owner),
        }
    }

    fn access_ptr(&self) -> *mut ffi::WriteAccess {
        self.access.expect("write access must be present").as_ptr()
    }

    fn access_size(&self) -> usize {
        // SAFETY: access_ptr is valid while self is alive.
        unsafe { ffi::shared_buffer_c_write_access_size(self.access_ptr()) }
    }
}

impl Deref for WriteAccess {
    type Target = [MaybeUninit<u8>];

    fn deref(&self) -> &Self::Target {
        // SAFETY: a shared view of the uninitialized storage is valid as
        // MaybeUninit, and the borrow is tied to the access lifetime.
        unsafe {
            slice::from_raw_parts(
                ffi::shared_buffer_c_write_access_data(self.access_ptr()) as *const MaybeUninit<u8>,
                self.access_size(),
            )
        }
    }
}

impl DerefMut for WriteAccess {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut_slice()
    }
}

impl Drop for WriteAccess {
    fn drop(&mut self) {
        if let Some(access) = self.access.take() {
            // SAFETY: access is owned by self and is released exactly once.
            unsafe { ffi::shared_buffer_c_write_access_destroy(access.as_ptr()) }
        }
        drop(self.owner.take());
    }
}

/// Error returned when a source slice cannot initialize the write access.
///
/// The original write access is retained so the caller can correct the input
/// and retry without reallocating.
pub struct WriteFromSliceError {
    access: WriteAccess,
    expected: usize,
    actual: usize,
}

impl fmt::Debug for WriteFromSliceError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("WriteFromSliceError")
            .field("expected", &self.expected)
            .field("actual", &self.actual)
            .finish_non_exhaustive()
    }
}

impl WriteFromSliceError {
    /// Return the required payload size.
    pub fn expected(&self) -> usize {
        self.expected
    }

    /// Return the rejected source size.
    pub fn actual(&self) -> usize {
        self.actual
    }

    /// Recover the write access for a corrected retry.
    pub fn into_write_access(self) -> WriteAccess {
        self.access
    }
}

impl fmt::Display for WriteFromSliceError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "shared-buffer length mismatch: expected {}, got {}",
            self.expected, self.actual
        )
    }
}

impl std::error::Error for WriteFromSliceError {}

/// A write lease whose entire payload is initialized.
pub struct InitializedWriteAccess {
    access: Option<NonNull<ffi::WriteAccess>>,
    owner: Option<OwnedBuffer>,
}

impl InitializedWriteAccess {
    /// Borrow the initialized payload mutably.
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        // SAFETY: the type invariant guarantees that every byte is initialized
        // and the native access remains an exclusive write lease.
        unsafe {
            slice::from_raw_parts_mut(
                ffi::shared_buffer_c_write_access_data(self.access_ptr()),
                self.access_size(),
            )
        }
    }

    /// Finalize the one-shot write and return the readable buffer.
    pub fn finish(mut self) -> Buffer {
        let access = self.access.take().expect("write access must be present");
        let owner = self.owner.take().expect("buffer owner must be present");
        // Destroying the native write access finalizes the one-shot write.
        unsafe { ffi::shared_buffer_c_write_access_destroy(access.as_ptr()) };
        std::mem::forget(self);
        Buffer { owner }
    }

    fn access_ptr(&self) -> *mut ffi::WriteAccess {
        self.access.expect("write access must be present").as_ptr()
    }

    fn access_size(&self) -> usize {
        // SAFETY: access_ptr is valid while self is alive.
        unsafe { ffi::shared_buffer_c_write_access_size(self.access_ptr()) }
    }
}

impl Deref for InitializedWriteAccess {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        // SAFETY: the type invariant guarantees initialized bytes.
        unsafe {
            slice::from_raw_parts(
                ffi::shared_buffer_c_write_access_data(self.access_ptr()),
                self.access_size(),
            )
        }
    }
}

impl DerefMut for InitializedWriteAccess {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut_slice()
    }
}

impl Drop for InitializedWriteAccess {
    fn drop(&mut self) {
        if let Some(access) = self.access.take() {
            // SAFETY: access is owned by self and is released exactly once.
            unsafe { ffi::shared_buffer_c_write_access_destroy(access.as_ptr()) }
        }
        drop(self.owner.take());
    }
}

/// A scoped read-only lease over an initialized [`Buffer`] payload.
pub struct ReadAccess<'a> {
    raw: NonNull<ffi::ReadAccess>,
    _buffer: PhantomData<&'a Buffer>,
}

impl ReadAccess<'_> {
    /// Borrow the initialized payload for the lifetime of this read lease.
    pub fn as_slice(&self) -> &[u8] {
        // SAFETY: the native access owns a valid read lease and reports its
        // matching payload size until Drop releases the lease.
        unsafe {
            slice::from_raw_parts(
                ffi::shared_buffer_c_read_access_data(self.raw.as_ptr()),
                ffi::shared_buffer_c_read_access_size(self.raw.as_ptr()),
            )
        }
    }
}

impl Deref for ReadAccess<'_> {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        self.as_slice()
    }
}

impl Drop for ReadAccess<'_> {
    fn drop(&mut self) {
        // SAFETY: raw is the access returned by the matching C ABI.
        unsafe { ffi::shared_buffer_c_read_access_destroy(self.raw.as_ptr()) }
    }
}

#[cfg(test)]
mod tests {
    use super::{Buffer, Error, UninitializedBuffer};
    use std::mem::MaybeUninit;

    #[test]
    fn rejects_empty_allocations() {
        assert!(matches!(
            UninitializedBuffer::new(0),
            Err(Error::InvalidArgument)
        ));
    }

    #[test]
    fn writes_maybe_uninit_then_reads_initialized_bytes() {
        let uninitialized = UninitializedBuffer::new(4).unwrap();
        let mut write = uninitialized.write().unwrap();
        let bytes: &mut [MaybeUninit<u8>] = &mut write;
        for (index, byte) in bytes.iter_mut().enumerate() {
            byte.write((index + 1) as u8);
        }
        let buffer: Buffer = unsafe { write.assume_init() }.finish();
        let read = buffer.read().unwrap();
        assert_eq!(&*read, &[1, 2, 3, 4]);
    }

    #[test]
    fn safe_helpers_initialize_the_whole_payload() {
        let write = UninitializedBuffer::new(4).unwrap().write().unwrap();
        let buffer = write.write_from_slice(&[1, 2, 3, 4]).unwrap().finish();
        assert_eq!(&*buffer.read().unwrap(), &[1, 2, 3, 4]);

        let filled = UninitializedBuffer::new(2)
            .unwrap()
            .write()
            .unwrap()
            .fill(7)
            .finish();
        assert_eq!(&*filled.read().unwrap(), &[7, 7]);
    }

    #[test]
    fn length_mismatch_keeps_the_write_access_usable() {
        let write = UninitializedBuffer::new(4).unwrap().write().unwrap();
        let write = match write.write_from_slice(&[1, 2]) {
            Err(error) => {
                assert_eq!(error.expected(), 4);
                assert_eq!(error.actual(), 2);
                error.into_write_access()
            }
            Ok(_) => panic!("short input unexpectedly initialized the buffer"),
        };
        let buffer = write.write_from_slice(&[1, 2, 3, 4]).unwrap().finish();
        assert_eq!(&*buffer.read().unwrap(), &[1, 2, 3, 4]);
    }
}
