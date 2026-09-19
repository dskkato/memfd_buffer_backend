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

#ifndef SHARED_BUFFER__SHARED_BUFFER_C_API_H_
#define SHARED_BUFFER__SHARED_BUFFER_C_API_H_

#include <stddef.h>
#include <stdint.h>

#include "shared_buffer/visibility_control.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// Opaque shared-memory buffer owned by the C++ implementation.
typedef struct shared_buffer_rust_buffer shared_buffer_rust_buffer_t;

/// Opaque scoped read access to a shared-memory buffer.
typedef struct shared_buffer_rust_read_access shared_buffer_rust_read_access_t;

/// Opaque scoped write access to a shared-memory buffer.
typedef struct shared_buffer_rust_write_access shared_buffer_rust_write_access_t;

/// Return codes used by the C ABI. No C++ exception crosses this interface.
enum shared_buffer_rust_status
{
  SHARED_BUFFER_RUST_OK = 0,
  SHARED_BUFFER_RUST_INVALID_ARGUMENT = 1,
  SHARED_BUFFER_RUST_ALLOCATION_FAILED = 2,
  SHARED_BUFFER_RUST_ACCESS_FAILED = 3
};

/// Allocate a shared-memory buffer with byte_count payload bytes.
///
/// byte_count must be greater than zero. On success, *out owns the allocation
/// and must eventually be passed to shared_buffer_rust_buffer_destroy().
SHARED_BUFFER_PUBLIC int shared_buffer_rust_buffer_new(
  size_t byte_count, shared_buffer_rust_buffer_t **out);

/// Destroy a buffer. NULL is accepted.
SHARED_BUFFER_PUBLIC void shared_buffer_rust_buffer_destroy(
  shared_buffer_rust_buffer_t * buffer);

/// Return the payload size, or zero for NULL.
SHARED_BUFFER_PUBLIC size_t shared_buffer_rust_buffer_size(
  const shared_buffer_rust_buffer_t * buffer);

/// Acquire a scoped read access. The access keeps the buffer allocation alive.
SHARED_BUFFER_PUBLIC int shared_buffer_rust_read_access_new(
  const shared_buffer_rust_buffer_t * buffer, shared_buffer_rust_read_access_t **out);

/// Release a read access. NULL is accepted.
SHARED_BUFFER_PUBLIC void shared_buffer_rust_read_access_destroy(
  shared_buffer_rust_read_access_t * access);

SHARED_BUFFER_PUBLIC const uint8_t * shared_buffer_rust_read_access_data(
  const shared_buffer_rust_read_access_t * access);

SHARED_BUFFER_PUBLIC size_t shared_buffer_rust_read_access_size(
  const shared_buffer_rust_read_access_t * access);

/// Acquire a scoped exclusive write access. The access keeps the buffer alive.
SHARED_BUFFER_PUBLIC int shared_buffer_rust_write_access_new(
  shared_buffer_rust_buffer_t * buffer, shared_buffer_rust_write_access_t **out);

/// Release a write access and finalize the write lease. NULL is accepted.
SHARED_BUFFER_PUBLIC void shared_buffer_rust_write_access_destroy(
  shared_buffer_rust_write_access_t * access);

SHARED_BUFFER_PUBLIC uint8_t * shared_buffer_rust_write_access_data(
  shared_buffer_rust_write_access_t * access);

SHARED_BUFFER_PUBLIC size_t shared_buffer_rust_write_access_size(
  const shared_buffer_rust_write_access_t * access);

#ifdef __cplusplus
}
#endif

#endif  // SHARED_BUFFER__SHARED_BUFFER_C_API_H_
