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

#ifndef MEMFD_BUFFER__MEMFD_BUFFER_API_HPP_
#define MEMFD_BUFFER__MEMFD_BUFFER_API_HPP_

#include <cstring>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "memfd_buffer/memfd_buffer_impl.hpp"
#include "rosidl_buffer/buffer.hpp"

namespace memfd_buffer_backend
{

/// \brief Allocate a fresh memfd-backed \c rosidl::Buffer<std::uint8_t>.
///
/// \details Allocates exactly \p count payload bytes from the memfd memory pool.
/// The returned buffer owns the allocation. This function does not initialize or
/// copy payload data. A zero count produces an empty buffer; the handle-acquisition
/// functions reject empty buffers.
/// \param count Number of payload bytes to allocate.
/// \return A newly allocated memfd-backed buffer.
/// \throws std::runtime_error If creating or mapping the memfd fails.
/// \throws std::length_error If the requested mapping is too large.
inline rosidl::Buffer<std::uint8_t> allocate_buffer(std::size_t count)
{
  return rosidl::Buffer<std::uint8_t>(std::make_unique<MemfdBufferImpl<std::uint8_t>>(count));
}

namespace detail
{

/// \brief Allocate a shared, fresh memfd-backed \c rosidl::Buffer<std::uint8_t>.
///
/// \details This is an internal helper for promoting non-memfd input buffers.
/// The \p byte_count argument is a byte count, rather than an element count.
/// The shared ownership lets a promoted read handle keep the new buffer alive
/// until the handle is released.
/// \param byte_count Number of payload bytes to allocate.
/// \return A shared pointer owning the newly allocated buffer.
/// \throws std::runtime_error If creating or mapping the memfd fails.
/// \throws std::length_error If the requested mapping is too large.
inline std::shared_ptr<rosidl::Buffer<std::uint8_t>> allocate_memfd_buffer_shared(
  std::size_t byte_count)
{
  return std::make_shared<rosidl::Buffer<std::uint8_t>>(
    std::make_unique<MemfdBufferImpl<std::uint8_t>>(byte_count));
}

template <typename T>
inline MemfdBufferImpl<T> * memfd_impl_of(rosidl::Buffer<T> & buffer)
{
  return dynamic_cast<MemfdBufferImpl<T> *>(buffer.get_impl());
}

}  // namespace detail

/// \brief Acquire exclusive mutable access to a memfd-backed buffer payload.
///
/// \details The input buffer must have a non-null implementation, contain at
/// least one element, and already be memfd-backed. This function does not
/// convert or replace buffers from another backend. Rejecting those buffers is
/// intentional: a handle for a newly allocated, detached memfd buffer would
/// not be reflected by the original buffer during message publication.
///
/// The returned handle is move-only and its \c get_ptr() remains valid while
/// the handle and the source buffer remain alive. Only one write handle may be
/// active for an allocation.
/// \param buffer Memfd-backed buffer for which to acquire write access.
/// \return A move-only handle exposing a writable \c std::uint8_t payload.
/// \throws MemfdError If \p buffer has no implementation, is empty, or is not
/// memfd-backed.
/// \throws std::runtime_error If a writer is already active or the write state
/// has already been finalized.
template <typename T>
WriteHandle from_output_buffer(rosidl::Buffer<T> & buffer)
{
  auto * impl = buffer.get_impl();
  if (impl == nullptr) {
    throw MemfdError("from_output_buffer called with null buffer implementation");
  }
  if (buffer.size() == 0) {
    throw MemfdError("from_output_buffer called on empty buffer");
  }

  auto * memfd_impl = dynamic_cast<MemfdBufferImpl<T> *>(impl);
  if (memfd_impl == nullptr) {
    throw MemfdError("from_output_buffer requires a memfd-backed buffer");
  }
  return memfd_impl->get_memfd_buffer().get_write_handle();
}

/// \brief Acquire read-only access to a buffer payload.
///
/// \details The input buffer must have a non-null implementation and contain
/// at least one element. If \p buffer is already memfd-backed, a read handle
/// for that allocation is returned directly and holds one logical reader
/// reference. Otherwise, a fresh memfd-backed
/// \c rosidl::Buffer<std::uint8_t> of the same byte size is allocated and the
/// source contents are copied synchronously with \c std::memcpy. The returned
/// handle exposes the copied payload and owns the promoted buffer.
///
/// The returned handle is move-only and its \c get_ptr() remains valid only
/// while the handle and, for a directly backed buffer, the source buffer remain
/// alive. Keep the handle alive for the entire read operation.
/// \param buffer Buffer from which to acquire read access.
/// \return A move-only handle exposing a read-only \c std::uint8_t payload.
/// \throws MemfdError If \p buffer has no implementation or is empty.
/// \throws std::runtime_error If a memfd block is being reused while its read
/// handle is acquired.
template <typename T>
ReadHandle from_input_buffer(const rosidl::Buffer<T> & buffer)
{
  const auto * impl = buffer.get_impl();
  if (impl == nullptr) {
    throw MemfdError("from_input_buffer called with null buffer implementation");
  }
  if (buffer.size() == 0) {
    throw MemfdError("from_input_buffer called on empty buffer");
  }

  if (const auto * memfd_impl = dynamic_cast<const MemfdBufferImpl<T> *>(impl)) {
    return memfd_impl->get_memfd_buffer().get_read_handle();
  }

  const std::size_t bytes = buffer.size() * sizeof(T);
  auto promoted = detail::allocate_memfd_buffer_shared(bytes);
  auto * promoted_impl = detail::memfd_impl_of(*promoted);
  auto write = promoted_impl->get_memfd_buffer().get_write_handle();
  const std::vector<T> source = buffer.to_vector();
  std::memcpy(write.get_ptr(), source.data(), bytes);
  auto read = promoted_impl->get_memfd_buffer().get_read_handle();
  read.set_promoted_buffer(std::move(promoted));
  return read;
}

/// \brief Copy bytes from a source pointer into a write handle's payload.
///
/// \details Performs a synchronous CPU \c std::memcpy. This function does not
/// allocate memory, resize the destination, or verify its capacity. The caller
/// must ensure that \p handle is an active write handle whose payload has room
/// for at least \p byte_count bytes, and that \p source points to at least that
/// many readable bytes. The source and destination must not overlap.
/// \param source Source memory from which to copy.
/// \param byte_count Number of bytes to copy.
/// \param handle Active write handle receiving the copied bytes.
/// \note If \p source, \p handle.get_ptr(), or \p byte_count is zero/null, the
/// function returns without copying.
inline void to_buffer(const void * source, std::size_t byte_count, WriteHandle & handle)
{
  if (source == nullptr || handle.get_ptr() == nullptr || byte_count == 0) {
    return;
  }
  std::memcpy(handle.get_ptr(), source, byte_count);
}

}  // namespace memfd_buffer_backend

#endif  // MEMFD_BUFFER__MEMFD_BUFFER_API_HPP_
