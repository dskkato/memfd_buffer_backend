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

#include "memfd_buffer/host_endpoint_manager.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>

namespace memfd_buffer_backend
{

HostEndpointManager::HostEndpointManager()
{
  std::array<char, 256> hostname{};
  if (gethostname(hostname.data(), hostname.size() - 1) != 0) {
    const auto euid = static_cast<std::uint64_t>(geteuid());
    metadata_ = "unknown:" + std::to_string(euid);
    return;
  }
  hostname.back() = '\0';
  const auto euid = static_cast<std::uint64_t>(geteuid());
  metadata_ = std::string(hostname.data()) + ":" + std::to_string(euid);
}

}  // namespace memfd_buffer_backend
