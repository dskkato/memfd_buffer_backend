cmake_minimum_required(VERSION 3.20)
project(memfd_buffer)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32)
  message(FATAL_ERROR
    "memfd_buffer supports Linux and Windows only (CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}').")
endif()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_buffer REQUIRED)
find_package(memfd_buffer_backend_msgs REQUIRED)
find_package(rmw REQUIRED)
find_package(rcutils REQUIRED)

set(memfd_buffer_sources
  src/memfd_buffer.cpp
  src/memfd_buffer_impl.cpp
  src/memfd_memory_pool.cpp
  src/host_endpoint_manager.cpp
)
if(WIN32)
  list(APPEND memfd_buffer_sources src/memfd_buffer_ipc_manager_windows.cpp)
else()
  list(APPEND memfd_buffer_sources src/memfd_buffer_ipc_manager.cpp)
endif()

add_library(${PROJECT_NAME} SHARED ${memfd_buffer_sources})

target_compile_definitions(${PROJECT_NAME} PRIVATE MEMFD_BUFFER_BUILDING_DLL)

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
)
if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE advapi32)
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE rt)
endif()

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

    # The default uncrustify configuration has conflicting rules for several
    # C++ constructs (for example, inline function bodies and template
    # declarations), so use clang-tidy as the C++ lint test instead.
    ament_lint_auto_find_test_dependencies()
  endif()

  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_memfd_buffer
    test/test_memfd_buffer.cpp
  )
  if(TARGET test_memfd_buffer)
    target_link_libraries(test_memfd_buffer
      ${PROJECT_NAME}
      rosidl_buffer::rosidl_buffer
    )
    if(NOT WIN32)
      target_link_libraries(test_memfd_buffer rt)
    endif()
  endif()

endif()

ament_package()
