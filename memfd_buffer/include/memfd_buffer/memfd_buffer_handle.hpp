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

#ifndef MEMFD_BUFFER__MEMFD_BUFFER_HANDLE_HPP_
#define MEMFD_BUFFER__MEMFD_BUFFER_HANDLE_HPP_

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include "rosidl_buffer/buffer.hpp"

namespace memfd_buffer_backend
{

class MemfdBuffer;

struct HandleState
{
  enum class State
  {
    Unset,
    InUse,
    Finalized
  };

  std::mutex mutex;
  State state{State::Unset};
};

/// Scoped const access to a memfd payload.  The handle owns one logical
/// reader reference until it is destroyed.
class ReadHandle
{
public:
  ReadHandle() = default;
  ReadHandle(const ReadHandle &) = delete;
  ReadHandle & operator=(const ReadHandle &) = delete;

  ReadHandle(ReadHandle && other) noexcept;
  ReadHandle & operator=(ReadHandle && other) noexcept;
  ~ReadHandle();

  const std::uint8_t * get_ptr() const { return data_ptr_; }

  std::shared_ptr<rosidl::Buffer<std::uint8_t>> get_promoted_buffer() const
  {
    return promoted_buffer_;
  }

  void set_promoted_buffer(std::shared_ptr<rosidl::Buffer<std::uint8_t>> buffer)
  {
    promoted_buffer_ = std::move(buffer);
  }

private:
  friend class MemfdBuffer;

  ReadHandle(
    const std::uint8_t * data_ptr, std::shared_ptr<void> reader_lease, std::shared_ptr<void> owner);

  void release() noexcept;

  const std::uint8_t * data_ptr_{nullptr};
  std::shared_ptr<void> reader_lease_;
  std::shared_ptr<void> owner_;
  std::shared_ptr<rosidl::Buffer<std::uint8_t>> promoted_buffer_;
};

/// Scoped mutable access to a memfd payload.  Only one writer can be active
/// and its destruction finalizes the local write state idempotently.
class WriteHandle
{
public:
  WriteHandle() = default;
  WriteHandle(const WriteHandle &) = delete;
  WriteHandle & operator=(const WriteHandle &) = delete;

  WriteHandle(WriteHandle && other) noexcept;
  WriteHandle & operator=(WriteHandle && other) noexcept;
  ~WriteHandle();

  std::uint8_t * get_ptr() { return data_ptr_; }

  std::shared_ptr<rosidl::Buffer<std::uint8_t>> get_promoted_buffer() const
  {
    return promoted_buffer_;
  }

  void set_promoted_buffer(std::shared_ptr<rosidl::Buffer<std::uint8_t>> buffer)
  {
    promoted_buffer_ = std::move(buffer);
  }

private:
  friend class MemfdBuffer;

  WriteHandle(
    std::uint8_t * data_ptr, std::shared_ptr<HandleState> state, std::shared_ptr<void> owner);

  void release() noexcept;

  std::uint8_t * data_ptr_{nullptr};
  std::shared_ptr<HandleState> state_;
  std::shared_ptr<void> owner_;
  std::shared_ptr<rosidl::Buffer<std::uint8_t>> promoted_buffer_;
};

}  // namespace memfd_buffer_backend

#endif  // MEMFD_BUFFER__MEMFD_BUFFER_HANDLE_HPP_
