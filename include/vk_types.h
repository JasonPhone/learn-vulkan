/**
 * @file vk_types.h
 * @author ja50n (zs_feng@qq.com)
 * @brief Base types and utils for this project.
 * @version 0.1
 * @date 2024-08-13
 */
#pragma once

#include <array>
#include <deque>
#include <stack>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#define VK_CHECK(x)                                                            \
  do {                                                                         \
    VkResult err = x;                                                          \
    if (err) {                                                                 \
      fmt::print("Detected Vulkan error: {}", string_VkResult(err));           \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#define VK_ONE_SEC 1000000000

/**
 * @brief Base class.
 */
class ObjectBase {};

/**
 * @brief Separate image.
 *        Images from swapchain are not guaranteed in formats
 *        (may be low precision) and have fixed resolution only.
 */
struct AllocatedImage {
  VkImage image;
  VkImageView image_view;
  VmaAllocation allocation;
  VkExtent3D image_extent;
  VkFormat image_format;
};