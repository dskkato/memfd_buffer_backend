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

## Pub/sub benchmark

The benchmark compares the CPU and memfd backends in two modes:

- `inter_process`: publisher and subscriber run as separate processes.
- `intra_process_va`: both run in one process with
  `use_intra_process_comms(true)`, `SharedPtr` intra-process buffers, and a
  `unique_ptr` publish so that the buffer virtual address is shared.

The timed interval starts immediately before `publish()` and ends immediately
after the subscriber loads `data()[0]`.  Payload allocation and writing are
outside the interval.  Intra-process rows verify that publisher and subscriber
buffer addresses match.

Both modes use `rmw_fastrtps_cpp`, Reliable `KeepLast(10)`, 10 Hz, 30 samples,
and exclude the first 10 callbacks as warm-up.  The default sizes are
`64,1024,4096,16384,65536,262144,1048576,4194304,16777216` bytes.

Build and run only this repository's packages as follows.

```bash
source /opt/ros2/lyrical/setup.bash
# or, if you built it from source
# source ~/ros2_lyrical/install/setup.bash
colcon build 
source install/setup.bash
ros2 run memfd_buffer_backend_benchmark run_e2e_benchmark.py \
  --output memfd-old-pubsub-results.csv
```

Use `--sizes`, `--count`, and `--rate-hz` to override the sweep.  `--count`
must be greater than the fixed 10-sample warm-up.
