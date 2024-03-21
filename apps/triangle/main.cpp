#include <cstdint>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

class HelloTriangleApplication {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  GLFWwindow *window_;

  VkInstance instance_;

  void initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window_ = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
  }

  void initVulkan() { createInstance(); }

  void mainLoop() {
    while (!glfwWindowShouldClose(window_)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
    vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);

    glfwTerminate();
  }

  void createInstance() {
    uint32_t n_ext = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n_ext, nullptr);
    std::vector<VkExtensionProperties> exts{n_ext};
    vkEnumerateInstanceExtensionProperties(nullptr, &n_ext, exts.data());
    std::cout << "Available extensions:" << std::endl;
    for (const auto& ext : exts) {
      std::cout << "\t" << ext.extensionName << " @ " << ext.specVersion << std::endl;
    }
    std::cout << n_ext << " extensions in total." << std::endl;

    VkApplicationInfo app_info{}; // Optional.
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Hello Triangle";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info{}; // Required.
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    uint32_t n_glfw_ext = 0;
    const char **glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&n_glfw_ext);

    create_info.enabledExtensionCount = n_glfw_ext;
    create_info.ppEnabledExtensionNames = glfwExtensions;

    create_info.enabledLayerCount = 0;

    // Create info, allocator, instance pointer.
    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
      throw std::runtime_error("failed to create instance!");
    }
  }
};

int main() {
  HelloTriangleApplication app;

  try {
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}