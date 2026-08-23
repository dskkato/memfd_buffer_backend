cmake_minimum_required(VERSION 3.20)
project(memfd_buffer)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "memfd_buffer supports Linux only (CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}').")
endif()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(rosidl_buffer REQUIRED)
find_package(memfd_buffer_backend_msgs REQUIRED)
find_package(rmw REQUIRED)
find_package(rcutils REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 REQUIRED)

add_library(${PROJECT_NAME} SHARED
  src/memfd_buffer.cpp
  src/memfd_buffer_ipc_manager.cpp
  src/memfd_memory_pool.cpp
  src/host_endpoint_manager.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    rosidl_buffer::rosidl_buffer
    rmw::rmw
    rcutils::rcutils
    ${memfd_buffer_backend_msgs_TARGETS}
  PRIVATE
    rt
)

pybind11_add_module(_memfd_buffer_py
  src/memfd_buffer_py.cpp
)
target_link_libraries(_memfd_buffer_py PRIVATE
  ${PROJECT_NAME}
  rosidl_buffer::rosidl_buffer
)

ament_python_install_package(memfd_buffer)

install(TARGETS _memfd_buffer_py
  LIBRARY DESTINATION "${PYTHON_INSTALL_DIR}/memfd_buffer"
)

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include/${PROJECT_NAME}
)

ament_export_targets(${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(rosidl_buffer memfd_buffer_backend_msgs rmw rcutils)
ament_export_libraries(${PROJECT_NAME})
ament_export_include_directories(include/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_clang_tidy REQUIRED)
  find_package(ament_lint_auto REQUIRED)

  # The default uncrustify configuration has conflicting rules for several
  # C++ constructs (for example, inline function bodies and template
  # declarations), so use clang-tidy as the C++ lint test instead.
  set(AMENT_LINT_AUTO_EXCLUDE ament_cmake_uncrustify)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)

  ament_add_gtest(test_memfd_buffer
    test/test_memfd_buffer.cpp
  )
  if(TARGET test_memfd_buffer)
    target_link_libraries(test_memfd_buffer
      ${PROJECT_NAME}
      rosidl_buffer::rosidl_buffer
      rt
    )
  endif()

  ament_add_pytest_test(test_memfd_buffer_py
    test/test_memfd_buffer_py.py
    # This is a standalone pytest test.  Avoid loading unrelated ROS pytest
    # plugins, whose launch_testing hook versions may not match in CI.
    ENV PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
  )

endif()

ament_package()
