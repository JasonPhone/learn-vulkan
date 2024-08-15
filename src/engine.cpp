#include "engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "vk_initializers.h"
#include "vk_types.h"
#include "VkBootstrap.h"

#include <chrono>
#include <thread>

constexpr bool bUseValidationLayers = true;

Engine *loaded_engine = nullptr;

Engine &Engine::get() { return *loaded_engine; }
void Engine::init() {
  // Only one engine initialization is allowed with the application.
  assert(loaded_engine == nullptr);
  loaded_engine = this;

  // We initialize SDL and create a window with it.
  SDL_Init(SDL_INIT_VIDEO);

  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

  window = SDL_CreateWindow("Vulkan Engine", window_extent.width,
                            window_extent.height, window_flags);

  initVulkan();
  initSwapchain();
  initCommands();
  initSyncStructures();
  is_initialized = true;
}

void Engine::cleanup() {
  if (is_initialized) {
    // Order matters, reversed of initialization.
    vkDeviceWaitIdle(m_device); // Wait for GPU to finish.
    for (uint32_t i = 0; i < kFrameOverlap; i++)
      // Cmd buffer is destroyed with pool it comes from.
      vkDestroyCommandPool(m_device, m_frames[i].cmd_pool, nullptr);
    destroySwapchain();
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyDevice(m_device, nullptr);
    vkb::destroy_debug_utils_messenger(m_instance, m_debug_msngr);
    vkDestroyInstance(m_instance, nullptr);

    SDL_DestroyWindow(window);
  }
  loaded_engine = nullptr;
}

void Engine::draw() {
  // Empty.
}

void Engine::run() {
  SDL_Event e;
  bool b_quit = false;

  while (!b_quit) {
    // Handle events on queue
    while (SDL_PollEvent(&e) != 0) {
      // Close the window when alt-f4 or the X button.
      if (e.type == SDL_EVENT_QUIT)
        b_quit = true;

      if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) {
        if (e.window.type == SDL_EVENT_WINDOW_MINIMIZED) {
          stop_rendering = true;
        }
        if (e.window.type == SDL_EVENT_WINDOW_RESTORED) {
          stop_rendering = false;
        }
      }
    }

    // Do not draw if we are minimized.
    if (stop_rendering) {
      // Throttle the speed to avoid the endless spinning.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    draw();
  }
}

void Engine::initVulkan() {
  fmt::print("init vulkan\n");
  vkb::InstanceBuilder builder;
  auto inst_ret = builder.set_app_name("Example Vulkan Application")
                      .request_validation_layers(bUseValidationLayers)
                      .use_default_debug_messenger()
                      .require_api_version(1, 3, 0)
                      .build();

  vkb::Instance vkb_inst = inst_ret.value();

  m_instance = vkb_inst.instance;
  m_debug_msngr = vkb_inst.debug_messenger;

  SDL_Vulkan_CreateSurface(window, m_instance, NULL, &m_surface);

  // Vulkan 1.3 features.
  VkPhysicalDeviceVulkan13Features features13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.dynamicRendering = true;
  features13.synchronization2 = true;

  // Vulkan 1.2 features.
  VkPhysicalDeviceVulkan12Features features12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  features12.bufferDeviceAddress = true;
  features12.descriptorIndexing = true;

  // Select a gpu.
  // We want a gpu that can write to the SDL surface and supports vulkan 1.3
  // with the correct features
  vkb::PhysicalDeviceSelector selector{vkb_inst};
  vkb::PhysicalDevice physical_device =
      selector.set_minimum_version(1, 3)
          .set_required_features_13(features13)
          .set_required_features_12(features12)
          .set_surface(m_surface)
          .select()
          .value();

  // Final vulkan device.
  vkb::DeviceBuilder device_builder{physical_device};

  vkb::Device vkb_device = device_builder.build().value();

  // Get the VkDevice handle for the rest of a vulkan application.
  m_device = vkb_device.device;
  m_chosen_GPU = physical_device.physical_device;

  m_graphic_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
  m_graphic_queue_family =
      vkb_device.get_queue_index(vkb::QueueType::graphics).value();
}
void Engine::initSwapchain() {
  fmt::print("init swapchain\n");
  createSwapchain(window_extent.width, window_extent.height);
}
void Engine::initCommands() {
  fmt::print("init commands\n");
  // create a command pool for commands submitted to the graphics queue.
  // we also want the pool to allow for resetting of individual command buffers
  VkCommandPoolCreateInfo cmd_pool_info = vkinit::cmdPoolCreateInfo(
      m_graphic_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  for (uint32_t i = 0; i < kFrameOverlap; i++) {

    VK_CHECK(vkCreateCommandPool(m_device, &cmd_pool_info, nullptr,
                                 &m_frames[i].cmd_pool));

    // allocate the default command buffer that we will use for rendering
    VkCommandBufferAllocateInfo cmd_alloc_info =
        vkinit::cmdBufferAlloInfo(m_frames[i].cmd_pool, 1);

    VK_CHECK(vkAllocateCommandBuffers(m_device, &cmd_alloc_info,
                                      &m_frames[i].cmd_buffer_main));
  }
}
void Engine::initSyncStructures() { fmt::print("init sync structures\n"); }
void Engine::createSwapchain(int w, int h) {
  vkb::SwapchainBuilder swapchainBuilder{m_chosen_GPU, m_device, m_surface};

  m_swapchain_img_format = VK_FORMAT_B8G8R8A8_UNORM;

  vkb::Swapchain vkbSwapchain =
      swapchainBuilder
          //.use_default_format_selection()
          .set_desired_format(VkSurfaceFormatKHR{
              .format = m_swapchain_img_format,
              .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
          // use vsync present mode
          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
          .set_desired_extent(w, h)
          .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
          .build()
          .value();

  m_swapchain_extent = vkbSwapchain.extent;
  m_swapchain = vkbSwapchain.swapchain;
  m_swapchain_imgs = vkbSwapchain.get_images().value();
  m_swapchain_img_views = vkbSwapchain.get_image_views().value();
}
void Engine::destroySwapchain() {
  // Images are deleted here.
  vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
  for (size_t i = 0; i < m_swapchain_img_views.size(); i++) {
    vkDestroyImageView(m_device, m_swapchain_img_views[i], nullptr);
  }
}