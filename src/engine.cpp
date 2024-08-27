#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include "engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_images.h"
#include <VkBootstrap.h>

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
    for (uint32_t i = 0; i < kFrameOverlap; i++) {
      // Cmd buffer is destroyed with pool it comes from.
      vkDestroyCommandPool(m_device, m_frames[i].cmd_pool, nullptr);

      vkDestroyFence(m_device, m_frames[i].render_fence, nullptr);
      vkDestroySemaphore(m_device, m_frames[i].render_semaphore, nullptr);
      vkDestroySemaphore(m_device, m_frames[i].swapchain_semaphore, nullptr);
    }
    m_main_deletion_queue.flush();

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
  VK_CHECK(vkWaitForFences(m_device, 1, &getCurrentFrame().render_fence, true,
                           VK_ONE_SEC));
  // Free objects dedicated to this frame (in last iteraton).
  getCurrentFrame().deletion_queue.flush();
  VK_CHECK(vkResetFences(m_device, 1, &getCurrentFrame().render_fence));

  // Request an image to draw to.
  uint32_t swapchain_img_idx;
  // Will signal the semaphore.
  VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, VK_ONE_SEC,
                                 getCurrentFrame().swapchain_semaphore, nullptr,
                                 &swapchain_img_idx));

  // Clear the cmd buffer.
  VkCommandBuffer cmd = getCurrentFrame().cmd_buffer_main;
  VK_CHECK(vkResetCommandBuffer(cmd, 0));
  // Record cmd. Bit is to tell vulkan buffer is used exactly once.
  VkCommandBufferBeginInfo cmd_begin_info =
      vkinit::cmdBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  m_draw_extent.width = m_draw_image.image_extent.width;
  m_draw_extent.height = m_draw_image.image_extent.height;
  VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));
  { // Commands: Draw a clear color.
    vkutil::transitionImage(cmd, m_draw_image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL);
    drawBackground(cmd);

    vkutil::transitionImage(cmd, m_draw_image.image, VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transitionImage(cmd, m_swapchain_imgs[swapchain_img_idx],
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkutil::copyImage(cmd, m_draw_image.image,
                      m_swapchain_imgs[swapchain_img_idx], m_draw_extent,
                      m_swapchain_extent);
    vkutil::transitionImage(cmd, m_swapchain_imgs[swapchain_img_idx],
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  }
  VK_CHECK(vkEndCommandBuffer(cmd));

  // Submit commands.
  VkCommandBufferSubmitInfo cmd_submit_info = vkinit::cmdBufferSubmitInfo(cmd);
  VkSemaphoreSubmitInfo wait_info = vkinit::semaphoreSubmitInfo(
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
      getCurrentFrame().swapchain_semaphore);
  VkSemaphoreSubmitInfo signal_info = vkinit::semaphoreSubmitInfo(
      VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, getCurrentFrame().render_semaphore);
  VkSubmitInfo2 submit_info =
      vkinit::submitInfo(&cmd_submit_info, &signal_info, &wait_info);
  VK_CHECK(vkQueueSubmit2(m_graphic_queue, 1, &submit_info,
                          getCurrentFrame().render_fence));

  // Present image.
  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.pNext = nullptr;
  present_info.pSwapchains = &m_swapchain;
  present_info.swapchainCount = 1;
  present_info.pWaitSemaphores = &getCurrentFrame().render_semaphore;
  present_info.waitSemaphoreCount = 1;
  present_info.pImageIndices = &swapchain_img_idx;

  VK_CHECK(vkQueuePresentKHR(m_graphic_queue, &present_info));

  frame_number++;
}
void Engine::drawBackground(VkCommandBuffer cmd) {
  VkClearColorValue clearValue;
  float flash = std::abs(std::sin(frame_number / 120.f));
  clearValue = {{0.0f, 0.0f, flash, 1.0f}};

  VkImageSubresourceRange clearRange =
      vkinit::imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

  // Clear image.
  vkCmdClearColorImage(cmd, m_draw_image.image, VK_IMAGE_LAYOUT_GENERAL,
                       &clearValue, 1, &clearRange);
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

  // Mem allocator.
  VmaAllocatorCreateInfo alloc_info = {};
  alloc_info.physicalDevice = m_chosen_GPU;
  alloc_info.device = m_device;
  alloc_info.instance = m_instance;
  alloc_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  vmaCreateAllocator(&alloc_info, &m_allocator);
  m_main_deletion_queue.push([&]() { vmaDestroyAllocator(m_allocator); });
}
void Engine::initSwapchain() {
  fmt::print("init swapchain\n");
  createSwapchain(window_extent.width, window_extent.height);

  // Custom draw image.
  VkExtent3D draw_img_ext = {window_extent.width, window_extent.height, 1};
  m_draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;
  m_draw_image.image_extent = draw_img_ext;
  VkImageUsageFlags draw_img_usage_flags = {};
  // Copy from and into.
  draw_img_usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  draw_img_usage_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  // Enable computer shader writing.
  draw_img_usage_flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  // Graphics pipelines draw.
  draw_img_usage_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  VkImageCreateInfo img_create_info = vkinit::imageCreateInfo(
      m_draw_image.image_format, draw_img_usage_flags, draw_img_ext);

  VmaAllocationCreateInfo img_alloc_info = {};
  img_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  img_alloc_info.requiredFlags =
      // Double check the allocation is in VRAM.
      VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // allocate and create the image
  vmaCreateImage(m_allocator, &img_create_info, &img_alloc_info,
                 &m_draw_image.image, &m_draw_image.allocation, nullptr);

  // build a image-view for the draw image to use for rendering
  VkImageViewCreateInfo img_view_create_info = vkinit::imageViewCreateInfo(
      m_draw_image.image_format, m_draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);

  VK_CHECK(vkCreateImageView(m_device, &img_view_create_info, nullptr,
                             &m_draw_image.image_view));

  // add to deletion queues
  m_main_deletion_queue.push([&]() {
    vkDestroyImageView(m_device, m_draw_image.image_view, nullptr);
    vmaDestroyImage(m_allocator, m_draw_image.image, m_draw_image.allocation);
  });
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
        vkinit::cmdBufferAllocInfo(m_frames[i].cmd_pool, 1);

    VK_CHECK(vkAllocateCommandBuffers(m_device, &cmd_alloc_info,
                                      &m_frames[i].cmd_buffer_main));
  }
}
void Engine::initSyncStructures() {
  // One fence to control when the gpu has finished rendering the frame.
  // 2 semaphores to synchronize rendering with swapchain.
  fmt::print("init sync structures\n");

  // The fence starts signalled so we can wait on it on the first frame.
  VkFenceCreateInfo fenceCreateInfo =
      vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
  VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphoreCreateInfo();

  for (uint32_t i = 0; i < kFrameOverlap; i++) {
    VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr,
                           &m_frames[i].render_fence));
    VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr,
                               &m_frames[i].swapchain_semaphore));
    VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr,
                               &m_frames[i].render_semaphore));
  }
}
void Engine::createSwapchain(int w, int h) {
  vkb::SwapchainBuilder swapchainBuilder{m_chosen_GPU, m_device, m_surface};

  m_swapchain_img_format = VK_FORMAT_B8G8R8A8_UNORM;

  vkb::Swapchain vkbSwapchain =
      swapchainBuilder
          //.use_default_format_selection()
          .set_desired_format(VkSurfaceFormatKHR{
              .format = m_swapchain_img_format,
              .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
          // use v-sync present mode
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