//! Safe Rust access to shared-memory payloads allocated by `shared_buffer`.
//!
//! `Buffer` owns the C++ `rosidl::Buffer<uint8_t>` allocation. `ReadAccess` and
//! `WriteAccess` are scoped leases: their slices must not outlive the access
//! value. A write lease is exclusive at the Rust type level and is finalized
//! when it is dropped.
//!
//! This crate currently exposes the payload API. Rust message generators do
//! not yet provide a portable field type for C++ `rosidl::Buffer`, so assigning
//! this allocation directly to a generated Rust message is not supported yet.

use std::fmt;
use std::marker::PhantomData;
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
        pub fn shared_buffer_rust_buffer_new(byte_count: usize, out: *mut *mut Buffer) -> c_int;
        pub fn shared_buffer_rust_buffer_destroy(buffer: *mut Buffer);
        pub fn shared_buffer_rust_buffer_size(buffer: *const Buffer) -> usize;

        pub fn shared_buffer_rust_read_access_new(
            buffer: *const Buffer,
            out: *mut *mut ReadAccess,
        ) -> c_int;
        pub fn shared_buffer_rust_read_access_destroy(access: *mut ReadAccess);
        pub fn shared_buffer_rust_read_access_data(access: *const ReadAccess) -> *const c_uchar;
        pub fn shared_buffer_rust_read_access_size(access: *const ReadAccess) -> usize;

        pub fn shared_buffer_rust_write_access_new(
            buffer: *mut Buffer,
            out: *mut *mut WriteAccess,
        ) -> c_int;
        pub fn shared_buffer_rust_write_access_destroy(access: *mut WriteAccess);
        pub fn shared_buffer_rust_write_access_data(access: *mut WriteAccess) -> *mut c_uchar;
        pub fn shared_buffer_rust_write_access_size(access: *const WriteAccess) -> usize;
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

/// An owned shared-memory payload.
pub struct Buffer {
    raw: NonNull<ffi::Buffer>,
    // The C++ Buffer and its handle state are not declared thread-safe.
    _not_thread_safe: PhantomData<std::rc::Rc<()>>,
}

impl Buffer {
    /// Allocate `byte_count` bytes from the shared-buffer pool.
    pub fn new(byte_count: usize) -> Result<Self, Error> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: `raw` points to valid output storage for the C ABI.
        unsafe { map_status(ffi::shared_buffer_rust_buffer_new(byte_count, &mut raw))? };
        let raw = NonNull::new(raw).ok_or(Error::AllocationFailed)?;
        Ok(Self {
            raw,
            _not_thread_safe: PhantomData,
        })
    }

    /// Return the payload size in bytes.
    pub fn len(&self) -> usize {
        // SAFETY: `self.raw` is owned and valid for the lifetime of self.
        unsafe { ffi::shared_buffer_rust_buffer_size(self.raw.as_ptr()) }
    }

    /// Return whether the payload has no bytes.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Acquire read-only access to the payload.
    pub fn read(&self) -> Result<ReadAccess<'_>, Error> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: self.raw is valid and raw is valid output storage.
        unsafe {
            map_status(ffi::shared_buffer_rust_read_access_new(
                self.raw.as_ptr(),
                &mut raw,
            ))?;
        }
        let raw = NonNull::new(raw).ok_or(Error::AccessFailed)?;
        Ok(ReadAccess {
            raw,
            _buffer: PhantomData,
        })
    }

    /// Acquire exclusive mutable access to the payload.
    pub fn write(&mut self) -> Result<WriteAccess<'_>, Error> {
        let mut raw = std::ptr::null_mut();
        // SAFETY: self.raw is valid and raw is valid output storage.
        unsafe {
            map_status(ffi::shared_buffer_rust_write_access_new(
                self.raw.as_ptr(),
                &mut raw,
            ))?;
        }
        let raw = NonNull::new(raw).ok_or(Error::AccessFailed)?;
        Ok(WriteAccess {
            raw,
            _buffer: PhantomData,
        })
    }
}

impl Drop for Buffer {
    fn drop(&mut self) {
        // SAFETY: self.raw is the allocation returned by the matching C ABI.
        unsafe { ffi::shared_buffer_rust_buffer_destroy(self.raw.as_ptr()) }
    }
}

/// A scoped read-only lease over a [`Buffer`] payload.
pub struct ReadAccess<'a> {
    raw: NonNull<ffi::ReadAccess>,
    _buffer: PhantomData<&'a Buffer>,
}

impl ReadAccess<'_> {
    /// Borrow the payload for the lifetime of this access lease.
    pub fn as_slice(&self) -> &[u8] {
        // SAFETY: the native access owns a valid read lease and reports its
        // matching payload size until Drop releases the lease.
        unsafe {
            slice::from_raw_parts(
                ffi::shared_buffer_rust_read_access_data(self.raw.as_ptr()),
                ffi::shared_buffer_rust_read_access_size(self.raw.as_ptr()),
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
        // SAFETY: self.raw is the access returned by the matching C ABI.
        unsafe { ffi::shared_buffer_rust_read_access_destroy(self.raw.as_ptr()) }
    }
}

/// A scoped exclusive mutable lease over a [`Buffer`] payload.
pub struct WriteAccess<'a> {
    raw: NonNull<ffi::WriteAccess>,
    _buffer: PhantomData<&'a mut Buffer>,
}

impl WriteAccess<'_> {
    /// Borrow the payload mutably for the lifetime of this access lease.
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        // SAFETY: the native access owns a valid exclusive write lease and
        // reports its matching payload size until Drop releases the lease.
        unsafe {
            slice::from_raw_parts_mut(
                ffi::shared_buffer_rust_write_access_data(self.raw.as_ptr()),
                ffi::shared_buffer_rust_write_access_size(self.raw.as_ptr()),
            )
        }
    }
}

impl Deref for WriteAccess<'_> {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        // SAFETY: a write access also provides a valid shared view while it is
        // held; the returned borrow is tied to the access lifetime.
        unsafe {
            slice::from_raw_parts(
                ffi::shared_buffer_rust_write_access_data(
                    self.raw.as_ptr() as *mut ffi::WriteAccess
                ),
                ffi::shared_buffer_rust_write_access_size(self.raw.as_ptr()),
            )
        }
    }
}

impl DerefMut for WriteAccess<'_> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut_slice()
    }
}

impl Drop for WriteAccess<'_> {
    fn drop(&mut self) {
        // SAFETY: self.raw is the access returned by the matching C ABI.
        unsafe { ffi::shared_buffer_rust_write_access_destroy(self.raw.as_ptr()) }
    }
}

#[cfg(test)]
mod tests {
    use super::{Buffer, Error};

    #[test]
    fn rejects_empty_allocations() {
        assert!(matches!(Buffer::new(0), Err(Error::InvalidArgument)));
    }

    #[test]
    fn read_write_access_is_scoped() {
        let mut buffer = Buffer::new(4).unwrap();
        {
            let mut write = buffer.write().unwrap();
            write.copy_from_slice(&[1, 2, 3, 4]);
        }
        let read = buffer.read().unwrap();
        assert_eq!(&*read, &[1, 2, 3, 4]);
    }
}
