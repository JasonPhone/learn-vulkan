#include "vk_pipelines.h"
#include <fstream>
#include "vk_initializers.h"

bool vkutil::loadShaderModule(const char *file_path, VkDevice device,
                              VkShaderModule *out_shader_module) {

  // Open the file with cursor at the end.
  std::ifstream file(file_path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    fmt::print("Error reading shader file %s", file_path);
    return false;
  }
  // Find size of the file by looking up the location of the cursor.
  // Because the cursor is at the end, it gives the size directly in bytes.
  size_t fileSize = (size_t)file.tellg();
  // Spirv expects the buffer to be in uint32, so make sure to reserve a int.
  // Should be big enough for the entire file.
  std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
  // Put file cursor at beginning.
  file.seekg(0);
  // Load the entire file into the buffer.
  file.read((char *)buffer.data(), fileSize);
  file.close();

  // Create a new shader module, using the buffer we loaded.
  VkShaderModuleCreateInfo create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.pNext = nullptr;
  create_info.codeSize = buffer.size() * sizeof(uint32_t); // In bytes.
  create_info.pCode = buffer.data();

  VkShaderModule shader_module;
  if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) !=
      VK_SUCCESS) {
    return false;
  }
  *out_shader_module = shader_module;
  return true;
}