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

#include "memfd_buffer/memfd_memory_pool.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

#include "memfd_buffer/memfd_buffer_ipc_manager.hpp"

namespace memfd_buffer_backend
{

namespace
{

#ifndef _WIN32
int create_memfd()
{
  const int fd = static_cast<int>(syscall(SYS_memfd_create, "rosidl_memfd_buffer", MFD_CLOEXEC));
  return fd;
}
#endif

#ifdef _WIN32
std::wstring widen_ascii(const std::string & value)
{
  return std::wstring(value.begin(), value.end());
}
#endif

std::uint64_t monotonic_time_us()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
}

}  // namespace

MemfdMemoryPool::MemfdMemoryPool() = default;

MemfdMemoryPool::~MemfdMemoryPool()
{
#ifndef _WIN32
  // The broker owns duplicated descriptor references and socket listeners.
  // Stop unregistering those before closing the publisher descriptors.
  broker_.reset();
#endif

  std::lock_guard<std::mutex> lock(mutex_);
  for (auto & block : all_blocks_) {
    if (block->control != nullptr) {
      block->control->~MemfdControlHeader();
      block->control = nullptr;
    }
    if (block->mapping != nullptr && block->mapped_size > 0) {
#ifdef _WIN32
      (void)UnmapViewOfFile(block->mapping);
#else
      munmap(block->mapping, block->mapped_size);
#endif
      block->mapping = nullptr;
    }
#ifdef _WIN32
    if (block->memfd != nullptr) {
      (void)CloseHandle(static_cast<HANDLE>(block->memfd));
      block->memfd = nullptr;
    }
#else
    if (block->memfd >= 0) {
      close(block->memfd);
      block->memfd = -1;
    }
    if (!block->socket_path.empty()) {
      unlink(block->socket_path.c_str());
    }
#endif
  }
}

MemfdBlock * MemfdMemoryPool::allocate(std::size_t payload_size)
{
  if (payload_size == 0) {
    throw std::invalid_argument("memfd pool cannot allocate a zero-size block");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto bucket = free_blocks_.find(payload_size);
  MemfdBlock * block = nullptr;
  if (bucket != free_blocks_.end()) {
    auto & candidates = bucket->second;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      if (try_claim_block(candidates[i])) {
        block = candidates[i];
        candidates[i] = candidates.back();
        candidates.pop_back();
        if (candidates.empty()) {
          free_blocks_.erase(bucket);
        }
        break;
      }
    }
  }

  if (block == nullptr) {
    block = create_block(payload_size);
  }

  block->in_use = true;
  block->current_uid = uid_dist_(uid_rng_);
  if (block->current_uid == 0) {
    block->current_uid = 1;
  }
  block->control->ipc_uid.store(0, std::memory_order_release);
  block->control->publish_timestamp_us.store(0, std::memory_order_release);
  release_memfd_reuse_claim(block->control);
  return block;
}

void MemfdMemoryPool::free(MemfdBlock * block)
{
  if (block == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!block->in_use) {
    return;
  }
  block->in_use = false;
  free_blocks_[block->payload_size].push_back(block);
}

std::function<void(std::uint8_t *)> MemfdMemoryPool::deleter(MemfdBlock * block)
{
  std::shared_ptr<MemfdMemoryPool> self;
  try {
    self = shared_from_this();
  } catch (const std::bad_weak_ptr &) {
    throw std::runtime_error("MemfdMemoryPool must be owned by shared_ptr");
  }
  return [self, block](std::uint8_t *) { self->free(block); };
}

std::uint64_t MemfdMemoryPool::assign_uid(MemfdBlock * block)
{
  if (block == nullptr || block->control == nullptr) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (block->current_uid == 0) {
    block->current_uid = uid_dist_(uid_rng_);
    if (block->current_uid == 0) {
      block->current_uid = 1;
    }
  }
  return block->current_uid;
}

void MemfdMemoryPool::mark_published(MemfdBlock * block)
{
  if (block == nullptr || block->control == nullptr) {
    return;
  }
  block->control->ipc_uid.store(block->current_uid, std::memory_order_release);
  block->control->publish_timestamp_us.store(monotonic_time_us(), std::memory_order_release);
}

std::string MemfdMemoryPool::register_block_for_ipc(MemfdBlock * block)
{
#ifdef _WIN32
  if (block == nullptr || block->memfd == nullptr) {
    return {};
  }
  return block->socket_path;
#else
  if (block == nullptr || broker_ == nullptr) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return broker_->register_block(block);
#endif
}

MemfdBlock * MemfdMemoryPool::find_block_for_ptr(const void * ptr) const
{
  if (ptr == nullptr) {
    return nullptr;
  }
  const auto address = reinterpret_cast<std::uintptr_t>(ptr);
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto & block : all_blocks_) {
    const auto start = reinterpret_cast<std::uintptr_t>(block->mapping) + kMemfdPayloadOffset;
    const auto end = reinterpret_cast<std::uintptr_t>(block->mapping) + block->mapped_size;
    if (address >= start && address < end) {
      return block.get();
    }
  }
  return nullptr;
}

bool MemfdMemoryPool::try_claim_block(MemfdBlock * block) const
{
  if (block == nullptr || block->control == nullptr) {
    return false;
  }
  const auto published = block->control->publish_timestamp_us.load(std::memory_order_acquire);
  if (published != 0) {
    const auto now = monotonic_time_us();
    if (now < published || (now - published) < kMemfdGracePeriodUs) {
      return false;
    }
  }
  return try_claim_memfd_reuse(block->control);
}

MemfdBlock * MemfdMemoryPool::create_block(std::size_t payload_size)
{
  if (payload_size > std::numeric_limits<std::size_t>::max() - kMemfdPayloadOffset) {
    throw std::length_error("memfd buffer mapping size overflows size_t");
  }
  const std::size_t mapped_size = kMemfdPayloadOffset + payload_size;
#ifdef _WIN32
  const std::uint32_t block_id = next_block_id_++;
  HANDLE mapping_handle = nullptr;
  std::string mapping_name;
  DWORD mapping_error = ERROR_SUCCESS;
  for (unsigned int attempt = 0; attempt < 8; ++attempt) {
    const auto nonce = uid_dist_(uid_rng_);
    mapping_name = "Local\\rosidl_memfd_buffer_" +
      std::to_string(GetCurrentProcessId()) + "_" + std::to_string(nonce) + "_" +
      std::to_string(block_id);
    const auto wide_name = widen_ascii(mapping_name);
    const auto size = static_cast<std::uint64_t>(mapped_size);
    mapping_handle = CreateFileMappingW(
      INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(size >> 32),
      static_cast<DWORD>(size & 0xffffffffULL), wide_name.c_str());
    if (mapping_handle == nullptr) {
      mapping_error = GetLastError();
      break;
    }
    mapping_error = GetLastError();
    if (mapping_error != ERROR_ALREADY_EXISTS) {
      break;
    }
    (void)CloseHandle(mapping_handle);
    mapping_handle = nullptr;
  }
  if (mapping_handle == nullptr || mapping_error == ERROR_ALREADY_EXISTS) {
    if (mapping_handle != nullptr) {
      (void)CloseHandle(mapping_handle);
    }
    throw std::runtime_error(
            "CreateFileMappingW failed: Windows error " + std::to_string(mapping_error));
  }
  void * mapping = MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, mapped_size);
  if (mapping == nullptr) {
    const DWORD error = GetLastError();
    (void)CloseHandle(mapping_handle);
    throw std::runtime_error("MapViewOfFile failed: Windows error " + std::to_string(error));
  }
#else
  if (mapped_size > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
    throw std::length_error("memfd buffer mapping is too large for ftruncate");
  }

  const int fd = create_memfd();
  if (fd < 0) {
    throw std::runtime_error("memfd_create failed: " + std::string(std::strerror(errno)));
  }
  if (ftruncate(fd, static_cast<off_t>(mapped_size)) != 0) {
    const std::string message = std::strerror(errno);
    close(fd);
    throw std::runtime_error("ftruncate for memfd failed: " + message);
  }

  void * mapping = mmap(nullptr, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    const std::string message = std::strerror(errno);
    close(fd);
    throw std::runtime_error("mmap for memfd failed: " + message);
  }
#endif

  auto block = std::make_unique<MemfdBlock>();
#ifdef _WIN32
  block->memfd = mapping_handle;
#else
  block->memfd = fd;
#endif
  block->mapping = mapping;
  block->mapped_size = mapped_size;
  block->payload_size = payload_size;
  block->block_id =
#ifdef _WIN32
    block_id;
#else
    next_block_id_++;
#endif
#ifdef _WIN32
  block->socket_path = mapping_name;
#endif
  block->control = new (mapping) MemfdControlHeader();
  block->control->payload_size = payload_size;
  block->control->ipc_uid.store(0, std::memory_order_relaxed);
  block->control->reader_state.store(0, std::memory_order_relaxed);
  block->control->publish_timestamp_us.store(0, std::memory_order_relaxed);

  MemfdBlock * result = block.get();
  all_blocks_.push_back(std::move(block));
  initialized_ = true;
  ipc_capable_ = true;
#ifndef _WIN32
  if (broker_ == nullptr) {
    broker_ = std::make_unique<MemfdFdBroker>();
  }
#endif
  return result;
}

}  // namespace memfd_buffer_backend
