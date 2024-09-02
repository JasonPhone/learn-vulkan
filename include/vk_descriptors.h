#pragma once
#include "vk_types.h"

/**
 * @note  A descriptor set contains multiple
 *        bindings, each bound with an image or buffer.
 *        The descriptor layout is used to allocate a
 *        descriptor set from descriptor pool.
 */

struct DescriptorLayoutBuilder {

  std::vector<VkDescriptorSetLayoutBinding> bindings;

  void addBinding(uint32_t binding, VkDescriptorType type);
  void clear();
  VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shader_stages,
                              void *p_next = nullptr,
                              VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorAllocator {

  struct PoolSizeRatio {
    VkDescriptorType type;
    float ratio;
  };

  VkDescriptorPool pool; // Pool only for descriptor allocation.
  /**
   * @note Reset the small pool for per-frame descriptors is recommended
   *       and guaranteed to be the fastest method.
   */

  /**
   * @brief Init a descriptor pool.
   *
   * @param device Logical device.
   * @param max_sets #descriptor sets.
   * @param pool_ratios
   */
  void initPool(VkDevice device, uint32_t max_sets,
                std::span<PoolSizeRatio> pool_ratios);
  void clearDescriptors(VkDevice device);
  void destroyPool(VkDevice device);

  VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};