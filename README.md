# memfd_buffer_backend

`memfd_buffer_backend` is a `rosidl::Buffer` backend that shares memory between
compatible ROS 2 endpoints on Linux and Windows. Linux uses anonymous memfd
allocations; Windows uses session-local named file mappings.

## Attribution

This repository is developed and maintained outside the Open Source Robotics
Foundation (OSRF). Its implementation was developed with reference to the
OSRF [`rosidl_buffer_backends`](https://github.com/ros2/rosidl_buffer_backends)
implementation.

## Packages

- `memfd_buffer`: shared-memory allocation, pooling, handles, Linux FD
  brokering, and Windows named-mapping import.
- `memfd_buffer_backend_py`: Python zero-copy bindings for the memfd backend
  (installs the `memfd_buffer` Python module).
- `memfd_buffer_backend`: rosidl buffer backend plugin.
- `memfd_buffer_backend_msgs`: descriptor message used for inter-process import.

## Python zero-copy access

The `memfd_buffer_backend_py` package exposes scoped Python buffer-protocol
access through the `memfd_buffer` Python module. NumPy is optional and consumes
the standard `memoryview` without copying:

```python
import numpy as np
from memfd_buffer import allocate_buffer, read_buffer, write_buffer

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

## Benchmark

The benchmark package, benchmark results, report, and figures are maintained
in the separate [`memfd_buffer_backend_benchmark`](https://github.com/dskkato/memfd_buffer_backend_benchmark)
repository.
