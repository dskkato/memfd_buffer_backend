// Copyright 2026 Open Source Robotics Foundation, Inc.
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

#ifndef MEMFD_BUFFER__MEMFD_BUFFER_HPP_
#define MEMFD_BUFFER__MEMFD_BUFFER_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "memfd_buffer/memfd_buffer_handle.hpp"
#include "memfd_buffer/memfd_memory_pool.hpp"

namespace memfd_buffer_backend
{

/// Low-level RAII view of a publisher or imported memfd payload.
class MemfdBuffer
{
public:
  MemfdBuffer() = default;

  MemfdBuffer(
    void * payload, std::size_t size,
    std::function<void(std::uint8_t *)> deleter,
    MemfdControlHeader * control = nullptr,
    std::shared_ptr<void> owner = nullptr,
    std::uint32_t block_id = 0,
    std::uint64_t mapped_size = 0);

  ~MemfdBuffer();

  MemfdBuffer(const MemfdBuffer &) = delete;
  MemfdBuffer & operator=(const MemfdBuffer &) = delete;

  MemfdBuffer(MemfdBuffer && other) noexcept;
  MemfdBuffer & operator=(MemfdBuffer && other) noexcept;

  ReadHandle get_read_handle() const;
  WriteHandle get_write_handle();
  void finalize_write_handle() const;

  void hold_reader_reference();

  std::size_t size() const {return size_;}
  std::uint8_t * get_ptr() {return data_ptr_;}
  const std::uint8_t * get_ptr() const {return data_ptr_;}
  MemfdControlHeader * control() const {return control_;}
  std::uint32_t block_id() const {return block_id_;}
  std::uint64_t mapped_size() const {return mapped_size_;}

private:
  void reset() noexcept;

  std::uint8_t * data_ptr_{nullptr};
  std::size_t size_{0};
  std::function<void(std::uint8_t *)> deleter_;
  MemfdControlHeader * control_{nullptr};
  std::shared_ptr<void> owner_;
  std::shared_ptr<void> held_reader_lease_;
  std::shared_ptr<HandleState> handle_state_;
  std::uint32_t block_id_{0};
  std::uint64_t mapped_size_{0};
};

}  // namespace memfd_buffer_backend

#endif  // MEMFD_BUFFER__MEMFD_BUFFER_HPP_
