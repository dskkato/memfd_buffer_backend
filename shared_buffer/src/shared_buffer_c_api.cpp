// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "shared_buffer/shared_buffer_c_api.h"

#include <memory>
#include <new>

#include "shared_buffer/shared_buffer_api.hpp"

struct shared_buffer_rust_buffer
{
  std::shared_ptr<rosidl::Buffer<std::uint8_t>> value;
};

struct shared_buffer_rust_read_access
{
  std::shared_ptr<rosidl::Buffer<std::uint8_t>> buffer;
  shared_buffer::ReadHandle handle;
};

struct shared_buffer_rust_write_access
{
  std::shared_ptr<rosidl::Buffer<std::uint8_t>> buffer;
  shared_buffer::WriteHandle handle;
};

extern "C" int shared_buffer_rust_buffer_new(
  size_t byte_count, shared_buffer_rust_buffer_t **out)
{
  if (out == nullptr || byte_count == 0) {
    return SHARED_BUFFER_RUST_INVALID_ARGUMENT;
  }
  *out = nullptr;
  try {
    auto result = std::make_unique<shared_buffer_rust_buffer>();
    result->value = shared_buffer::detail::allocate_shared_buffer_shared(byte_count);
    *out = result.release();
    return SHARED_BUFFER_RUST_OK;
  } catch (const std::bad_alloc &) {
    return SHARED_BUFFER_RUST_ALLOCATION_FAILED;
  } catch (...) {
    return SHARED_BUFFER_RUST_ACCESS_FAILED;
  }
}

extern "C" void shared_buffer_rust_buffer_destroy(shared_buffer_rust_buffer_t * buffer)
{
  delete buffer;
}

extern "C" size_t shared_buffer_rust_buffer_size(const shared_buffer_rust_buffer_t * buffer)
{
  return buffer == nullptr || buffer->value == nullptr ? 0 : buffer->value->size();
}

extern "C" int shared_buffer_rust_read_access_new(
  const shared_buffer_rust_buffer_t * buffer, shared_buffer_rust_read_access_t **out)
{
  if (out == nullptr || buffer == nullptr || buffer->value == nullptr) {
    return SHARED_BUFFER_RUST_INVALID_ARGUMENT;
  }
  *out = nullptr;
  try {
    auto result = std::make_unique<shared_buffer_rust_read_access>();
    result->buffer = buffer->value;
    result->handle = shared_buffer::from_input_buffer(*result->buffer);
    *out = result.release();
    return SHARED_BUFFER_RUST_OK;
  } catch (...) {
    return SHARED_BUFFER_RUST_ACCESS_FAILED;
  }
}

extern "C" void shared_buffer_rust_read_access_destroy(
  shared_buffer_rust_read_access_t * access)
{
  delete access;
}

extern "C" const uint8_t * shared_buffer_rust_read_access_data(
  const shared_buffer_rust_read_access_t * access)
{
  return access == nullptr ? nullptr : access->handle.get_ptr();
}

extern "C" size_t shared_buffer_rust_read_access_size(
  const shared_buffer_rust_read_access_t * access)
{
  return access == nullptr || access->buffer == nullptr ? 0 : access->buffer->size();
}

extern "C" int shared_buffer_rust_write_access_new(
  shared_buffer_rust_buffer_t * buffer, shared_buffer_rust_write_access_t **out)
{
  if (out == nullptr || buffer == nullptr || buffer->value == nullptr) {
    return SHARED_BUFFER_RUST_INVALID_ARGUMENT;
  }
  *out = nullptr;
  try {
    auto result = std::make_unique<shared_buffer_rust_write_access>();
    result->buffer = buffer->value;
    result->handle = shared_buffer::from_output_buffer(*result->buffer);
    *out = result.release();
    return SHARED_BUFFER_RUST_OK;
  } catch (...) {
    return SHARED_BUFFER_RUST_ACCESS_FAILED;
  }
}

extern "C" void shared_buffer_rust_write_access_destroy(
  shared_buffer_rust_write_access_t * access)
{
  delete access;
}

extern "C" uint8_t * shared_buffer_rust_write_access_data(
  shared_buffer_rust_write_access_t * access)
{
  return access == nullptr ? nullptr : access->handle.get_ptr();
}

extern "C" size_t shared_buffer_rust_write_access_size(
  const shared_buffer_rust_write_access_t * access)
{
  return access == nullptr || access->buffer == nullptr ? 0 : access->buffer->size();
}
