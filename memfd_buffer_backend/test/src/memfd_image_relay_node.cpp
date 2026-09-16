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

#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "sensor_msgs/msg/image.hpp"

/// A zero-copy forwarding node: it subscribes to a memfd-backed image and
/// republishes the same message object.  The backend serializes the imported
/// buffer's original IPC descriptor instead of copying the payload to CPU.
class MemfdImageRelay : public rclcpp::Node
{
public:
  explicit MemfdImageRelay(const rclcpp::NodeOptions & options)
  : Node("memfd_image_relay", options)
  {
    const auto input_topic = declare_parameter<std::string>(
      "input_topic", "test_memfd_image_dso");
    const auto output_topic = declare_parameter<std::string>(
      "output_topic", "test_memfd_image_dso_relay");

    rclcpp::SubscriptionOptions subscription_options;
    subscription_options.acceptable_buffer_backends = "memfd";
    subscription_ = create_subscription<sensor_msgs::msg::Image>(
      input_topic, 10,
      std::bind(&MemfdImageRelay::image_callback, this, std::placeholders::_1),
      subscription_options);
    publisher_ = create_publisher<sensor_msgs::msg::Image>(output_topic, 10);
  }

private:
  void image_callback(std::shared_ptr<const sensor_msgs::msg::Image> msg)
  {
    // publish(const MessageT &) keeps the received buffer implementation
    // intact.  No vector conversion or allocation is performed here.
    publisher_->publish(*msg);
  }

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
};

RCLCPP_COMPONENTS_REGISTER_NODE(MemfdImageRelay)
