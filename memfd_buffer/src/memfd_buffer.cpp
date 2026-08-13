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

#include "memfd_buffer/memfd_buffer.hpp"

#include <stdexcept>

namespace memfd_buffer_backend
{

namespace
{

std::shared_ptr<void> make_lease(MemfdControlHeader * control)
{
  if (control == nullptr) {
    return {};
  }
  if (!try_acquire_memfd_reader(control)) {
    throw std::runtime_error("memfd block is being reused");
  }
  return std::shared_ptr<void>(control, [](void * ptr) {
    auto * header = static_cast<MemfdControlHeader *>(ptr);
    release_memfd_reader(header);
  });
}

}  // namespace

ReadHandle::ReadHandle(
  const std::uint8_t * data_ptr, std::shared_ptr<void> reader_lease, std::shared_ptr<void> owner,
  std::shared_ptr<HandleState> state)
: data_ptr_(data_ptr),
  state_(std::move(state)),
  reader_lease_(std::move(reader_lease)),
  owner_(std::move(owner))
{
}

ReadHandle::ReadHandle(ReadHandle && other) noexcept
: data_ptr_(other.data_ptr_),
  state_(std::move(other.state_)),
  reader_lease_(std::move(other.reader_lease_)),
  owner_(std::move(other.owner_)),
  promoted_buffer_(std::move(other.promoted_buffer_))
{
  other.data_ptr_ = nullptr;
}

ReadHandle & ReadHandle::operator=(ReadHandle && other) noexcept
{
  if (this != &other) {
    release();
    data_ptr_ = other.data_ptr_;
    state_ = std::move(other.state_);
    reader_lease_ = std::move(other.reader_lease_);
    owner_ = std::move(other.owner_);
    promoted_buffer_ = std::move(other.promoted_buffer_);
    other.data_ptr_ = nullptr;
  }
  return *this;
}

ReadHandle::~ReadHandle() { release(); }

void ReadHandle::release() noexcept
{
  auto state = std::move(state_);
  if (state != nullptr) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->active_readers > 0) {
        --state->active_readers;
      }
    }
  }
  reader_lease_.reset();
  owner_.reset();
  promoted_buffer_.reset();
  data_ptr_ = nullptr;
}

WriteHandle::WriteHandle(
  std::uint8_t * data_ptr, std::shared_ptr<HandleState> state, std::shared_ptr<void> owner)
: data_ptr_(data_ptr), state_(std::move(state)), owner_(std::move(owner))
{
}

WriteHandle::WriteHandle(WriteHandle && other) noexcept
: data_ptr_(other.data_ptr_),
  state_(std::move(other.state_)),
  owner_(std::move(other.owner_)),
  promoted_buffer_(std::move(other.promoted_buffer_))
{
  other.data_ptr_ = nullptr;
}

WriteHandle & WriteHandle::operator=(WriteHandle && other) noexcept
{
  if (this != &other) {
    release();
    data_ptr_ = other.data_ptr_;
    state_ = std::move(other.state_);
    owner_ = std::move(other.owner_);
    promoted_buffer_ = std::move(other.promoted_buffer_);
    other.data_ptr_ = nullptr;
  }
  return *this;
}

WriteHandle::~WriteHandle() { release(); }

void WriteHandle::release() noexcept
{
  if (state_ != nullptr) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->state = HandleState::State::Finalized;
  }
  promoted_buffer_.reset();
  owner_.reset();
  data_ptr_ = nullptr;
}

MemfdBuffer::MemfdBuffer(
  void * payload, std::size_t size, std::function<void(std::uint8_t *)> deleter,
  MemfdControlHeader * control, std::shared_ptr<void> owner, std::uint32_t block_id,
  std::uint64_t mapped_size, bool writable)
: data_ptr_(static_cast<std::uint8_t *>(payload)),
  size_(size),
  deleter_(std::move(deleter)),
  control_(control),
  owner_(std::move(owner)),
  block_id_(block_id),
  mapped_size_(mapped_size),
  writable_(writable)
{
  if (data_ptr_ == nullptr && size_ != 0) {
    throw std::invalid_argument("MemfdBuffer payload must not be null");
  }
  if (!deleter_) {
    deleter_ = [](std::uint8_t *) {};
  }
}

MemfdBuffer::~MemfdBuffer() { reset(); }

MemfdBuffer::MemfdBuffer(MemfdBuffer && other) noexcept
: data_ptr_(other.data_ptr_),
  size_(other.size_),
  deleter_(std::move(other.deleter_)),
  control_(other.control_),
  owner_(std::move(other.owner_)),
  held_reader_lease_(std::move(other.held_reader_lease_)),
  handle_state_(std::move(other.handle_state_)),
  block_id_(other.block_id_),
  mapped_size_(other.mapped_size_),
  writable_(other.writable_)
{
  other.data_ptr_ = nullptr;
  other.size_ = 0;
  other.control_ = nullptr;
  other.block_id_ = 0;
  other.mapped_size_ = 0;
  other.writable_ = true;
}

MemfdBuffer & MemfdBuffer::operator=(MemfdBuffer && other) noexcept
{
  if (this != &other) {
    reset();
    data_ptr_ = other.data_ptr_;
    size_ = other.size_;
    deleter_ = std::move(other.deleter_);
    control_ = other.control_;
    owner_ = std::move(other.owner_);
    held_reader_lease_ = std::move(other.held_reader_lease_);
    handle_state_ = std::move(other.handle_state_);
    block_id_ = other.block_id_;
    mapped_size_ = other.mapped_size_;
    writable_ = other.writable_;
    other.data_ptr_ = nullptr;
    other.size_ = 0;
    other.control_ = nullptr;
    other.block_id_ = 0;
    other.mapped_size_ = 0;
    other.writable_ = true;
  }
  return *this;
}

ReadHandle MemfdBuffer::get_read_handle() const
{
  if (handle_state_ == nullptr) {
    handle_state_ = std::make_shared<HandleState>();
  }
  auto state = handle_state_;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->state == HandleState::State::InUse) {
      throw std::runtime_error("cannot acquire a read handle while writing");
    }
    ++state->active_readers;
  }

  try {
    std::shared_ptr<void> reader_lease = make_lease(control_);
    return ReadHandle(data_ptr_, std::move(reader_lease), owner_, std::move(state));
  } catch (...) {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->active_readers > 0) {
      --state->active_readers;
    }
    throw;
  }
}

WriteHandle MemfdBuffer::get_write_handle()
{
  if (!writable_) {
    throw std::runtime_error("cannot acquire a write handle for a read-only memfd buffer");
  }
  if (data_ptr_ == nullptr || size_ == 0) {
    throw std::runtime_error("cannot acquire a write handle for an empty memfd buffer");
  }
  if (handle_state_ == nullptr) {
    handle_state_ = std::make_shared<HandleState>();
  }
  std::lock_guard<std::mutex> lock(handle_state_->mutex);
  if (handle_state_->state == HandleState::State::InUse) {
    throw std::runtime_error("memfd buffer write handle already in use");
  }
  if (handle_state_->state == HandleState::State::Finalized) {
    throw std::runtime_error("memfd buffer write has already been finalized");
  }
  if (handle_state_->active_readers != 0) {
    throw std::runtime_error("memfd buffer read handle already in use");
  }
  handle_state_->state = HandleState::State::InUse;
  return WriteHandle(data_ptr_, handle_state_, owner_);
}

void MemfdBuffer::finalize_write_handle() const
{
  if (handle_state_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(handle_state_->mutex);
  if (handle_state_->state == HandleState::State::InUse) {
    handle_state_->state = HandleState::State::Finalized;
  }
}

void MemfdBuffer::hold_reader_reference()
{
  if (held_reader_lease_ != nullptr) {
    return;
  }
  held_reader_lease_ = make_lease(control_);
}

void MemfdBuffer::reset() noexcept
{
  held_reader_lease_.reset();
  handle_state_.reset();
  owner_.reset();
  if (data_ptr_ != nullptr && deleter_) {
    try {
      deleter_(data_ptr_);
    } catch (...) {
      // A buffer destructor must not propagate a pool cleanup exception.
    }
  }
  data_ptr_ = nullptr;
  size_ = 0;
  control_ = nullptr;
  block_id_ = 0;
  mapped_size_ = 0;
  writable_ = true;
  deleter_ = {};
}

}  // namespace memfd_buffer_backend
