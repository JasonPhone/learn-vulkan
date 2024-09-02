#pragma once
#include "vk_types.h"

namespace vkutil {
bool loadShaderModule(const char *file_path, VkDevice device,
                        VkShaderModule *out_shader_module);

}