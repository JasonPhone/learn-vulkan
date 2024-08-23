#include "vk_images.h"
#include "vk_initializers.h"

void vkutil::transitionImage(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout cur_layout,
                             VkImageLayout new_layout) {
  // Pipeline barrier.
  VkImageMemoryBarrier2 img_barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  img_barrier.pNext = nullptr;

  img_barrier.srcStageMask =
      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // Can be more specific.
  img_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
  img_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  img_barrier.dstAccessMask =
      VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

  img_barrier.oldLayout = cur_layout;
  img_barrier.newLayout = new_layout;

  VkImageAspectFlags aspectMask =
      (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
          ? VK_IMAGE_ASPECT_DEPTH_BIT
          : VK_IMAGE_ASPECT_COLOR_BIT;
  img_barrier.subresourceRange = vkinit::imageSubresourceRange(aspectMask);
  img_barrier.image = image;

  VkDependencyInfo dep_info{};
  dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dep_info.pNext = nullptr;

  dep_info.imageMemoryBarrierCount =
      1; // Transit multiple images at once will be faster.
  dep_info.pImageMemoryBarriers = &img_barrier;

  vkCmdPipelineBarrier2(cmd, &dep_info);
}