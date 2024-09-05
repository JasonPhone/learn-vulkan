#pragma once
#include "vk_types.h"
#include "vk_descriptors.h"

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

struct ComputePushConstants {
  glm::vec4 data1;
  glm::vec4 data2;
  glm::vec4 data3;
  glm::vec4 data4;
};

struct ComputePipeline {
  const char *name;
  VkPipeline pipeline;
  VkPipelineLayout layout;
  ComputePushConstants data;
};

constexpr uint32_t kFrameOverlap = 2;

class Engine : public ObjectBase {
public:
  // Engine() = delete;
  void init();
  void run();
  void draw();
  void cleanup();
  void immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&func);
  GPUMeshBuffers uploadMesh(std::span<uint32_t> indices,
                            std::span<Vertex> vertices);

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

  DescriptorAllocator m_global_ds_allocator;
  VkDescriptorSet m_draw_image_ds;
  VkDescriptorSetLayout m_draw_image_ds_layout;

  VkPipelineLayout m_default_pipeline_layout;
  VkPipeline m_default_pipeline;
  std::vector<ComputePipeline> m_compute_pipelines;
  int m_cur_comp_pipeline_idx = 0;
  VkPipelineLayout m_triangle_pipeline_layout;
  VkPipeline m_triangle_pipeline;
  VkPipelineLayout m_simple_mesh_pipeline_layout;
  VkPipeline m_simple_mesh_pipeline;
  GPUMeshBuffers m_simple_mesh;

  VkFence m_imm_fence;
  VkCommandBuffer m_imm_cmd;
  VkCommandPool m_imm_cmd_pool;

private:
  void initVulkan();
  void initSwapchain();
  void initCommands();
  void initSyncStructures();

  void initShaderDescriptors();
  void initPipelines();
  void initBackgroundPipelines();
  void initTrianglePipeline();
  void initSimpleMeshPipeline();
  void initDefaultMesh();

  void initImGui();
  void drawImGui(VkCommandBuffer cmd, VkImageView target_img_view);
  void drawBackground(VkCommandBuffer cmd);
  void drawGeometry(VkCommandBuffer cmd);

  void createSwapchain(int w, int h);
  void destroySwapchain();

  AllocatedBuffer createBuffer(size_t alloc_size, VkBufferUsageFlags usage,
                               VmaMemoryUsage mem_usage);
  void destroyBuffer(const AllocatedBuffer &buffer);

};
