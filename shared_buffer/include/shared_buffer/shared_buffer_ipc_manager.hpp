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

#ifndef SHARED_BUFFER__SHARED_BUFFER_IPC_MANAGER_HPP_
#define SHARED_BUFFER__SHARED_BUFFER_IPC_MANAGER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "shared_buffer/shared_buffer_memory_pool.hpp"
#include "shared_buffer/visibility_control.h"

namespace shared_buffer
{

/// Reusable publisher-side IPC broker.
///
/// Linux publishes an fd through a Unix-domain socket. Windows publishes the
/// name of the file-mapping object. The implementation is selected by CMake;
/// callers do not need to know which transport is active.
class SHARED_BUFFER_PUBLIC SharedBufferFdBroker
{
public:
  SharedBufferFdBroker();
  ~SharedBufferFdBroker();

  SharedBufferFdBroker(const SharedBufferFdBroker &) = delete;
  SharedBufferFdBroker & operator=(const SharedBufferFdBroker &) = delete;

  std::string register_block(SharedBufferBlock * block);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// One received and mapped physical block in the subscriber process.
class SHARED_BUFFER_PUBLIC SharedBufferImportedBlock
{
public:
  SharedBufferImportedBlock(
    std::intptr_t native_handle, void * mapping, std::size_t mapped_size, std::string ipc_name);
  ~SharedBufferImportedBlock();

  SharedBufferImportedBlock(const SharedBufferImportedBlock &) = delete;
  SharedBufferImportedBlock & operator=(const SharedBufferImportedBlock &) = delete;

  SharedBufferControlHeader * control() const {return control_;}
  std::uint8_t * payload() const;
  std::size_t mapped_size() const {return mapped_size_;}

  void acquire_reader();
  void release_reader() noexcept;

private:
  std::intptr_t native_handle_{-1};
  void * mapping_{nullptr};
  std::size_t mapped_size_{0};
  SharedBufferControlHeader * control_{nullptr};
  std::string ipc_name_;
};

/// Process-local cache for imported Linux shared-memory or Windows named mappings.
/// The cache key is the physical block identity, not the publication UID.
class SHARED_BUFFER_PUBLIC SharedBufferHandleCache
{
public:
  static std::shared_ptr<SharedBufferImportedBlock> import_block(
    const std::string & ipc_name, std::int32_t pid, std::uint32_t block_id,
    std::uint64_t mapped_size, std::uint64_t payload_size, std::uint64_t expected_uid);

private:
  static void validate(
    const SharedBufferImportedBlock & block, std::uint64_t mapped_size, std::uint64_t payload_size,
    std::uint64_t expected_uid);
};

}  // namespace shared_buffer

#endif  // SHARED_BUFFER__SHARED_BUFFER_IPC_MANAGER_HPP_
