#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const std::vector<const char *> validation_layers = {
    "VK_LAYER_KHRONOS_validation",
};

const std::vector<const char *> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
  return VK_FALSE;
}

bool checkValidationLayerSupport() {
  uint32_t n_layers;
  // Device validation layers are deprecated
  // but should be enabled for compatibility.
  vkEnumerateInstanceLayerProperties(&n_layers, nullptr);
  std::vector<VkLayerProperties> layers{n_layers};
  vkEnumerateInstanceLayerProperties(&n_layers, layers.data());

  // Brute match.
  for (const char *target_layer_name : validation_layers) {
    bool found = false;
    for (const auto &available_layer : layers) {
      if (std::strcmp(target_layer_name, available_layer.layerName) == 0) {
        found = true;
        break;
      }
    }
    if (found == false)
      return false;
  }
  return true;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
  uint32_t n_ext;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &n_ext, nullptr);

  std::vector<VkExtensionProperties> available_exts(n_ext);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &n_ext,
                                       available_exts.data());

  std::set<std::string> required_exts(device_extensions.begin(),
                                      device_extensions.end());

  for (const auto &extension : available_exts) {
    required_exts.erase(extension.extensionName);
  }

  return required_exts.empty();
}

struct QueueFamilyIndices {
  std::optional<uint32_t> graphics_family;
  std::optional<uint32_t> present_family;

  bool isComplete() {
    return graphics_family.has_value() && present_family.has_value();
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> present_modes;
};

std::vector<const char *> getRequiredExtensions() {
  uint32_t n_glfw_ext;
  const char **glfw_exts = glfwGetRequiredInstanceExtensions(&n_glfw_ext);
  std::vector<const char *> exts{glfw_exts, glfw_exts + n_glfw_ext};

  if (kEnableValidationLayers)
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  return exts;
}

void populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT &create_info) {
  create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = debugCallback;
}

VkResult createDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *p_create_info,
    const VkAllocationCallbacks *p_allocator,
    VkDebugUtilsMessengerEXT *p_debug_messenger) {
  // Locate creator function.
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, p_create_info, p_allocator, p_debug_messenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}
void destroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debug_messenger,
                                   const VkAllocationCallbacks *p_allocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debug_messenger, p_allocator);
  }
}

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
  VkDebugUtilsMessengerEXT debug_messenger_;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_;
  VkQueue graphics_queue_;
  VkQueue present_queue_;
  VkSurfaceKHR surface_;

  void initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window_ = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    selectPhysicalDevice();
    createLogicalDevice();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window_)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
    vkDestroyDevice(device_, nullptr);
    if (kEnableValidationLayers)
      destroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);

    vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);
    glfwTerminate();
  }

  void createSurface() {
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) !=
        VK_SUCCESS)
      throw std::runtime_error("Failed creating window surface.");
  }

  void createLogicalDevice() {
    QueueFamilyIndices indices = getQueueFamilies(physical_device_);

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<uint32_t> unique_queue_families{indices.graphics_family.value(),
                                             indices.present_family.value()};
    float queue_priority = 1.0f;
    for (uint32_t queue_family : unique_queue_families) {
      VkDeviceQueueCreateInfo queue_create_info{};
      queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queue_create_info.queueFamilyIndex = queue_family;
      queue_create_info.queueCount = 1;
      queue_create_info.pQueuePriorities = &queue_priority;
      queue_create_infos.push_back(queue_create_info);
    }

    VkPhysicalDeviceFeatures device_features{};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.queueCreateInfoCount =
        static_cast<uint32_t>(queue_create_infos.size());

    create_info.pEnabledFeatures = &device_features;

    create_info.ppEnabledExtensionNames = device_extensions.data();
    create_info.enabledExtensionCount =
        static_cast<uint32_t>(device_extensions.size());

    if (kEnableValidationLayers) {
      create_info.enabledLayerCount =
          static_cast<uint32_t>(validation_layers.size());
      create_info.ppEnabledLayerNames = validation_layers.data();
    } else {
      create_info.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) !=
        VK_SUCCESS)
      throw std::runtime_error("Failed creating logical device.");

    vkGetDeviceQueue(device_, indices.graphics_family.value(), 0,
                     &graphics_queue_);
    vkGetDeviceQueue(device_, indices.present_family.value(), 0,
                     &present_queue_);
  }

  void selectPhysicalDevice() {
    uint32_t n_device = 0;
    vkEnumeratePhysicalDevices(instance_, &n_device, nullptr);
    if (n_device == 0)
      throw std::runtime_error("No GPU supports Vulkan.");
    std::vector<VkPhysicalDevice> devices{n_device};
    vkEnumeratePhysicalDevices(instance_, &n_device, devices.data());

    for (const auto &device : devices) {
      if (checkPhysicalDevice(device)) {
        physical_device_ = device;
        break;
      }
    }
    if (physical_device_ == VK_NULL_HANDLE)
      throw std::runtime_error("Failed selecting physical device.");
  }

  void createInstance() {
    if (kEnableValidationLayers && !checkValidationLayerSupport())
      throw std::runtime_error(
          "Validation layer enabled but none is available.");

    uint32_t n_ext = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n_ext, nullptr);
    std::vector<VkExtensionProperties> exts{n_ext};
    vkEnumerateInstanceExtensionProperties(nullptr, &n_ext, exts.data());
    std::cout << "Available extensions:" << std::endl;
    for (const auto &ext : exts) {
      std::cout << "\t" << ext.extensionName << " @ " << ext.specVersion
                << std::endl;
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

    auto extensions = getRequiredExtensions();
    create_info.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    if (kEnableValidationLayers) {
      create_info.enabledLayerCount =
          static_cast<uint32_t>(validation_layers.size());
      create_info.ppEnabledLayerNames = validation_layers.data();
      populateDebugMessengerCreateInfo(debug_create_info);
      // Debug the creation and destruction.
      create_info.pNext =
          (VkDebugUtilsMessengerCreateInfoEXT *)&debug_create_info;
    } else {
      create_info.enabledLayerCount = 0;
      create_info.pNext = nullptr;
    }

    // Create info, allocator, instance pointer.
    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
      throw std::runtime_error("Failed creating instance.");
    }
  }

  void setupDebugMessenger() {
    if (!kEnableValidationLayers)
      return;
    VkDebugUtilsMessengerCreateInfoEXT create_info;
    populateDebugMessengerCreateInfo(create_info);

    if (createDebugUtilsMessengerEXT(instance_, &create_info, nullptr,
                                     &debug_messenger_) != VK_SUCCESS)
      throw std::runtime_error("Failed creating debug messenger.");
  }

  QueueFamilyIndices getQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices{};
    uint32_t n_queue_family = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n_queue_family, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families{n_queue_family};
    // Should be idempotent.
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n_queue_family,
                                             queue_families.data());
    int idx = 0;
    for (const auto &family : queue_families) {
      if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        indices.graphics_family = idx;

      VkBool32 support_present = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, idx, surface_,
                                           &support_present);
      if (support_present)
        indices.present_family = idx;

      idx++;
    }
    return indices;
  }

  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_,
                                              &details.capabilities);

    uint32_t n_format = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &n_format, nullptr);
    if (n_format != 0) {
      details.formats.resize(n_format);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &n_format,
                                           details.formats.data());
    }

    uint32_t n_present_modes = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_,
                                              &n_present_modes, nullptr);
    if (n_present_modes != 0) {
      details.present_modes.resize(n_present_modes);
      vkGetPhysicalDeviceSurfacePresentModesKHR(
          device, surface_, &n_present_modes, details.present_modes.data());
    }

    return details;
  }

  bool checkPhysicalDevice(VkPhysicalDevice device) {
    // VkPhysicalDeviceProperties properties;
    // VkPhysicalDeviceFeatures features;
    // vkGetPhysicalDeviceProperties(device, &properties);
    // vkGetPhysicalDeviceFeatures(device, &features);
    QueueFamilyIndices indices = getQueueFamilies(device);

    bool support_ext = checkDeviceExtensionSupport(device);

    bool swap_chain_adequate = false;
    if (support_ext) {
      SwapChainSupportDetails details = querySwapChainSupport(device);
      swap_chain_adequate =
          !details.formats.empty() && !details.present_modes.empty();
    }

    return indices.isComplete() && support_ext && swap_chain_adequate;
  }
  VkSurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &available_formats) {
    for (const auto &format : available_formats) {
      if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
          format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        return format;
    }
    return available_formats[0];
  }
  VkPresentModeKHR chooseSwapPresentMode(
      const std::vector<VkPresentModeKHR> &available_present_modes) {
    for (const auto &mode : available_present_modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
  }
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max()) {
      return capabilities.currentExtent;
    } else {
      int width, height;
      glfwGetFramebufferSize(window_, &width, &height);

      VkExtent2D actualExtent = {static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height)};

      actualExtent.width =
          std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                     capabilities.maxImageExtent.width);
      actualExtent.height =
          std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                     capabilities.maxImageExtent.height);

      return actualExtent;
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