#pragma once
#include "vk_types.h"

class Engine : public ObjectBase {
public:
  Engine() = delete;
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
};