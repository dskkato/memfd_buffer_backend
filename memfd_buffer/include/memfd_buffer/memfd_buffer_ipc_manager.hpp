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

#ifndef MEMFD_BUFFER__MEMFD_BUFFER_IPC_MANAGER_HPP_
#define MEMFD_BUFFER__MEMFD_BUFFER_IPC_MANAGER_HPP_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "memfd_buffer/memfd_memory_pool.hpp"

namespace memfd_buffer_backend
{

/// Reusable publisher-side Unix socket FD broker.
class MemfdFdBroker
{
public:
  MemfdFdBroker();
  ~MemfdFdBroker();

  MemfdFdBroker(const MemfdFdBroker &) = delete;
  MemfdFdBroker & operator=(const MemfdFdBroker &) = delete;

  std::string register_block(MemfdBlock * block);

private:
  struct FDDispatcher;

  static std::shared_ptr<FDDispatcher> get_dispatcher();
  static int create_fd_server_socket(const std::string & path);

  struct RegisteredBlock
  {
    int server_socket{-1};
    std::string socket_path;
  };

  std::shared_ptr<FDDispatcher> dispatcher_;
  std::unordered_map<std::uint32_t, RegisteredBlock> registered_blocks_;
  std::mutex mutex_;
};

/// One received and mapped physical block in the subscriber process.
class MemfdImportedBlock
{
public:
  MemfdImportedBlock(
    int memfd, void * mapping, std::size_t mapped_size,
    std::string socket_path);
  ~MemfdImportedBlock();

  MemfdImportedBlock(const MemfdImportedBlock &) = delete;
  MemfdImportedBlock & operator=(const MemfdImportedBlock &) = delete;

  MemfdControlHeader * control() const {return control_;}
  std::uint8_t * payload() const;
  std::size_t mapped_size() const {return mapped_size_;}

  void acquire_reader();
  void release_reader() noexcept;

private:
  int memfd_{-1};
  void * mapping_{nullptr};
  std::size_t mapped_size_{0};
  MemfdControlHeader * control_{nullptr};
  std::string socket_path_;
};

/// Process-local cache for imported mappings.  The cache key is the physical
/// block identity, not the publication UID.
class MemfdHandleCache
{
public:
  static std::shared_ptr<MemfdImportedBlock> import_block(
    const std::string & socket_path,
    std::int32_t pid,
    std::uint32_t block_id,
    std::uint64_t mapped_size,
    std::uint64_t payload_size,
    std::uint64_t expected_uid);

private:
  static int receive_fd_from_socket(const std::string & socket_path);
  static void validate(
    const MemfdImportedBlock & block,
    std::int32_t pid,
    std::uint32_t block_id,
    std::uint64_t mapped_size,
    std::uint64_t payload_size,
    std::uint64_t expected_uid);
};

}  // namespace memfd_buffer_backend

#endif  // MEMFD_BUFFER__MEMFD_BUFFER_IPC_MANAGER_HPP_
