# shared_buffer_rust

Rust RAII bindings for the `shared_buffer` ROS 2 backend.

The crate keeps the native shared-memory allocation alive through
`UninitializedBuffer` / `Buffer` and exposes scoped `ReadAccess` /
`WriteAccess` slices:

```rust
use shared_buffer_rust::UninitializedBuffer;

let mut write = UninitializedBuffer::new(1024)?.write()?;
write.fill(7);
let buffer = write.finish()?;
{
    let read = buffer.read()?;
    assert_eq!(read[0], 7);
}
# Ok::<(), shared_buffer_rust::Error>(())
```

Build it as an `ament_cargo` package after building `shared_buffer` and
sourcing the ROS 2 workspace. `cargo` locates the native library through
`AMENT_PREFIX_PATH`; `SHARED_BUFFER_PREFIX` can be used for a standalone
installation prefix.

The allocation lifecycle is `UninitializedBuffer -> WriteAccess (MaybeUninit<u8>
view) -> Buffer -> ReadAccess`. The native write lease is
one-shot: dropping or finishing it finalizes the buffer, and no later write
access can be acquired. `write_from_slice` validates the length before
touching the payload and keeps the write access usable on mismatch. Use
`write_from_slice` or `fill`, followed by `finish`, for safe initialization;
`assume_init` is available for callers that initialized every byte manually
and is `unsafe`.

The crate currently manages shared payloads from Rust. Generated Rust ROS
messages do not yet expose a portable field type for C++ `rosidl::Buffer`, so
message-field assignment and Rust-side descriptor transport are not included.
