# memfd_buffer_backend

memfd backend implementation for `rosidl::Buffer`, mitigating extra serialization/deserialization around large buffers, such as PointCloud2 or Image massages.

## Packages

- **memfd_buffer_backend** -- 
- **memfd_buffer_backend_msgs** -- 
- **memfd_buffer** -- 
- **memfd_buffre_backend_benchmark** --

## Additional notes

When experimenting this backend with `rmw_fastrtps`, publisher-subscriber message passing took the time whose time was proportional to the buffer size, while the wire message is almost constant.

This overhead comes from `rmw_fastrtps`'s `rmw_publisher` implementation, where the heap buffer is allocated with `std::vector<uint8_t>(buffer_size)`. This API allocates and initialize the buffer. So, it seems that initialization is the cause of that phenomenon.
