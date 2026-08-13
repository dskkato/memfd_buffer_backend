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

#ifndef MEMFD_BUFFER__MEMFD_BUFFER_IMPL_HPP_
#define MEMFD_BUFFER__MEMFD_BUFFER_IMPL_HPP_

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "memfd_buffer/memfd_buffer.hpp"
#include "rosidl_buffer/buffer_impl_base.hpp"
#include "rosidl_buffer/cpu_buffer_impl.hpp"

namespace memfd_buffer_backend
{

class MemfdError : public std::runtime_error
{
public:
  explicit MemfdError(const std::string & message) : std::runtime_error(message) {}
};

template <typename T>
class MemfdBufferImpl : public rosidl::BufferImplBase<T>
{
public:
  MemfdBufferImpl() = default;

  explicit MemfdBufferImpl(std::size_t size) : size_(size)
  {
    if (size_ > 0) {
      allocate(size_);
    }
  }

  MemfdBufferImpl(MemfdBuffer && buffer, std::size_t size)
  : size_(size), memfd_buffer_(std::move(buffer))
  {
  }

  ~MemfdBufferImpl() override = default;

  MemfdBufferImpl(const MemfdBufferImpl &) = delete;
  MemfdBufferImpl & operator=(const MemfdBufferImpl &) = delete;
  MemfdBufferImpl(MemfdBufferImpl &&) = delete;
  MemfdBufferImpl & operator=(MemfdBufferImpl &&) = delete;

  std::string get_backend_type() const override { return "memfd"; }
  std::size_t size() const override { return size_; }

  void resize(std::size_t size)
  {
    if (size == size_) {
      return;
    }
    if (size == 0) {
      memfd_buffer_ = MemfdBuffer();
      size_ = 0;
      return;
    }

    MemfdBuffer new_buffer = allocate_raw(size);
    const std::size_t copy_size = std::min(size, size_) * sizeof(T);
    if (copy_size > 0 && memfd_buffer_.get_ptr() != nullptr) {
      ReadHandle read = memfd_buffer_.get_read_handle();
      WriteHandle write = new_buffer.get_write_handle();
      std::memcpy(write.get_ptr(), read.get_ptr(), copy_size);
    }
    memfd_buffer_ = std::move(new_buffer);
    size_ = size;
  }

  void clear()
  {
    memfd_buffer_ = MemfdBuffer();
    size_ = 0;
  }

  std::unique_ptr<rosidl::BufferImplBase<T>> to_cpu() const override
  {
    auto cpu = std::make_unique<rosidl::CpuBufferImpl<T>>();
    cpu->get_storage().resize(size_);
    if (size_ > 0) {
      ReadHandle read = memfd_buffer_.get_read_handle();
      std::memcpy(cpu->get_storage().data(), read.get_ptr(), size_ * sizeof(T));
    }
    return cpu;
  }

  std::unique_ptr<rosidl::BufferImplBase<T>> clone() const override
  {
    auto copy = std::make_unique<MemfdBufferImpl<T>>(size_);
    if (size_ > 0) {
      ReadHandle read = memfd_buffer_.get_read_handle();
      WriteHandle write = copy->memfd_buffer_.get_write_handle();
      std::memcpy(write.get_ptr(), read.get_ptr(), size_ * sizeof(T));
    }
    return copy;
  }

  MemfdBuffer & get_memfd_buffer() { return memfd_buffer_; }
  const MemfdBuffer & get_memfd_buffer() const { return memfd_buffer_; }

  static std::shared_ptr<MemfdMemoryPool> get_or_create_global_pool()
  {
    static std::shared_ptr<MemfdMemoryPool> pool = std::make_shared<MemfdMemoryPool>();
    return pool;
  }

  static bool is_pool_ipc_capable() { return get_or_create_global_pool()->is_ipc_capable(); }

private:
  static std::size_t byte_count(std::size_t count)
  {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw MemfdError("memfd buffer size overflows size_t");
    }
    return count * sizeof(T);
  }

  static MemfdBuffer allocate_raw(std::size_t count)
  {
    auto pool = get_or_create_global_pool();
    const std::size_t bytes = byte_count(count);
    MemfdBlock * block = pool->allocate(bytes);
    auto deleter = pool->deleter(block);
    auto * payload = reinterpret_cast<std::uint8_t *>(block->mapping) + sizeof(MemfdControlHeader);
    return MemfdBuffer(
      payload, bytes, std::move(deleter), block->control, nullptr, block->block_id,
      block->mapped_size);
  }

  void allocate(std::size_t count) { memfd_buffer_ = allocate_raw(count); }

  template <typename U>
  friend class MemfdBufferImpl;
  template <typename U>
  friend class MemfdBufferApiAccess;

  std::size_t size_{0};
  MemfdBuffer memfd_buffer_;
};

inline std::shared_ptr<MemfdMemoryPool> get_global_memfd_pool()
{
  return MemfdBufferImpl<std::uint8_t>::get_or_create_global_pool();
}

}  // namespace memfd_buffer_backend

#endif  // MEMFD_BUFFER__MEMFD_BUFFER_IMPL_HPP_
