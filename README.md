# shared_buffer_backend

`shared_buffer_backend` is a `rosidl::Buffer` backend that shares memory between
compatible ROS 2 endpoints on Linux and Windows. Linux uses anonymous memfd
allocations; Windows uses session-local named file mappings.

See the [user guide](docs/index.html) for an illustrated overview, setup steps,
and C++ / Python examples. The guide is a standalone site ready to be served
with GitHub Pages directly from the `docs/` directory.

## Attribution

This repository is developed and maintained outside the Open Source Robotics
Foundation (OSRF). Its implementation was developed with reference to the
OSRF [`rosidl_buffer_backends`](https://github.com/ros2/rosidl_buffer_backends)
implementation.

## Packages

- `shared_buffer`: shared-memory allocation, pooling, handles, Linux FD
  brokering, and Windows named-mapping import.
- `shared_buffer_backend_py`: Python zero-copy bindings for the shared-memory backend
  (installs the `shared_buffer` Python module).
- `shared_buffer_rust`: Rust RAII bindings for shared-memory allocation and scoped
  read/write access.
- `shared_buffer_backend`: rosidl buffer backend plugin.
- `shared_buffer_backend_msgs`: descriptor message used for inter-process import.

## Python zero-copy access

The `shared_buffer_backend_py` package exposes scoped Python buffer-protocol
access through the `shared_buffer` Python module. NumPy is optional and consumes
the standard `memoryview` without copying:

```python
import numpy as np
from shared_buffer import allocate_buffer, read_buffer, write_buffer

buffer = allocate_buffer(1024)
with write_buffer(buffer) as view:
    array = np.frombuffer(view, dtype=np.uint8)
    array[:] = 7
    del array  # Derived views must be released before leaving the scope.

message.data = buffer
```

```python
with read_buffer(received_message.data) as view:
    array = np.frombuffer(view, dtype=np.uint8)
    process(array)
    del array  # Derived views must be released before leaving the scope.
```

Read views are read-only. Write views are exclusive and are finalized when the
scope closes. A derived view must not escape the scope; closing raises
`BufferError` while an exported NumPy or memoryview object remains alive.

## Rust zero-copy access

The `shared_buffer_rust` package provides an `ament_cargo` crate with the same
scoped lifecycle. It keeps the native allocation alive and exposes Rust slices
whose lifetimes are tied to their access lease:

Build the Rust package with a Rust toolchain and the `colcon-cargo` /
`colcon-ros-cargo` extensions (or build the crate directly with Cargo after
sourcing the ROS 2 workspace).

```rust
use shared_buffer_rust::UninitializedBuffer;

let mut write = UninitializedBuffer::new(1024)?.write()?;
write.fill(7);
let buffer = write.finish()?;
let read = buffer.read()?;
process(&read);
# Ok::<(), shared_buffer_rust::Error>(())
```

The lifecycle is `UninitializedBuffer -> WriteAccess (MaybeUninit<u8> view)
-> Buffer -> ReadAccess`. The native write lease is
one-shot: dropping or finishing it finalizes the buffer, and no later write
access can be acquired. `write_from_slice` validates the length before
touching the payload and keeps the write access usable on mismatch. Use
`write_from_slice` or `fill`, followed by `finish`, for safe initialization;
`assume_init` is available for callers that initialized every byte manually
and is `unsafe`.

The crate currently manages the shared payload from Rust. Rust message
generators do not yet expose a portable field type for C++ `rosidl::Buffer`,
so assigning this allocation directly to a generated Rust message is not yet
supported.

## Benchmark

The benchmark package, benchmark results, report, and figures are maintained
in the separate [`memfd_buffer_backend_benchmark`](https://github.com/dskkato/memfd_buffer_backend_benchmark)
repository.
