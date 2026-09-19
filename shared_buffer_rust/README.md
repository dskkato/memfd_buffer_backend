# shared_buffer_rust

Rust RAII bindings for the `shared_buffer` ROS 2 backend.

The crate keeps the native shared-memory allocation alive with `Buffer` and
exposes scoped `ReadAccess` / `WriteAccess` slices:

```rust
use shared_buffer_rust::Buffer;

let mut buffer = Buffer::new(1024)?;
{
    let mut write = buffer.write()?;
    write.fill(7);
}

let read = buffer.read()?;
assert_eq!(read[0], 7);
# Ok::<(), shared_buffer_rust::Error>(())
```

Build it as an `ament_cargo` package after building `shared_buffer` and
sourcing the ROS 2 workspace. `cargo` locates the native library through
`AMENT_PREFIX_PATH`; `SHARED_BUFFER_PREFIX` can be used for a standalone
installation prefix.

The crate currently manages shared payloads from Rust. Generated Rust ROS
messages do not yet expose a portable field type for C++ `rosidl::Buffer`, so
message-field assignment and Rust-side descriptor transport are not included.
