cmake_minimum_required(VERSION 3.20)
project(memfd_buffer_backend_msgs)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/MemfdBufferDescriptor.msg"
)

option(ENABLE_LINT_TESTS "Enable lint tests" ON)

if(BUILD_TESTING)
  if(ENABLE_LINT_TESTS)
    find_package(ament_lint_auto REQUIRED)
    ament_lint_auto_find_test_dependencies()
  endif()
endif()

ament_package()
