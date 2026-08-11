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

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "memfd_buffer/memfd_buffer_api.hpp"
#include "memfd_buffer/memfd_buffer_impl.hpp"
#include "memfd_buffer_backend/memfd_buffer_backend.hpp"
#include "memfd_buffer_backend_msgs/msg/memfd_buffer_descriptor.hpp"

namespace
{

rmw_topic_endpoint_info_t endpoint()
{
  rmw_topic_endpoint_info_t result{};
  result.topic_type = "test_msgs/msg/Bytes";
  return result;
}

TEST(MemfdBufferBackendTest, MetadataRequiresSameHostAndEuid)
{
  memfd_buffer_backend::MemfdBufferBackend backend;
  const auto info = endpoint();
  std::unordered_map<std::string, std::string> supported;
  supported["memfd"] = backend.get_backend_metadata();
  EXPECT_TRUE(backend.on_discovering_endpoint(info, {}, supported).first);

  supported["memfd"] = "different-host:4294967295";
  EXPECT_FALSE(backend.on_discovering_endpoint(info, {}, supported).first);
}

TEST(MemfdBufferBackendTest, DescriptorRoundTripUsesBrokerAndMappingCache)
{
  memfd_buffer_backend::MemfdBufferBackend backend;
  const auto info = endpoint();
  std::unordered_map<std::string, std::string> supported;
  supported["memfd"] = backend.get_backend_metadata();
  ASSERT_TRUE(backend.on_discovering_endpoint(info, {}, supported).first);

  auto buffer = memfd_buffer_backend::allocate_buffer(128);
  {
    auto write = memfd_buffer_backend::from_output_buffer(buffer);
    for (std::size_t i = 0; i < buffer.size(); ++i) {
      write.get_ptr()[i] = static_cast<std::uint8_t>(i ^ 0x5A);
    }
  }

  auto descriptor = backend.create_descriptor_with_endpoint(buffer.get_impl(), info);
  ASSERT_NE(nullptr, descriptor);
  const auto * typed_descriptor = static_cast<
    const memfd_buffer_backend_msgs::msg::MemfdBufferDescriptor *>(descriptor.get());
  ASSERT_EQ(128u, typed_descriptor->size);
  ASSERT_NE(0u, typed_descriptor->ipc_uid);

  auto imported = backend.from_descriptor_with_endpoint(descriptor.get(), info);
  ASSERT_NE(nullptr, imported.get());
  auto * imported_base = static_cast<rosidl::BufferImplBase<std::uint8_t> *>(imported.get());
  auto * imported_impl = dynamic_cast<memfd_buffer_backend::MemfdBufferImpl<std::uint8_t> *>(
    imported_base);
  ASSERT_NE(nullptr, imported_impl);
  auto first_copy = imported_impl->to_cpu();
  auto * first_cpu = dynamic_cast<rosidl::CpuBufferImpl<std::uint8_t> *>(first_copy.get());
  ASSERT_NE(nullptr, first_cpu);
  EXPECT_EQ(0x5A, first_cpu->get_storage()[0]);
  EXPECT_EQ(static_cast<std::uint8_t>(127 ^ 0x5A), first_cpu->get_storage()[127]);

  // A second import has the same physical key and therefore reuses the cache
  // entry without asking the broker for another descriptor mapping.
  auto imported_again = backend.from_descriptor_with_endpoint(descriptor.get(), info);
  ASSERT_NE(nullptr, imported_again.get());
  auto * imported_again_base = static_cast<rosidl::BufferImplBase<std::uint8_t> *>(
    imported_again.get());
  auto * imported_again_impl = dynamic_cast<memfd_buffer_backend::MemfdBufferImpl<std::uint8_t> *>(
    imported_again_base);
  ASSERT_NE(nullptr, imported_again_impl);
  auto second_copy = imported_again_impl->to_cpu();
  auto * second_cpu = dynamic_cast<rosidl::CpuBufferImpl<std::uint8_t> *>(second_copy.get());
  ASSERT_NE(nullptr, second_cpu);
  EXPECT_EQ(first_cpu->get_storage(), second_cpu->get_storage());
}

TEST(MemfdBufferBackendTest, IncompatiblePeerFallsBack)
{
  memfd_buffer_backend::MemfdBufferBackend backend;
  const auto info = endpoint();
  std::unordered_map<std::string, std::string> supported;
  supported["memfd"] = "other-host:1000";
  EXPECT_FALSE(backend.on_discovering_endpoint(info, {}, supported).first);

  auto buffer = memfd_buffer_backend::allocate_buffer(8);
  EXPECT_EQ(nullptr, backend.create_descriptor_with_endpoint(buffer.get_impl(), info));
}

}  // namespace
