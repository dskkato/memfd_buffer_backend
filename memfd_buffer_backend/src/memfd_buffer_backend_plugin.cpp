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

#include <unistd.h>

#include <atomic>
#include <rosidl_buffer/buffer_impl_base.hpp>
#include <stdexcept>
#include <typeinfo>

#include "memfd_buffer/memfd_buffer.hpp"
#include "memfd_buffer/memfd_buffer_impl.hpp"
#include "memfd_buffer/memfd_buffer_ipc_manager.hpp"
#include "memfd_buffer/memfd_memory_pool.hpp"
#include "memfd_buffer_backend/memfd_buffer_backend.hpp"
#include "memfd_buffer_backend_msgs/msg/memfd_buffer_descriptor.hpp"
#include "pluginlib/class_list_macros.hpp"
#include "rosidl_typesupport_cpp/message_type_support.hpp"

namespace memfd_buffer_backend
{

MemfdBufferBackend::MemfdBufferBackend() = default;

std::shared_ptr<HostEndpointManager> MemfdBufferBackend::get_endpoint_manager() const
{
  std::lock_guard<std::mutex> lock(manager_mutex_);
  if (endpoint_manager_ == nullptr) {
    endpoint_manager_ = std::make_shared<HostEndpointManager>();
  }
  return endpoint_manager_;
}

std::string MemfdBufferBackend::get_backend_metadata() const
{
  return get_endpoint_manager()->metadata();
}

const rosidl_message_type_support_t * MemfdBufferBackend::get_descriptor_type_support() const
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<
    memfd_buffer_backend_msgs::msg::MemfdBufferDescriptor>();
}

std::shared_ptr<void> MemfdBufferBackend::create_empty_descriptor() const
{
  return std::make_shared<memfd_buffer_backend_msgs::msg::MemfdBufferDescriptor>();
}

void MemfdBufferBackend::on_creating_endpoint(const rmw_topic_endpoint_info_t & endpoint_info) const
{
  (void)endpoint_info;
}

std::pair<bool, std::vector<std::set<std::uint32_t>>> MemfdBufferBackend::on_discovering_endpoint(
  const rmw_topic_endpoint_info_t & endpoint_info,
  const std::vector<rmw_topic_endpoint_info_t> & existing_endpoints,
  const std::unordered_map<std::string, std::string> & endpoint_supported_backends)
{
  (void)existing_endpoints;

  const auto it = endpoint_supported_backends.find("memfd");
  const bool is_compatible =
    it != endpoint_supported_backends.end() && it->second == get_backend_metadata();
  {
    std::lock_guard<std::mutex> lock(compatibility_mutex_);
    compatibility_cache_[GidKey(endpoint_info.endpoint_gid)] = is_compatible;
  }
  return {is_compatible, {}};
}

std::shared_ptr<void> MemfdBufferBackend::create_descriptor_with_endpoint(
  const void * impl, const rmw_topic_endpoint_info_t & endpoint_info) const
{
  {
    std::lock_guard<std::mutex> lock(compatibility_mutex_);
    const auto it = compatibility_cache_.find(GidKey(endpoint_info.endpoint_gid));
    if (it != compatibility_cache_.end() && !it->second) {
      return nullptr;
    }
  }
  if (impl == nullptr) {
    return nullptr;
  }
  const auto * base = static_cast<const rosidl::BufferImplBase<std::uint8_t> *>(impl);
  const auto * memfd_impl = dynamic_cast<const MemfdBufferImpl<std::uint8_t> *>(base);
  if (memfd_impl == nullptr) {
    return nullptr;
  }

  try {
    const auto pool = get_global_memfd_pool();
    auto & memfd_buffer = memfd_impl->get_memfd_buffer();
    memfd_buffer.finalize_write_handle();
    MemfdBlock * block = pool->find_block_for_ptr(memfd_buffer.get_ptr());
    if (block == nullptr || block->control == nullptr) {
      return nullptr;
    }
    const std::string socket_path = pool->register_block_for_ipc(block);
    const std::uint64_t uid = pool->assign_uid(block);
    if (socket_path.empty() || uid == 0) {
      return nullptr;
    }

    auto descriptor = std::make_shared<memfd_buffer_backend_msgs::msg::MemfdBufferDescriptor>();
    descriptor->size = memfd_impl->size();
    descriptor->element_type_name = typeid(std::uint8_t).name();
    descriptor->memfd_pid = static_cast<std::int32_t>(getpid());
    descriptor->memfd_block_id = block->block_id;
    descriptor->memfd_block_size = block->mapped_size;
    descriptor->memfd_socket_path = socket_path;
    descriptor->ipc_uid = uid;
    pool->mark_published(block);
    return descriptor;
  } catch (const std::exception &) {
    return nullptr;
  }
}

std::unique_ptr<void, void (*)(void *)> MemfdBufferBackend::from_descriptor_with_endpoint(
  const void * descriptor_ptr, const rmw_topic_endpoint_info_t & endpoint_info) const
{
  (void)endpoint_info;
  if (descriptor_ptr == nullptr) {
    throw std::runtime_error("null memfd descriptor");
  }
  const auto & descriptor =
    *static_cast<const memfd_buffer_backend_msgs::msg::MemfdBufferDescriptor *>(descriptor_ptr);
  if (descriptor.element_type_name != typeid(std::uint8_t).name()) {
    throw std::runtime_error("memfd descriptor element type mismatch");
  }
  if (
    descriptor.memfd_pid <= 0 || descriptor.memfd_socket_path.empty() || descriptor.ipc_uid == 0 ||
    descriptor.memfd_block_size < kMemfdPayloadOffset ||
    descriptor.size > descriptor.memfd_block_size - kMemfdPayloadOffset) {
    throw std::runtime_error("invalid memfd descriptor metadata");
  }

  auto imported = MemfdHandleCache::import_block(
    descriptor.memfd_socket_path, descriptor.memfd_pid, descriptor.memfd_block_id,
    descriptor.memfd_block_size, descriptor.size, descriptor.ipc_uid);
  imported->acquire_reader();
  if (imported->control()->ipc_uid.load(std::memory_order_acquire) != descriptor.ipc_uid) {
    imported->release_reader();
    throw std::runtime_error("stale memfd descriptor raced with block reuse");
  }

  auto reader_release = [imported](std::uint8_t *) { imported->release_reader(); };
  MemfdBuffer buffer(
    imported->payload(), descriptor.size, std::move(reader_release), imported->control(), imported,
    descriptor.memfd_block_id, descriptor.memfd_block_size);
  auto result = std::make_unique<MemfdBufferImpl<std::uint8_t>>(
    std::move(buffer), static_cast<std::size_t>(descriptor.size));
  return {result.release(), [](void * ptr) {
            delete static_cast<rosidl::BufferImplBase<std::uint8_t> *>(ptr);
          }};
}

}  // namespace memfd_buffer_backend

PLUGINLIB_EXPORT_CLASS(memfd_buffer_backend::MemfdBufferBackend, rosidl::BufferBackend)
