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

#ifndef MEMFD_BUFFER__MEMFD_BUFFER_IPC_MANAGER_HPP_
#define MEMFD_BUFFER__MEMFD_BUFFER_IPC_MANAGER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "memfd_buffer/memfd_memory_pool.hpp"
#include "memfd_buffer/visibility_control.h"

namespace memfd_buffer_backend
{

/// Reusable publisher-side IPC broker.
///
/// Linux publishes an fd through a Unix-domain socket. Windows publishes the
/// name of the file-mapping object. The implementation is selected by CMake;
/// callers do not need to know which transport is active.
class MEMFD_BUFFER_PUBLIC MemfdFdBroker
{
public:
  MemfdFdBroker();
  ~MemfdFdBroker();

  MemfdFdBroker(const MemfdFdBroker &) = delete;
  MemfdFdBroker & operator=(const MemfdFdBroker &) = delete;

  std::string register_block(MemfdBlock * block);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// One received and mapped physical block in the subscriber process.
class MEMFD_BUFFER_PUBLIC MemfdImportedBlock
{
public:
  MemfdImportedBlock(
    std::intptr_t native_handle, void * mapping, std::size_t mapped_size, std::string ipc_name);
  ~MemfdImportedBlock();

  MemfdImportedBlock(const MemfdImportedBlock &) = delete;
  MemfdImportedBlock & operator=(const MemfdImportedBlock &) = delete;

  MemfdControlHeader * control() const {return control_;}
  std::uint8_t * payload() const;
  std::size_t mapped_size() const {return mapped_size_;}

  void acquire_reader();
  void release_reader() noexcept;

private:
  std::intptr_t native_handle_{-1};
  void * mapping_{nullptr};
  std::size_t mapped_size_{0};
  MemfdControlHeader * control_{nullptr};
  std::string ipc_name_;
};

/// Process-local cache for imported Linux memfd or Windows named mappings.
/// The cache key is the physical block identity, not the publication UID.
class MEMFD_BUFFER_PUBLIC MemfdHandleCache
{
public:
  static std::shared_ptr<MemfdImportedBlock> import_block(
    const std::string & ipc_name, std::int32_t pid, std::uint32_t block_id,
    std::uint64_t mapped_size, std::uint64_t payload_size, std::uint64_t expected_uid);

private:
  static void validate(
    const MemfdImportedBlock & block, std::uint64_t mapped_size, std::uint64_t payload_size,
    std::uint64_t expected_uid);
};

}  // namespace memfd_buffer_backend

#endif  // MEMFD_BUFFER__MEMFD_BUFFER_IPC_MANAGER_HPP_
