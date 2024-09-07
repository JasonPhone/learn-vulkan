#define VMA_IMPLEMENTATION
#include "engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_images.h"
#include "vk_pipelines.h"
#include <VkBootstrap.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <chrono>
#include <thread>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

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

  initShaderDescriptors();
  initPipelines();

  initImGui();

  initDefaultMesh();

  is_initialized = true;
}

void Engine::cleanup() {
  if (is_initialized) {
    // Order matters, reversed of initialization.
    vkDeviceWaitIdle(m_device); // Wait for GPU to finish.
    m_main_deletion_queue.flush();
    for (uint32_t i = 0; i < kFrameOverlap; i++) {
      // Cmd buffer is destroyed with pool it comes from.
      vkDestroyCommandPool(m_device, m_frames[i].cmd_pool, nullptr);
      vkDestroyFence(m_device, m_frames[i].render_fence, nullptr);
      vkDestroySemaphore(m_device, m_frames[i].render_semaphore, nullptr);
      vkDestroySemaphore(m_device, m_frames[i].swapchain_semaphore, nullptr);
    }

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
  // Free objects dedicated to this frame (in last iteration).
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

  m_draw_extent.width = m_color_image.extent.width;
  m_draw_extent.height = m_color_image.extent.height;
  VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));
  { // Drawing commands.
    vkutil::transitionImage(cmd, m_color_image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL);
    drawBackground(cmd);
    vkutil::transitionImage(cmd, m_color_image.image, VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transitionImage(cmd, m_depth_image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    drawGeometry(cmd);
    // Copy to swapchain.
    vkutil::transitionImage(cmd, m_color_image.image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transitionImage(cmd, m_swapchain_imgs[swapchain_img_idx],
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkutil::copyImage(cmd, m_color_image.image,
                      m_swapchain_imgs[swapchain_img_idx], m_draw_extent,
                      m_swapchain_extent);
    vkutil::transitionImage(cmd, m_swapchain_imgs[swapchain_img_idx],
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    // GUI drawing.
    drawImGui(cmd, m_swapchain_img_views[swapchain_img_idx]);
    vkutil::transitionImage(cmd, m_swapchain_imgs[swapchain_img_idx],
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
  VkPresentInfoKHR present_info = vkinit::presentInfo();
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
  auto &background = m_compute_pipelines[m_cur_comp_pipeline_idx];
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, background.pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          m_compute_pipeline_layout, 0, 1, &m_draw_image_ds, 0,
                          nullptr);
  vkCmdPushConstants(cmd, m_compute_pipeline_layout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(ComputePushConstants), &background.data);
  vkCmdDispatch(cmd, std::ceil(m_draw_extent.width / 16.f),
                std::ceil(m_draw_extent.height / 16.f), 1);
}
void Engine::drawGeometry(VkCommandBuffer cmd) {
  VkRenderingAttachmentInfo color_attach = vkinit::attachmentInfo(
      m_color_image.view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  VkRenderingAttachmentInfo depth_attach = vkinit::depthAttachmentInfo(
      m_depth_image.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
  VkRenderingInfo i_render =
      vkinit::renderingInfo(m_draw_extent, &color_attach, &depth_attach);
  vkCmdBeginRendering(cmd, &i_render);
  {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_simple_mesh_pipeline);
    VkViewport view_port = {.x = 0, .y = 0, .minDepth = 0.f, .maxDepth = 1.f};
    view_port.width = m_draw_extent.width;
    view_port.height = m_draw_extent.height;
    vkCmdSetViewport(cmd, 0, 1, &view_port);
    VkRect2D scissor = {};
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = m_draw_extent.width;
    scissor.extent.height = m_draw_extent.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    GPUDrawPushConstants push_constants;
    push_constants.vertex_buffer_address =
        m_meshes[2]->mesh_buffers.vertex_buffer_address;
    glm::mat4 view = glm::rotate(glm::radians(-40.f), glm::vec3{1.f, 0.f, 0.f});
    view = glm::translate(glm::vec3{0, 0, -3}) * view;
    glm::mat4 projection = glm::perspective(
        glm::radians(70.f), 1.f * m_draw_extent.width / m_draw_extent.height,
        // 10000.f, 0.1f);
        0.1f, 10000.f);
    projection[1][1] *= -1; // Reverse Y-axis.
    push_constants.world_mat = projection * view;
    vkCmdPushConstants(cmd, m_simple_mesh_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(GPUDrawPushConstants), &push_constants);
    vkCmdBindIndexBuffer(cmd, m_meshes[2]->mesh_buffers.index_buffer.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m_meshes[2]->surfaces[0].count, 1,
                     m_meshes[2]->surfaces[0].start_index, 0, 0);
  }

  vkCmdEndRendering(cmd);
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
      ImGui_ImplSDL3_ProcessEvent(&e);
    }
    // Do not draw if we are minimized.
    if (stop_rendering) {
      // Throttle the speed to avoid the endless spinning.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    // UI update.
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    // UI layout.
    {
      if (ImGui::Begin("background")) {
        auto &selected_pipeline = m_compute_pipelines[m_cur_comp_pipeline_idx];
        ImGui::Text("Selected Compute Pipeline: %s", selected_pipeline.name);
        ImGui::SliderInt("Effect Index", &m_cur_comp_pipeline_idx, 0,
                         m_compute_pipelines.size() - 1);

        ImGui::InputFloat4("data1", (float *)&selected_pipeline.data.data1);
        ImGui::InputFloat4("data2", (float *)&selected_pipeline.data.data2);
        ImGui::InputFloat4("data3", (float *)&selected_pipeline.data.data3);
        ImGui::InputFloat4("data4", (float *)&selected_pipeline.data.data4);
      }
      ImGui::End();
    }
    ImGui::Render();
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
  VkExtent3D color_img_ext = {window_extent.width, window_extent.height, 1};
  m_color_image.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  m_color_image.extent = color_img_ext;
  VkImageUsageFlags color_img_usage_flags = {};
  // Copy from and into.
  color_img_usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  color_img_usage_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  // Enable compute shader writing.
  color_img_usage_flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  // Graphics pipelines draw.
  color_img_usage_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkImageCreateInfo ci_color_img = vkinit::imageCreateInfo(
      m_color_image.format, color_img_usage_flags, m_color_image.extent);
  VmaAllocationCreateInfo ci_color_img_alloc = {};
  ci_color_img_alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  // Double check the allocation is in VRAM.
  ci_color_img_alloc.requiredFlags =
      VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vmaCreateImage(m_allocator, &ci_color_img, &ci_color_img_alloc,
                 &m_color_image.image, &m_color_image.allocation, nullptr);
  VkImageViewCreateInfo ci_color_img_view = vkinit::imageViewCreateInfo(
      m_color_image.format, m_color_image.image, VK_IMAGE_ASPECT_COLOR_BIT);
  VK_CHECK(vkCreateImageView(m_device, &ci_color_img_view, nullptr,
                             &m_color_image.view));

  VkExtent3D depth_img_ext = {window_extent.width, window_extent.height, 1};
  m_depth_image.format = VK_FORMAT_D32_SFLOAT;
  m_depth_image.extent = depth_img_ext;
  VkImageUsageFlags draw_depth_usage_flags = {};
  draw_depth_usage_flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  VkImageCreateInfo ci_depth_image = vkinit::imageCreateInfo(
      m_depth_image.format, draw_depth_usage_flags, m_depth_image.extent);
  VmaAllocationCreateInfo ci_depth_img_alloc = {};
  ci_depth_img_alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  // Double check the allocation is in VRAM.
  ci_depth_img_alloc.requiredFlags =
      VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  vmaCreateImage(m_allocator, &ci_depth_image, &ci_depth_img_alloc,
                 &m_depth_image.image, &m_depth_image.allocation, nullptr);
  VkImageViewCreateInfo ci_depth_view = vkinit::imageViewCreateInfo(
      m_depth_image.format, m_depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT);
  VK_CHECK(vkCreateImageView(m_device, &ci_depth_view, nullptr,
                             &m_depth_image.view));

  m_main_deletion_queue.push([&]() {
    vkDestroyImageView(m_device, m_color_image.view, nullptr);
    vmaDestroyImage(m_allocator, m_color_image.image, m_color_image.allocation);

    vkDestroyImageView(m_device, m_depth_image.view, nullptr);
    vmaDestroyImage(m_allocator, m_depth_image.image, m_depth_image.allocation);
  });
}
void Engine::initCommands() {
  fmt::print("init commands\n");
  // Create a command pool for commands submitted to the graphics queue.
  // We also want the pool to allow for resetting of individual command buffers
  VkCommandPoolCreateInfo cmd_pool_info = vkinit::cmdPoolCreateInfo(
      m_graphic_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  for (uint32_t i = 0; i < kFrameOverlap; i++) {
    VK_CHECK(vkCreateCommandPool(m_device, &cmd_pool_info, nullptr,
                                 &m_frames[i].cmd_pool));
    VkCommandBufferAllocateInfo cmd_alloc_info =
        vkinit::cmdBufferAllocInfo(m_frames[i].cmd_pool, 1);
    VK_CHECK(vkAllocateCommandBuffers(m_device, &cmd_alloc_info,
                                      &m_frames[i].cmd_buffer_main));
  }

  // Init immediate cmd.
  VK_CHECK(
      vkCreateCommandPool(m_device, &cmd_pool_info, nullptr, &m_imm_cmd_pool));
  VkCommandBufferAllocateInfo cmd_alloc_info =
      vkinit::cmdBufferAllocInfo(m_imm_cmd_pool, 1);
  VK_CHECK(vkAllocateCommandBuffers(m_device, &cmd_alloc_info, &m_imm_cmd));
  m_main_deletion_queue.push(
      [&]() { vkDestroyCommandPool(m_device, m_imm_cmd_pool, nullptr); });
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
  VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_imm_fence));
  m_main_deletion_queue.push(
      [&]() { vkDestroyFence(m_device, m_imm_fence, nullptr); });
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
void Engine::initShaderDescriptors() {
  std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
  // Descriptor pool with 10 des sets, 1 image each.
  m_global_ds_allocator.initPool(m_device, 10, sizes);
  // Init the layout and get descriptor set.
  {
    DescriptorLayoutBuilder builder;
    builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    m_draw_image_ds_layout =
        builder.build(m_device, VK_SHADER_STAGE_COMPUTE_BIT);
    m_draw_image_ds =
        m_global_ds_allocator.allocate(m_device, m_draw_image_ds_layout);
  }

  VkDescriptorImageInfo img_info = {};
  img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  img_info.imageView = m_color_image.view; // Here connect the data stream.

  VkWriteDescriptorSet draw_img_write = {};
  draw_img_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  draw_img_write.pNext = nullptr;
  draw_img_write.dstBinding = 0;
  draw_img_write.dstSet = m_draw_image_ds;
  draw_img_write.descriptorCount = 1;
  draw_img_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  draw_img_write.pImageInfo = &img_info;

  vkUpdateDescriptorSets(m_device, 1, &draw_img_write, 0, nullptr);

  m_main_deletion_queue.push([&]() {
    m_global_ds_allocator.destroyPool(m_device);
    vkDestroyDescriptorSetLayout(m_device, m_draw_image_ds_layout, nullptr);
  });
}
void Engine::initPipelines() {
  // Compute pipelines.
  initBackgroundPipelines();
  // Graphics pipelines.
  initSimpleMeshPipeline();
}
void Engine::initBackgroundPipelines() {
  // Create pipeline layout.
  VkPipelineLayoutCreateInfo comp_layout = {};
  comp_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  comp_layout.pNext = nullptr;
  comp_layout.pSetLayouts = &m_draw_image_ds_layout;
  comp_layout.setLayoutCount = 1;
  VkPushConstantRange push_range = {};
  push_range.offset = 0;
  push_range.size = sizeof(ComputePushConstants);
  push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  comp_layout.pPushConstantRanges = &push_range;
  comp_layout.pushConstantRangeCount = 1;
  VK_CHECK(vkCreatePipelineLayout(m_device, &comp_layout, nullptr,
                                  &m_compute_pipeline_layout));

  // Load shader data.
  VkShaderModule gradient_shader;
  if (!vkutil::loadShaderModule("../../assets/shaders/gradient_color.comp.spv",
                                m_device, &gradient_shader)) {
    fmt::println("Error building compute shader.");
  }
  VkShaderModule sky_shader;
  if (!vkutil::loadShaderModule("../../assets/shaders/sky.comp.spv", m_device,
                                &sky_shader)) {
    fmt::println("Error building compute shader.");
  }
  // Fill shader stage info.
  VkPipelineShaderStageCreateInfo stage_info = {};
  stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage_info.pNext = nullptr;
  stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage_info.module = gradient_shader;
  stage_info.pName = "main";
  // Create pipeline.
  VkComputePipelineCreateInfo comp_pipeline_info = {};
  comp_pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  comp_pipeline_info.pNext = nullptr;
  comp_pipeline_info.layout = m_compute_pipeline_layout;
  comp_pipeline_info.stage = stage_info;
  ComputePipeline gradient;
  gradient.layout = m_compute_pipeline_layout;
  gradient.name = "gradient";
  gradient.data = {};
  gradient.data.data1 = glm::vec4{1, 0, 0, 1};
  gradient.data.data2 = glm::vec4{0, 0, 1, 1};
  VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1,
                                    &comp_pipeline_info, nullptr,
                                    &gradient.pipeline));
  m_compute_pipelines.push_back(gradient);

  stage_info.module = sky_shader;
  ComputePipeline sky;
  sky.layout = m_compute_pipeline_layout;
  sky.name = "sky";
  sky.data = {};
  sky.data.data1 = glm::vec4{0.1, 0.2, 0.4, 0.97};
  VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1,
                                    &comp_pipeline_info, nullptr,
                                    &sky.pipeline));
  m_compute_pipelines.push_back(sky);

  // Shader is already in the pipeline.
  vkDestroyShaderModule(m_device, gradient_shader, nullptr);
  vkDestroyShaderModule(m_device, sky_shader, nullptr);
  m_main_deletion_queue.push([&]() {
    vkDestroyPipelineLayout(m_device, m_compute_pipeline_layout, nullptr);
    for (auto &&pipeline : m_compute_pipelines) {
      vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
    }
  });
}
void Engine::initSimpleMeshPipeline() {
  VkShaderModule mesh_shader_vert;
  VkShaderModule mesh_shader_frag;
  if (!vkutil::loadShaderModule("../../assets/shaders/simple_mesh.vert.spv",
                                m_device, &mesh_shader_vert)) {
    fmt::println("Error loading vert shader.");
  }
  if (!vkutil::loadShaderModule("../../assets/shaders/simple_mesh.frag.spv",
                                m_device, &mesh_shader_frag)) {
    fmt::println("Error loading frag shader.");
  }
  VkPushConstantRange buffer_range = {};
  buffer_range.offset = 0;
  buffer_range.size = sizeof(GPUDrawPushConstants);
  buffer_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  VkPipelineLayoutCreateInfo ci_pipeline_layout =
      vkinit::pipelineLayoutCreateInfo();
  ci_pipeline_layout.pPushConstantRanges = &buffer_range;
  ci_pipeline_layout.pushConstantRangeCount = 1;
  VK_CHECK(vkCreatePipelineLayout(m_device, &ci_pipeline_layout, nullptr,
                                  &m_simple_mesh_pipeline_layout));
  PipelineBuilder builder;
  builder.pipeline_layout = m_simple_mesh_pipeline_layout;
  builder.setShaders(mesh_shader_vert, mesh_shader_frag);
  builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  builder.setPolygonMode(VK_POLYGON_MODE_FILL);
  builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  builder.setMultisamplingNone();
  builder.disableBlending();
  builder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
  builder.setColorAttachFormat(m_color_image.format);
  builder.setDepthFormat(m_depth_image.format);
  m_simple_mesh_pipeline = builder.buildPipeline(m_device);

  vkDestroyShaderModule(m_device, mesh_shader_vert, nullptr);
  vkDestroyShaderModule(m_device, mesh_shader_frag, nullptr);
  m_main_deletion_queue.push([&]() {
    vkDestroyPipelineLayout(m_device, m_simple_mesh_pipeline_layout, nullptr);
    vkDestroyPipeline(m_device, m_simple_mesh_pipeline, nullptr);
  });
}

void Engine::immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&func) {
  VK_CHECK(vkResetFences(m_device, 1, &m_imm_fence));
  VK_CHECK(vkResetCommandBuffer(m_imm_cmd, 0));

  VkCommandBuffer cmd = m_imm_cmd;

  VkCommandBufferBeginInfo cmdBeginInfo =
      vkinit::cmdBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

  func(cmd);

  VK_CHECK(vkEndCommandBuffer(cmd));

  VkCommandBufferSubmitInfo cmd_info = vkinit::cmdBufferSubmitInfo(cmd);
  VkSubmitInfo2 submit = vkinit::submitInfo(&cmd_info, nullptr, nullptr);

  // Submit command buffer to the queue and execute it.
  // imm_fence will now block until the graphic commands finish execution.
  VK_CHECK(vkQueueSubmit2(m_graphic_queue, 1, &submit, m_imm_fence));

  VK_CHECK(vkWaitForFences(m_device, 1, &m_imm_fence, true, VK_ONE_SEC));
}

void Engine::initImGui() {
  // 1: create descriptor pool for IMGUI.
  // Sizes are oversize, but copied from imgui demo itself.
  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100}};

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 100;
  pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;

  VkDescriptorPool imgui_pool;
  VK_CHECK(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &imgui_pool));

  // 2: initialize imgui library
  // Init the core structures of imgui
  ImGui::CreateContext();
  // Init imgui for SDL
  ImGui_ImplSDL3_InitForVulkan(window);
  // Init info of imgui for Vulkan
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = m_instance;
  init_info.PhysicalDevice = m_chosen_GPU;
  init_info.Device = m_device;
  init_info.Queue = m_graphic_queue;
  init_info.DescriptorPool = imgui_pool;
  init_info.MinImageCount = 3;
  init_info.ImageCount = 3;
  init_info.UseDynamicRendering = true;
  // Dynamic rendering parameters for imgui to use.
  init_info.PipelineRenderingCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  // GUI is drawn directly into swapchain.
  init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats =
      &m_swapchain_img_format;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_Init(&init_info);
  ImGui_ImplVulkan_CreateFontsTexture();
  // Pool value changes if capture using reference,
  // will cause validation layer to report.
  m_main_deletion_queue.push([this, imgui_pool]() {
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(m_device, imgui_pool, nullptr);
  });
}
void Engine::drawImGui(VkCommandBuffer cmd, VkImageView target_img_view) {
  VkRenderingAttachmentInfo color_attachment = vkinit::attachmentInfo(
      target_img_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  VkRenderingInfo render_info =
      vkinit::renderingInfo(m_swapchain_extent, &color_attachment, nullptr);

  vkCmdBeginRendering(cmd, &render_info);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
  vkCmdEndRendering(cmd);
}
AllocatedBuffer Engine::createBuffer(size_t alloc_size,
                                     VkBufferUsageFlags usage,
                                     VmaMemoryUsage mem_usage) {
  VkBufferCreateInfo ci_buffer = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
  };
  ci_buffer.size = alloc_size;
  ci_buffer.usage = usage;
  VmaAllocationCreateInfo ci_alloc = {};
  ci_alloc.usage = mem_usage;
  ci_alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
  AllocatedBuffer buffer;
  VK_CHECK(vmaCreateBuffer(m_allocator, &ci_buffer, &ci_alloc, &buffer.buffer,
                           &buffer.allocation, &buffer.alloc_info));
  return buffer;
}
void Engine::destroyBuffer(const AllocatedBuffer &buffer) {
  vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
}
GPUMeshBuffers Engine::uploadMesh(std::span<uint32_t> indices,
                                  std::span<Vertex> vertices) {
  const size_t kVertexBufferSize = vertices.size() * sizeof(Vertex);
  const size_t kIndexBufferSize = indices.size() * sizeof(uint32_t);

  GPUMeshBuffers mesh;
  mesh.vertex_buffer = createBuffer(
      kVertexBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY);
  VkBufferDeviceAddressInfo i_device_address{
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = mesh.vertex_buffer.buffer,
  };
  mesh.vertex_buffer_address =
      vkGetBufferDeviceAddress(m_device, &i_device_address);

  mesh.index_buffer = createBuffer(kIndexBufferSize,
                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VMA_MEMORY_USAGE_GPU_ONLY);

  // Write data into a CPU-only staging buffer, then upload to GPU-only buffer.
  AllocatedBuffer staging =
      createBuffer(kVertexBufferSize + kIndexBufferSize,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
  void *data = staging.allocation->GetMappedData();
  memcpy(data, vertices.data(), kVertexBufferSize);
  memcpy((char *)data + kVertexBufferSize, indices.data(), kIndexBufferSize);
  immediateSubmit([&](VkCommandBuffer cmd) {
    VkBufferCopy vertex_copy = {};
    vertex_copy.dstOffset = 0;
    vertex_copy.srcOffset = 0;
    vertex_copy.size = kVertexBufferSize;
    vkCmdCopyBuffer(cmd, staging.buffer, mesh.vertex_buffer.buffer, 1,
                    &vertex_copy);
    VkBufferCopy index_copy = {};
    index_copy.dstOffset = 0;
    index_copy.srcOffset = kVertexBufferSize;
    index_copy.size = kIndexBufferSize;
    vkCmdCopyBuffer(cmd, staging.buffer, mesh.index_buffer.buffer, 1,
                    &index_copy);
  });
  destroyBuffer(staging);
  return mesh;
}

void Engine::initDefaultMesh() {
  std::array<Vertex, 4> rect_vertices;
  rect_vertices[0].position = {0.5, -0.5, 0};
  rect_vertices[1].position = {0.5, 0.5, 0};
  rect_vertices[2].position = {-0.5, -0.5, 0};
  rect_vertices[3].position = {-0.5, 0.5, 0};
  rect_vertices[0].color = {0.8, 0.2, 0.2, 1};
  rect_vertices[1].color = {0.2, 0.8, 0.2, 1};
  rect_vertices[2].color = {0.2, 0.2, 0.8, 1};
  rect_vertices[3].color = {0.8, 0.8, 0.8, 1};

  std::array<uint32_t, 6> rect_indices;
  rect_indices[0] = 0;
  rect_indices[1] = 1;
  rect_indices[2] = 2;
  rect_indices[3] = 2;
  rect_indices[4] = 1;
  rect_indices[5] = 3;

  m_simple_mesh = uploadMesh(rect_indices, rect_vertices);
  m_main_deletion_queue.push([&]() {
    destroyBuffer(m_simple_mesh.index_buffer);
    destroyBuffer(m_simple_mesh.vertex_buffer);
  });

  m_meshes = loadGltfMeshes(this, "../../assets/models/basic_mesh.glb").value();
  for (auto &&mesh : m_meshes) {
    m_main_deletion_queue.push([&]() {
      destroyBuffer(mesh->mesh_buffers.vertex_buffer);
      destroyBuffer(mesh->mesh_buffers.index_buffer);
    });
  }
}