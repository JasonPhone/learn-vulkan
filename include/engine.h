#pragma once
#include "vk_types.h"

/**
 * @brief Manage the deletion.
 * @note Better store vulkan handles instead of functions.
 */
struct DeletionQueue {
  std::stack<std::function<void()>> delete_callbacks;
  void push(std::function<void()> &&function) {
    delete_callbacks.push(function);
  }
  void flush() {
    while (!delete_callbacks.empty()) {
      delete_callbacks.top()();
      delete_callbacks.pop();
    }
  }
};

struct FrameData {
  VkCommandPool cmd_pool;
  VkCommandBuffer cmd_buffer_main;

  // https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples
  // Sync between GPU queues, one operation wait another to signal a semaphore.
  VkSemaphore swapchain_semaphore, render_semaphore; // Two one-way channels.
  // Sync between CPU and GPU, CPU waits for some GPU operations to finish.
  VkFence render_fence;

  DeletionQueue deletion_queue;
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

  DeletionQueue m_main_deletion_queue;

  VmaAllocator m_allocator;

  AllocatedImage m_draw_image;
  VkExtent2D m_draw_extent;

private:
  void drawBackground(VkCommandBuffer cmd);
  void initVulkan();
  void initSwapchain();
  void initCommands();
  void initSyncStructures();
  void createSwapchain(int w, int h);
  void destroySwapchain();
};
