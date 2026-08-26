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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#include "memfd_buffer/memfd_buffer_api.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/u_int32.hpp"

class MemfdImageSubscriber : public rclcpp::Node
{
public:
  explicit MemfdImageSubscriber(const rclcpp::NodeOptions & options)
  : Node("memfd_image_subscriber", options)
  {
    rclcpp::SubscriptionOptions subscription_options;
    // Reject CPU fallback at endpoint matching time.  The test must fail if
    // descriptor creation in the publisher process did not find its block.
    subscription_options.acceptable_buffer_backends = "memfd";
    subscription_ = create_subscription<sensor_msgs::msg::Image>(
      "test_memfd_image_dso", 10,
      std::bind(&MemfdImageSubscriber::image_callback, this, std::placeholders::_1),
      subscription_options);

    count_publisher_ = create_publisher<std_msgs::msg::UInt32>("memfd_dso_subscriber_count", 10);
    validation_publisher_ = create_publisher<std_msgs::msg::Bool>("memfd_dso_validation", 10);
    backend_publisher_ = create_publisher<std_msgs::msg::Bool>("memfd_dso_backend_validation", 10);
    content_publisher_ = create_publisher<std_msgs::msg::Bool>("memfd_dso_content_validation", 10);
    metadata_publisher_ = create_publisher<std_msgs::msg::Bool>(
      "memfd_dso_metadata_validation", 10);
  }

private:
  static std::uint8_t pattern_for(std::size_t sequence)
  {
    return static_cast<std::uint8_t>((sequence * 37u + 11u) & 0xffu);
  }

  void image_callback(const sensor_msgs::msg::Image::SharedPtr msg)
  {
    ++received_count_;
    bool metadata_valid = msg->header.frame_id ==
      "memfd_pool_dso_" + std::to_string(received_count_);
    metadata_valid = metadata_valid && msg->height == 16u && msg->width == 256u;
    metadata_valid = metadata_valid && msg->encoding == "mono8" && msg->step == 256u;
    metadata_valid = metadata_valid && msg->data.size() == 4096u;

    const bool backend_valid = msg->data.get_backend_type() == "memfd";
    bool content_valid = false;
    try {
      // Check the backend before acquiring the handle so CPU fallback cannot
      // be silently promoted back to memfd by from_input_buffer().
      if (backend_valid) {
        const auto read = memfd_buffer_backend::from_input_buffer(msg->data);
        std::vector<std::uint8_t> data(msg->data.size());
        std::memcpy(data.data(), read.get_ptr(), data.size());
        const auto expected = pattern_for(received_count_);
        content_valid = !data.empty();
        for (const auto value : data) {
          if (value != expected) {
            content_valid = false;
            break;
          }
        }
      }
    } catch (const std::exception & exception) {
      RCLCPP_ERROR(get_logger(), "Failed to read memfd payload: %s", exception.what());
    }

    const bool message_valid = metadata_valid && backend_valid && content_valid;
    validation_passed_ = validation_passed_ && message_valid;
    publish_result(message_valid, backend_valid, content_valid, metadata_valid);
  }

  void publish_result(bool valid, bool backend_valid, bool content_valid, bool metadata_valid)
  {
    std_msgs::msg::UInt32 count;
    count.data = received_count_;
    count_publisher_->publish(count);

    std_msgs::msg::Bool validation;
    validation.data = validation_passed_;
    validation_publisher_->publish(validation);

    std_msgs::msg::Bool backend;
    backend.data = backend_valid;
    backend_publisher_->publish(backend);

    std_msgs::msg::Bool content;
    content.data = content_valid;
    content_publisher_->publish(content);

    std_msgs::msg::Bool metadata;
    metadata.data = metadata_valid;
    metadata_publisher_->publish(metadata);

    if (!valid) {
      RCLCPP_ERROR(get_logger(), "Invalid memfd image #%u", received_count_);
    }
  }

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::Publisher<std_msgs::msg::UInt32>::SharedPtr count_publisher_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr validation_publisher_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr backend_publisher_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr content_publisher_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr metadata_publisher_;
  std::uint32_t received_count_{0};
  bool validation_passed_{true};
};

RCLCPP_COMPONENTS_REGISTER_NODE(MemfdImageSubscriber)
