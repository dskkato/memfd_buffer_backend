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

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "memfd_buffer/memfd_buffer_api.hpp"
#include "memfd_buffer/memfd_buffer_impl.hpp"
#include "memfd_buffer/memfd_memory_pool.hpp"

namespace
{

TEST(MemfdBufferTest, ReuseClaimSynchronizesReaderAcquisition)
{
  memfd_buffer_backend::MemfdControlHeader control;

  EXPECT_TRUE(memfd_buffer_backend::try_acquire_memfd_reader(&control));
  EXPECT_FALSE(memfd_buffer_backend::try_claim_memfd_reuse(&control));
  memfd_buffer_backend::release_memfd_reader(&control);

  EXPECT_TRUE(memfd_buffer_backend::try_claim_memfd_reuse(&control));
  EXPECT_EQ(
    memfd_buffer_backend::kMemfdReuseClaimed, control.reader_state.load(std::memory_order_acquire));
  EXPECT_FALSE(memfd_buffer_backend::try_acquire_memfd_reader(&control));

  control.ipc_uid.store(42, std::memory_order_release);
  memfd_buffer_backend::release_memfd_reuse_claim(&control);
  EXPECT_TRUE(memfd_buffer_backend::try_acquire_memfd_reader(&control));
  EXPECT_EQ(1u, control.reader_state.load(std::memory_order_acquire));
  memfd_buffer_backend::release_memfd_reader(&control);
  EXPECT_EQ(0u, control.reader_state.load(std::memory_order_acquire));
}

TEST(MemfdBufferTest, AllocateWriteReadAndCpuCopy)
{
  auto buffer = memfd_buffer_backend::allocate_buffer(64);
  {
    auto write = memfd_buffer_backend::from_output_buffer(buffer);
    ASSERT_NE(nullptr, write.get_ptr());
    for (std::size_t i = 0; i < buffer.size(); ++i) {
      write.get_ptr()[i] = static_cast<std::uint8_t>(i + 7);
    }
  }

  const auto & const_buffer = buffer;
  auto read = memfd_buffer_backend::from_input_buffer(const_buffer);
  ASSERT_NE(nullptr, read.get_ptr());
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    EXPECT_EQ(static_cast<std::uint8_t>(i + 7), read.get_ptr()[i]);
  }
  auto cpu = buffer.to_vector();
  ASSERT_EQ(64u, cpu.size());
  EXPECT_EQ(7u, cpu.front());
  EXPECT_EQ(70u, cpu.back());
}

TEST(MemfdBufferTest, PromotesCpuBuffer)
{
  rosidl::Buffer<std::uint8_t> cpu(8);
  for (std::size_t i = 0; i < cpu.size(); ++i) {
    cpu[i] = static_cast<std::uint8_t>(0xA0 + i);
  }
  const auto & const_cpu = cpu;
  auto read = memfd_buffer_backend::from_input_buffer(const_cpu);
  ASSERT_NE(nullptr, read.get_promoted_buffer());
  EXPECT_EQ("memfd", read.get_promoted_buffer()->get_backend_type());
  EXPECT_EQ(0xA0, read.get_ptr()[0]);
  EXPECT_EQ(0xA7, read.get_ptr()[7]);

  rosidl::Buffer<std::uint8_t> output(8);
  auto write = memfd_buffer_backend::from_output_buffer(output);
  ASSERT_NE(nullptr, write.get_promoted_buffer());
  ASSERT_NE(nullptr, write.get_ptr());
  write.get_ptr()[0] = 0x5A;
  EXPECT_EQ(8u, write.get_promoted_buffer()->size());
}

TEST(MemfdBufferTest, RejectsConcurrentAndFinalizedWriters)
{
  auto buffer = memfd_buffer_backend::allocate_buffer(8);
  auto first = memfd_buffer_backend::from_output_buffer(buffer);
  EXPECT_THROW(memfd_buffer_backend::from_output_buffer(buffer), std::runtime_error);
  first = memfd_buffer_backend::WriteHandle();
  EXPECT_THROW(memfd_buffer_backend::from_output_buffer(buffer), std::runtime_error);
}

TEST(MemfdBufferTest, PoolUsesSizeBucketsAndReaderProtection)
{
  auto pool = std::make_shared<memfd_buffer_backend::MemfdMemoryPool>();
  auto * first = pool->allocate(32);
  auto * different = pool->allocate(64);
  ASSERT_NE(first, different);
  pool->free(first);
  pool->free(different);

  auto * reused = pool->allocate(32);
  EXPECT_EQ(first, reused);
  pool->free(reused);
}

}  // namespace
