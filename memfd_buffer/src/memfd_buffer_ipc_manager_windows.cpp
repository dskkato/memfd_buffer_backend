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

#include "memfd_buffer/memfd_buffer_ipc_manager.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace memfd_buffer_backend
{

namespace
{

std::wstring widen_ascii(const std::string & value)
{
  return std::wstring(value.begin(), value.end());
}

struct CacheKey
{
  std::int32_t pid;
  std::uint32_t block_id;
  std::string mapping_name;

  bool operator==(const CacheKey & other) const
  {
    return
      pid == other.pid && block_id == other.block_id && mapping_name == other.mapping_name;
  }
};

struct CacheKeyHash
{
  std::size_t operator()(const CacheKey & key) const
  {
    std::size_t result = std::hash<std::int32_t>{}(key.pid);
    result ^= std::hash<std::uint32_t>{}(key.block_id) + 0x9e3779b9 +
      (result << 6) + (result >> 2);
    result ^= std::hash<std::string>{}(key.mapping_name) + 0x9e3779b9 +
      (result << 6) + (result >> 2);
    return result;
  }
};

using CacheMap = std::unordered_map<CacheKey, std::shared_ptr<MemfdImportedBlock>, CacheKeyHash>;

CacheMap & cache()
{
  static auto * value = new CacheMap();
  return *value;
}

std::mutex & cache_mutex()
{
  static auto * value = new std::mutex();
  return *value;
}

void validate_mapping(
  const MemfdImportedBlock & block, std::uint64_t mapped_size, std::uint64_t payload_size,
  std::uint64_t expected_uid)
{
  const MemfdControlHeader * control = block.control();
  if (
    control == nullptr || mapped_size != block.mapped_size() || mapped_size < kMemfdPayloadOffset ||
    payload_size > mapped_size - kMemfdPayloadOffset) {
    throw std::runtime_error("invalid shared memory mapping size");
  }
  if (control->magic != kMemfdControlMagic || control->abi_version != kMemfdControlAbiVersion) {
    throw std::runtime_error("shared memory control header ABI mismatch");
  }
  if (control->payload_size != payload_size) {
    throw std::runtime_error("shared memory control header payload size mismatch");
  }
  if (expected_uid == 0) {
    throw std::runtime_error("shared memory descriptor contains a zero publication UID");
  }
  if (control->ipc_uid.load(std::memory_order_acquire) != expected_uid) {
    throw std::runtime_error("stale shared memory descriptor publication UID");
  }
}

}  // namespace

MemfdImportedBlock::MemfdImportedBlock(
  void * mapping_handle, void * mapping, std::size_t mapped_size, std::string socket_path)
: memfd_(mapping_handle),
  mapping_(mapping),
  mapped_size_(mapped_size),
  control_(static_cast<MemfdControlHeader *>(mapping)),
  socket_path_(std::move(socket_path))
{
}

MemfdImportedBlock::~MemfdImportedBlock()
{
  if (mapping_ != nullptr) {
    (void)UnmapViewOfFile(mapping_);
  }
  if (memfd_ != nullptr) {
    (void)CloseHandle(static_cast<HANDLE>(memfd_));
  }
}

std::uint8_t * MemfdImportedBlock::payload() const
{
  return reinterpret_cast<std::uint8_t *>(mapping_) + kMemfdPayloadOffset;
}

void MemfdImportedBlock::acquire_reader()
{
  if (control_ == nullptr) {
    throw std::runtime_error("cannot acquire a reader for unmapped shared memory");
  }
  if (!try_acquire_memfd_reader(control_)) {
    throw std::runtime_error("shared memory block is being reused");
  }
}

void MemfdImportedBlock::release_reader() noexcept
{
  if (control_ != nullptr) {
    release_memfd_reader(control_);
  }
}

void MemfdHandleCache::validate(
  const MemfdImportedBlock & block, std::uint64_t mapped_size, std::uint64_t payload_size,
  std::uint64_t expected_uid)
{
  validate_mapping(block, mapped_size, payload_size, expected_uid);
}

std::shared_ptr<MemfdImportedBlock> MemfdHandleCache::import_block(
  const std::string & mapping_name, std::int32_t pid, std::uint32_t block_id,
  std::uint64_t mapped_size, std::uint64_t payload_size, std::uint64_t expected_uid)
{
  if (
    mapping_name.empty() || mapped_size < kMemfdPayloadOffset ||
    mapped_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("invalid shared memory descriptor metadata");
  }

  std::lock_guard<std::mutex> lock(cache_mutex());
  const CacheKey key{pid, block_id, mapping_name};
  auto existing = cache().find(key);
  if (existing != cache().end()) {
    validate(*existing->second, mapped_size, payload_size, expected_uid);
    return existing->second;
  }

  const auto wide_name = widen_ascii(mapping_name);
  HANDLE mapping_handle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide_name.c_str());
  if (mapping_handle == nullptr) {
    throw std::runtime_error(
            "OpenFileMappingW failed: Windows error " + std::to_string(GetLastError()));
  }
  void * mapping = MapViewOfFile(
    mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<std::size_t>(mapped_size));
  if (mapping == nullptr) {
    const DWORD error = GetLastError();
    (void)CloseHandle(mapping_handle);
    throw std::runtime_error("MapViewOfFile failed: Windows error " + std::to_string(error));
  }

  auto imported = std::make_shared<MemfdImportedBlock>(
    mapping_handle, mapping, static_cast<std::size_t>(mapped_size), mapping_name);
  validate(*imported, mapped_size, payload_size, expected_uid);
  cache().emplace(key, imported);
  return imported;
}

}  // namespace memfd_buffer_backend
