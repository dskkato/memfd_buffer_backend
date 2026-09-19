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
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "shared_buffer/shared_buffer_api.hpp"
#include "shared_buffer/shared_buffer_c_api.h"
#include "shared_buffer/shared_buffer_impl.hpp"
#include "shared_buffer/shared_buffer_memory_pool.hpp"

namespace
{

TEST(SharedBufferTest, ReuseClaimSynchronizesReaderAcquisition)
{
  shared_buffer::SharedBufferControlHeader control;

  EXPECT_TRUE(shared_buffer::try_acquire_shared_buffer_reader(&control));
  EXPECT_FALSE(shared_buffer::try_claim_shared_buffer_reuse(&control));
  shared_buffer::release_shared_buffer_reader(&control);

  EXPECT_TRUE(shared_buffer::try_claim_shared_buffer_reuse(&control));
  EXPECT_EQ(
    shared_buffer::kSharedBufferReuseClaimed, control.reader_state.load(std::memory_order_acquire));
  EXPECT_FALSE(shared_buffer::try_acquire_shared_buffer_reader(&control));

  control.ipc_uid.store(42, std::memory_order_release);
  shared_buffer::release_shared_buffer_reuse_claim(&control);
  EXPECT_TRUE(shared_buffer::try_acquire_shared_buffer_reader(&control));
  EXPECT_EQ(1u, control.reader_state.load(std::memory_order_acquire));
  shared_buffer::release_shared_buffer_reader(&control);
  EXPECT_EQ(0u, control.reader_state.load(std::memory_order_acquire));
}

TEST(SharedBufferTest, ControlHeaderDescribesFixedMappingLayout)
{
  auto pool = std::make_shared<shared_buffer::SharedBufferMemoryPool>();
  auto * block = pool->allocate(32);
  ASSERT_NE(nullptr, block);
  ASSERT_NE(nullptr, block->control);

  EXPECT_EQ(0u, block->control->ipc_uid.load(std::memory_order_acquire));
  EXPECT_EQ(shared_buffer::kSharedBufferControlMagic, block->control->magic);
  EXPECT_EQ(shared_buffer::kSharedBufferControlAbiVersion, block->control->abi_version);
  EXPECT_EQ(32u, block->control->payload_size);
  EXPECT_EQ(shared_buffer::kSharedBufferPayloadOffset + 32u, block->mapped_size);
  EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(block->mapping) % 64u);
  EXPECT_EQ(
    0u,
    (reinterpret_cast<std::uintptr_t>(block->mapping) + shared_buffer::kSharedBufferPayloadOffset) %
      64u);

  pool->free(block);
}

TEST(SharedBufferTest, AllocateWriteReadAndCpuCopy)
{
  auto buffer = shared_buffer::allocate_buffer(64);
  {
    auto write = shared_buffer::from_output_buffer(buffer);
    ASSERT_NE(nullptr, write.get_ptr());
    for (std::size_t i = 0; i < buffer.size(); ++i) {
      write.get_ptr()[i] = static_cast<std::uint8_t>(i + 7);
    }
  }

  const auto & const_buffer = buffer;
  auto read = shared_buffer::from_input_buffer(const_buffer);
  ASSERT_NE(nullptr, read.get_ptr());
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    EXPECT_EQ(static_cast<std::uint8_t>(i + 7), read.get_ptr()[i]);
  }
  auto cpu = buffer.to_vector();
  ASSERT_EQ(64u, cpu.size());
  EXPECT_EQ(7u, cpu.front());
  EXPECT_EQ(70u, cpu.back());
}

TEST(SharedBufferTest, PromotesCpuInputBuffer)
{
  rosidl::Buffer<std::uint8_t> cpu(8);
  for (std::size_t i = 0; i < cpu.size(); ++i) {
    cpu[i] = static_cast<std::uint8_t>(0xA0 + i);
  }
  const auto & const_cpu = cpu;
  auto read = shared_buffer::from_input_buffer(const_cpu);
  ASSERT_NE(nullptr, read.get_promoted_buffer());
  EXPECT_EQ("shared_buffer", read.get_promoted_buffer()->get_backend_type());
  EXPECT_EQ(0xA0, read.get_ptr()[0]);
  EXPECT_EQ(0xA7, read.get_ptr()[7]);
}

TEST(SharedBufferTest, CApiProvidesScopedSharedBufferAccess)
{
  shared_buffer_rust_buffer_t * buffer = nullptr;
  ASSERT_EQ(
    SHARED_BUFFER_RUST_OK,
    shared_buffer_rust_buffer_new(16, &buffer));
  ASSERT_NE(nullptr, buffer);
  EXPECT_EQ(16u, shared_buffer_rust_buffer_size(buffer));

  shared_buffer_rust_write_access_t * write = nullptr;
  ASSERT_EQ(
    SHARED_BUFFER_RUST_OK,
    shared_buffer_rust_write_access_new(buffer, &write));
  ASSERT_NE(nullptr, write);
  ASSERT_EQ(16u, shared_buffer_rust_write_access_size(write));
  for (std::size_t i = 0; i < 16; ++i) {
    shared_buffer_rust_write_access_data(write)[i] = static_cast<std::uint8_t>(i + 1);
  }
  shared_buffer_rust_write_access_destroy(write);

  shared_buffer_rust_read_access_t * read = nullptr;
  ASSERT_EQ(
    SHARED_BUFFER_RUST_OK,
    shared_buffer_rust_read_access_new(buffer, &read));
  ASSERT_NE(nullptr, read);
  EXPECT_EQ(1u, shared_buffer_rust_read_access_data(read)[0]);
  EXPECT_EQ(16u, shared_buffer_rust_read_access_data(read)[15]);
  shared_buffer_rust_read_access_destroy(read);
  shared_buffer_rust_buffer_destroy(buffer);
}

TEST(SharedBufferTest, RejectsNonSharedBufferOutputBuffer)
{
  rosidl::Buffer<std::uint8_t> cpu(8);
  EXPECT_THROW(shared_buffer::from_output_buffer(cpu), shared_buffer::SharedBufferError);
}

TEST(SharedBufferTest, RejectsConcurrentAndFinalizedWriters)
{
  auto buffer = shared_buffer::allocate_buffer(8);
  auto first = shared_buffer::from_output_buffer(buffer);
  EXPECT_THROW(shared_buffer::from_output_buffer(buffer), std::runtime_error);
  first = shared_buffer::WriteHandle();
  EXPECT_THROW(shared_buffer::from_output_buffer(buffer), std::runtime_error);
}

TEST(SharedBufferTest, ReadHandleDoesNotImplicitlyFinalizeWriter)
{
  auto buffer = shared_buffer::allocate_buffer(8);
  auto write = shared_buffer::from_output_buffer(buffer);
  const auto & const_buffer = buffer;

  EXPECT_THROW(shared_buffer::from_input_buffer(const_buffer), std::runtime_error);
}

TEST(SharedBufferTest, WriterAndReaderAccessAreMutuallyExclusive)
{
  auto buffer = shared_buffer::allocate_buffer(8);
  const auto & const_buffer = buffer;
  auto read = shared_buffer::from_input_buffer(const_buffer);

  EXPECT_THROW(shared_buffer::from_output_buffer(buffer), std::runtime_error);

  read = shared_buffer::ReadHandle();
  EXPECT_NO_THROW(shared_buffer::from_output_buffer(buffer));
}

TEST(SharedBufferTest, MultipleReadHandlesAreAllowed)
{
  auto buffer = shared_buffer::allocate_buffer(8);
  const auto & const_buffer = buffer;
  auto first = shared_buffer::from_input_buffer(const_buffer);
  auto second = shared_buffer::from_input_buffer(const_buffer);

  EXPECT_THROW(shared_buffer::from_output_buffer(buffer), std::runtime_error);

  first = shared_buffer::ReadHandle();
  EXPECT_THROW(shared_buffer::from_output_buffer(buffer), std::runtime_error);

  second = shared_buffer::ReadHandle();
  EXPECT_NO_THROW(shared_buffer::from_output_buffer(buffer));
}

TEST(SharedBufferTest, ExplicitFinalizeAllowsReadWhileWriteHandleLives)
{
  auto buffer = shared_buffer::allocate_buffer(8);
  auto write = shared_buffer::from_output_buffer(buffer);
  write.get_ptr()[0] = 0x5A;

  auto * impl =
    dynamic_cast<shared_buffer::SharedBufferImpl<std::uint8_t> *>(buffer.get_impl());
  ASSERT_NE(nullptr, impl);
  impl->get_shared_buffer().finalize_write_handle();

  const auto & const_buffer = buffer;
  auto read = shared_buffer::from_input_buffer(const_buffer);
  EXPECT_EQ(0x5A, read.get_ptr()[0]);
}

TEST(SharedBufferTest, RejectsWritersForReadOnlyBuffer)
{
  std::uint8_t payload[8]{};
  shared_buffer::SharedBuffer buffer(
    payload, sizeof(payload), [](std::uint8_t *) {}, nullptr, nullptr, 0, 0, false);

  EXPECT_FALSE(buffer.writable());
  EXPECT_THROW(buffer.get_write_handle(), std::runtime_error);
}

TEST(SharedBufferTest, PoolUsesSizeBucketsAndReaderProtection)
{
  auto pool = std::make_shared<shared_buffer::SharedBufferMemoryPool>();
  auto * first = pool->allocate(32);
  auto * different = pool->allocate(64);
  ASSERT_NE(first, different);
  pool->free(first);
  pool->free(different);

  auto * reused = pool->allocate(32);
  EXPECT_EQ(first, reused);
  pool->free(reused);
}

#ifdef _WIN32
TEST(SharedBufferTest, NamedMappingIsRemovedAfterLastHandleCloses)
{
  std::string mapping_name;
  {
    auto pool = std::make_shared<shared_buffer::SharedBufferMemoryPool>();
    auto * block = pool->allocate(32);
    ASSERT_NE(nullptr, block);
    ASSERT_FALSE(block->socket_path.empty());
    mapping_name = block->socket_path;
    const std::wstring wide_name(mapping_name.begin(), mapping_name.end());
    HANDLE probe = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide_name.c_str());
    ASSERT_NE(nullptr, probe);
    EXPECT_NE(FALSE, CloseHandle(probe));
  }

  const std::wstring wide_name(mapping_name.begin(), mapping_name.end());
  HANDLE stale = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide_name.c_str());
  EXPECT_EQ(nullptr, stale);
  if (stale != nullptr) {
    (void)CloseHandle(stale);
  }
}
#endif

}  // namespace
