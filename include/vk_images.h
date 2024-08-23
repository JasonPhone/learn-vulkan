#pragma once

#include <vulkan/vulkan.h>

namespace vkutil {
void transitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout cur_layout, VkImageLayout new_layout);
}