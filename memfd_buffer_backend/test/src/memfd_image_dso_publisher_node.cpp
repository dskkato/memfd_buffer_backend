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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "memfd_buffer/memfd_buffer_api.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "sensor_msgs/msg/image.hpp"

class MemfdImageDsoPublisher : public rclcpp::Node
{
public:
  explicit MemfdImageDsoPublisher(const rclcpp::NodeOptions & options)
  : Node("memfd_image_dso_publisher", options)
  {
    publisher_ = create_publisher<sensor_msgs::msg::Image>("test_memfd_image_dso", 10);
    timer_ = create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&MemfdImageDsoPublisher::publish_next, this));
  }

private:
  static constexpr std::size_t kMessages = 5;
  static constexpr std::size_t kPayloadSize = 4096;

  static std::uint8_t pattern_for(std::size_t sequence)
  {
    return static_cast<std::uint8_t>((sequence * 37u + 11u) & 0xffu);
  }

  void publish_next()
  {
    if (sequence_ >= kMessages) {
      timer_->cancel();
      return;
    }
    if (publisher_->get_subscription_count() == 0u) {
      return;
    }

    const std::size_t sequence = sequence_ + 1u;
    const std::uint8_t pattern = pattern_for(sequence);

    sensor_msgs::msg::Image msg;
    msg.header.stamp = now();
    msg.header.frame_id = "memfd_pool_dso_" + std::to_string(sequence);
    msg.height = 16;
    msg.width = 256;
    msg.encoding = "mono8";
    msg.is_bigendian = 0;
    msg.step = 256;
    msg.data = memfd_buffer_backend::allocate_buffer(kPayloadSize);

    {
      auto write = memfd_buffer_backend::from_output_buffer(msg.data);
      for (std::size_t i = 0; i < kPayloadSize; ++i) {
        write.get_ptr()[i] = pattern;
      }
    }

    publisher_->publish(msg);
    sequence_ = sequence;
  }

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::size_t sequence_{0};
};

RCLCPP_COMPONENTS_REGISTER_NODE(MemfdImageDsoPublisher)
