cmake_minimum_required(VERSION 3.20)
project(memfd_buffer_backend_py)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32)
  message(FATAL_ERROR
    "memfd_buffer_backend_py supports Linux and Windows only "
    "(CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}').")
endif()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(memfd_buffer REQUIRED)
find_package(rosidl_buffer REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 REQUIRED)

pybind11_add_module(_memfd_buffer_py
  src/memfd_buffer_py.cpp
)
target_link_libraries(_memfd_buffer_py PRIVATE
  memfd_buffer::memfd_buffer
  rosidl_buffer::rosidl_buffer
)

# Keep the public Python module name stable while moving its ROS package out of
# the core C++ package.
ament_python_install_package(memfd_buffer)

install(TARGETS _memfd_buffer_py
  LIBRARY DESTINATION "${PYTHON_INSTALL_DIR}/memfd_buffer"
  RUNTIME DESTINATION "${PYTHON_INSTALL_DIR}/memfd_buffer"
)

option(ENABLE_LINT_TESTS "Enable lint tests" ON)

if(BUILD_TESTING)
  find_package(ament_cmake_pytest REQUIRED)

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

    # The default uncrustify configuration has conflicting rules for several
    # C++ constructs, so use clang-tidy as the C++ lint test instead.
    ament_lint_auto_find_test_dependencies()
  endif()

  ament_add_pytest_test(test_memfd_buffer_py
    test/test_memfd_buffer_py.py
    # This is a standalone pytest test. Avoid loading unrelated ROS pytest
    # plugins, whose launch_testing hook versions may not match in CI.
    ENV PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
  )
endif()

ament_export_dependencies(memfd_buffer rosidl_buffer)
ament_package()
