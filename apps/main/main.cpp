#include "engine.h"
#include "vk_types.h"

int main() {
  Engine engine;
  engine.init();
  engine.run();
  engine.cleanup();
  return 0;
}