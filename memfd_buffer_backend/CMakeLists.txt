cmake_minimum_required(VERSION 3.20)
project(memfd_buffer_backend)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32)
  message(FATAL_ERROR
    "memfd_buffer_backend supports Linux and Windows only "
    "(CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}').")
endif()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake_auto REQUIRED)
ament_auto_find_build_dependencies()

ament_auto_add_library(${PROJECT_NAME} SHARED
  src/memfd_buffer_backend_plugin.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)

target_link_libraries(${PROJECT_NAME}
  ${memfd_buffer_backend_msgs_TARGETS}
  rosidl_buffer_backend::rosidl_buffer_backend
  rosidl_runtime_cpp::rosidl_runtime_cpp
  rosidl_buffer_backend_registry::rosidl_buffer_backend_registry
  rosidl_typesupport_cpp::rosidl_typesupport_cpp
  pluginlib::pluginlib
  memfd_buffer::memfd_buffer
  rcutils::rcutils
)

pluginlib_export_plugin_description_file(rosidl_buffer_backend memfd_buffer_plugin.xml)

option(ENABLE_LINT_TESTS "Enable lint tests" ON)

if(BUILD_TESTING)
  if(ENABLE_LINT_TESTS)
    find_program(CLANG_TIDY_EXECUTABLE
      NAMES clang-tidy clang-tidy-10 clang-tidy-11 clang-tidy-6.0
    )
    # Visual Studio generators do not produce compile_commands.json, even
    # when CMAKE_EXPORT_COMPILE_COMMANDS is enabled.  Do not register a
    # clang-tidy test that cannot run in that configuration.
    if(CLANG_TIDY_EXECUTABLE AND EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
      find_package(ament_cmake_clang_tidy REQUIRED)
      set(AMENT_LINT_AUTO_EXCLUDE ament_cmake_uncrustify)
    else()
      set(AMENT_LINT_AUTO_EXCLUDE
        ament_cmake_clang_tidy
        ament_cmake_uncrustify
      )
    endif()
    find_package(ament_lint_auto REQUIRED)

    # The default uncrustify configuration conflicts with the C++ constructs
    # used by this package.  Keep clang-tidy as the C++ lint test instead.
    ament_lint_auto_find_test_dependencies()
  endif()

  find_package(ament_cmake_gtest REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(rclcpp_components REQUIRED)
  find_package(sensor_msgs REQUIRED)
  find_package(std_msgs REQUIRED)
  find_package(launch_testing_ament_cmake REQUIRED)

  ament_add_gtest(test_memfd_buffer_backend
    test/test_memfd_buffer_backend.cpp
  )
  if(TARGET test_memfd_buffer_backend)
    target_link_libraries(test_memfd_buffer_backend
      ${PROJECT_NAME}
      memfd_buffer::memfd_buffer
      ${memfd_buffer_backend_msgs_TARGETS}
      rosidl_buffer_backend::rosidl_buffer_backend
      rosidl_runtime_cpp::rosidl_runtime_cpp
      rosidl_typesupport_cpp::rosidl_typesupport_cpp
    )
  endif()

  ament_auto_add_library(memfd_image_dso_publisher_component SHARED
    NO_TARGET_LINK_LIBRARIES
    test/src/memfd_image_dso_publisher_node.cpp
  )
  set_target_properties(memfd_image_dso_publisher_component PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
  )
  target_link_libraries(memfd_image_dso_publisher_component
    memfd_buffer::memfd_buffer
    rclcpp::rclcpp
    rclcpp_components::component
    ${sensor_msgs_TARGETS}
    rosidl_runtime_cpp::rosidl_runtime_cpp
  )
  rclcpp_components_register_nodes(memfd_image_dso_publisher_component
    "MemfdImageDsoPublisher")

  ament_auto_add_library(memfd_image_subscriber_component SHARED
    NO_TARGET_LINK_LIBRARIES
    test/src/memfd_image_subscriber_node.cpp
  )
  target_link_libraries(memfd_image_subscriber_component
    memfd_buffer::memfd_buffer
    rclcpp::rclcpp
    rclcpp_components::component
    ${sensor_msgs_TARGETS}
    ${std_msgs_TARGETS}
    rosidl_runtime_cpp::rosidl_runtime_cpp
  )
  rclcpp_components_register_nodes(memfd_image_subscriber_component
    "MemfdImageSubscriber")

  add_launch_test(test/test_memfd_pool_dso_fastrtps_launch.py
    TARGET test_memfd_pool_dso_fastrtps_launch
    TIMEOUT 45
    ENV "ROS_LOG_DIR=${CMAKE_CURRENT_BINARY_DIR}/ros_log"
  )

endif()

ament_auto_package(USE_SCOPED_HEADER_INSTALL_DIR)
