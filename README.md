# memfd_buffer_backend

`memfd_buffer_backend` is a Linux-only `rosidl::Buffer` backend that shares
same-host, same-UID memfd allocations between ROS 2 endpoints.

## Packages

- `memfd_buffer`: memfd allocation, pooling, handles, and FD brokering.
- `memfd_buffer_backend`: rosidl buffer backend plugin.
- `memfd_buffer_backend_msgs`: descriptor message used for inter-process import.
- `memfd_buffer_backend_benchmark`: CPU/memfd pub/sub benchmark.

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

Build and run only this repository's packages as follows.  The Lyrical source
tree is not rebuilt; its existing install is used as the ROS 2 underlay.

```bash
source ~/ros2_lyrical/install/setup.bash
colcon --log-base log_old_memfd build \
  --base-paths src/memfd_buffer_backend \
  --packages-up-to memfd_buffer_backend_benchmark \
  --build-base build_old_memfd --install-base install_old_memfd
source install_old_memfd/setup.bash
ros2 run memfd_buffer_backend_benchmark run_e2e_benchmark.py \
  --output memfd-old-pubsub-results.csv
```

Use `--sizes`, `--count`, and `--rate-hz` to override the sweep.  `--count`
must be greater than the fixed 10-sample warm-up.
