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

#ifndef MEMFD_BUFFER__MEMFD_MEMORY_POOL_HPP_
#define MEMFD_BUFFER__MEMFD_MEMORY_POOL_HPP_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace memfd_buffer_backend
{

inline constexpr std::uint64_t kMemfdGracePeriodUs = 100000;
inline constexpr std::uint32_t kMemfdReuseClaimed = (std::uint32_t{1} << 31);
inline constexpr std::uint32_t kMemfdReaderCountMask = ~kMemfdReuseClaimed;

/// Metadata at the beginning of every mapped memfd region.
///
/// The atomic members are deliberately lock-free Linux atomics.  The header
/// is shared between processes and must not contain a process-local mutex.
struct MemfdControlHeader
{
  // Bit 31 claims the block for reuse; bits 0-30 count active readers.
  // Reader acquisition and reuse claiming both use CAS.
  // This prevents a reader from appearing after the publisher
  // observes zero readers but before it starts reusing the block.
  std::atomic<std::uint32_t> reader_state{0};
  std::atomic<std::uint64_t> ipc_uid{0};
  std::atomic<std::uint64_t> publish_timestamp_us{0};

  MemfdControlHeader() = default;
  MemfdControlHeader(const MemfdControlHeader &) = delete;
  MemfdControlHeader & operator=(const MemfdControlHeader &) = delete;
};

inline bool try_acquire_memfd_reader(MemfdControlHeader * control)
{
  if (control == nullptr) {
    return false;
  }

  auto value = control->reader_state.load(std::memory_order_acquire);
  for (;;) {
    if (
      (value & kMemfdReuseClaimed) != 0 ||
      (value & kMemfdReaderCountMask) == kMemfdReaderCountMask) {
      return false;
    }
    if (control->reader_state.compare_exchange_weak(
          value, value + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
      return true;
    }
  }
}

inline bool try_claim_memfd_reuse(MemfdControlHeader * control)
{
  if (control == nullptr) {
    return false;
  }
  std::uint32_t expected = 0;
  return control->reader_state.compare_exchange_strong(
    expected, kMemfdReuseClaimed, std::memory_order_acq_rel, std::memory_order_acquire);
}

inline void release_memfd_reader(MemfdControlHeader * control) noexcept
{
  if (control != nullptr) {
    control->reader_state.fetch_sub(1, std::memory_order_release);
  }
}

inline void release_memfd_reuse_claim(MemfdControlHeader * control) noexcept
{
  if (control != nullptr) {
    control->reader_state.store(0, std::memory_order_release);
  }
}

static_assert(sizeof(MemfdControlHeader) == 24, "memfd control metadata must remain compact");
static_assert(
  std::atomic<std::uint64_t>::is_always_lock_free,
  "memfd control metadata requires lock-free uint64 atomics");
static_assert(
  std::atomic<std::uint32_t>::is_always_lock_free,
  "memfd control metadata requires lock-free uint32 atomics");

/// Publisher-side allocation and its stable physical identity.
struct MemfdBlock
{
  int memfd{-1};
  void * mapping{nullptr};
  std::size_t mapped_size{0};
  std::size_t payload_size{0};
  std::uint32_t block_id{0};
  MemfdControlHeader * control{nullptr};
  std::uint64_t current_uid{0};
  std::string socket_path;
  bool in_use{false};
};

class MemfdFdBroker;

/// Publisher-side size-aware pool for anonymous memfd blocks.
class MemfdMemoryPool : public std::enable_shared_from_this<MemfdMemoryPool>
{
public:
  MemfdMemoryPool();
  ~MemfdMemoryPool();

  MemfdMemoryPool(const MemfdMemoryPool &) = delete;
  MemfdMemoryPool & operator=(const MemfdMemoryPool &) = delete;
  MemfdMemoryPool(MemfdMemoryPool &&) = delete;
  MemfdMemoryPool & operator=(MemfdMemoryPool &&) = delete;

  /// Allocate an exact-size payload block, reusing only a safe free block.
  /// Throws std::runtime_error when memfd allocation or mapping fails.
  MemfdBlock * allocate(std::size_t payload_size);

  /// Return a publisher-side buffer block to its size bucket.
  void free(MemfdBlock * block);

  /// Create a deleter suitable for MemfdBuffer's publisher-side ownership.
  std::function<void(std::uint8_t *)> deleter(MemfdBlock * block);

  /// Assign the UID for the current generation of a reserved block.
  std::uint64_t assign_uid(MemfdBlock * block);

  /// Record that a descriptor for the current generation was created.
  void mark_published(MemfdBlock * block);

  /// Register the block's memfd with the reusable FD broker.
  std::string register_block_for_ipc(MemfdBlock * block);

  /// Find the block containing a publisher-side payload pointer.
  MemfdBlock * find_block_for_ptr(const void * ptr) const;

  bool is_ipc_capable() const { return ipc_capable_; }

private:
  MemfdBlock * create_block(std::size_t payload_size);
  bool try_claim_block(MemfdBlock * block) const;

  bool initialized_{false};
  bool ipc_capable_{false};
  std::uint32_t next_block_id_{0};
  std::mt19937_64 uid_rng_{std::random_device{}()};
  std::uniform_int_distribution<std::uint64_t> uid_dist_{1, UINT64_MAX};

  std::map<std::size_t, std::vector<MemfdBlock *>> free_blocks_;
  std::vector<std::unique_ptr<MemfdBlock>> all_blocks_;
  std::unique_ptr<MemfdFdBroker> broker_;
  mutable std::mutex mutex_;
};

}  // namespace memfd_buffer_backend

#endif  // MEMFD_BUFFER__MEMFD_MEMORY_POOL_HPP_
