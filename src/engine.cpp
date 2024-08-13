#include "engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include <chrono>
#include <thread>

constexpr bool bUseValidationLayers = false;

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

  // everything went fine
  is_initialized = true;
}

void Engine::cleanup() {
  if (is_initialized)
    SDL_DestroyWindow(window);
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