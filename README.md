# memfd_buffer_backend

`memfd_buffer_backend` is a Linux-only `rosidl::Buffer` backend that shares
same-host, same-UID memfd allocations between ROS 2 endpoints.

## Attribution

This repository is developed and maintained outside the Open Source Robotics
Foundation (OSRF). Its implementation was developed with reference to the
OSRF [`rosidl_buffer_backends`](https://github.com/ros2/rosidl_buffer_backends)
implementation.

## Packages

- `memfd_buffer`: memfd allocation, pooling, handles, and FD brokering.
- `memfd_buffer_backend`: rosidl buffer backend plugin.
- `memfd_buffer_backend_msgs`: descriptor message used for inter-process import.

## Benchmark

The benchmark package, benchmark results, report, and figures are maintained
in the separate [`memfd_buffer_backend_benchmark`](https://github.com/dskkato/memfd_buffer_backend_benchmark)
repository.
