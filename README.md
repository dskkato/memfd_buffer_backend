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
source /opt/ros/lyrical/setup.bash
# or, if you built it from source
# source ~/ros2_lyrical/install/setup.bash
colcon build 
source install/setup.bash
ros2 run memfd_buffer_backend_benchmark run_e2e_benchmark.py \
  --output memfd-old-pubsub-results.csv \
  --raw-output memfd-old-pubsub-raw.csv
```

Use `--sizes`, `--count`, and `--rate-hz` to override the sweep.  `--count`
must be greater than the fixed 10-sample warm-up.  The optional raw output has
one row per measured sample and stores the publish timestamp, the duration of
the `publish()` call, the receive timestamp, and the end-to-end latency, all
in nanoseconds.  Allocation and payload initialization remain outside the
publish timing interval.

To rebuild each RMW variant from `origin/lyrical` and run the complete
16-way matrix (720 cases by default), use:

```bash
source ~/ros2_lyrical/install/setup.bash
source install/setup.bash
ros2 run memfd_buffer_backend_benchmark run_16way_benchmark.py \
  --output-dir benchmark-results-16way-rerun \
  --overwrite
```

The orchestration script applies the three patches independently, builds each
variant in a separate Release prefix, runs the existing end-to-end runner for
all payload sizes and communication/backend combinations, and restores the
original `rmw_fastrtps` branch and revision on exit.  In addition to the four
summary CSV files, it writes per-sample raw timing files to
`<output-dir>/raw/<variant>.csv`.  Use `--dry-run` to print the case count and
variants without changing the source checkout.
