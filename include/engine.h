#pragma once
#include "vk_types.h"

struct FrameData {
  VkCommandPool cmd_pool;
  VkCommandBuffer cmd_buffer_main;
};

constexpr uint32_t kFrameOverlap = 2;

class Engine : public ObjectBase {
public:
  // Engine() = delete;
  void init();
  void run();
  void draw();
  void cleanup();

  bool stop_rendering{false};
  bool is_initialized{false};
  int frame_number{0};
  VkExtent2D window_extent{1280, 720};

  struct SDL_Window *window{nullptr};

  static Engine &get();

  FrameData &getCurrentFrame() {
    return m_frames[frame_number % kFrameOverlap];
  }

private:
  VkInstance m_instance;                  // Vulkan library handle
  VkDebugUtilsMessengerEXT m_debug_msngr; // Vulkan debug output handle
  VkPhysicalDevice m_chosen_GPU;          // GPU chosen as the default device
  VkDevice m_device;                      // Vulkan device for commands
  VkSurfaceKHR m_surface;                 // Vulkan window surface

  VkSwapchainKHR m_swapchain;
  VkFormat m_swapchain_img_format;
  std::vector<VkImage> m_swapchain_imgs;
  std::vector<VkImageView> m_swapchain_img_views;
  VkExtent2D m_swapchain_extent;

  FrameData m_frames[kFrameOverlap];
  VkQueue m_graphic_queue;
  uint32_t m_graphic_queue_family;

private:
  void initVulkan();
  void initSwapchain();
  void initCommands();
  void initSyncStructures();
  void createSwapchain(int w, int h);
  void destroySwapchain();
};
