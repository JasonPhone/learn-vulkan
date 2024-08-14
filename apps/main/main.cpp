#include "engine.h"
// #include "vk_types.h"
#include <iostream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char *argv[]) {
  Engine engine{};
  std::cout << "init" << std::endl;
  engine.init();
  fmt::print("run\n");
  engine.run();
  fmt::print("cleanup\n");
  engine.cleanup();

  return 0;
}