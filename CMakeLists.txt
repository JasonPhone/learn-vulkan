cmake_minimum_required(VERSION 3.5.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(learn-vulkan)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow -Wnon-virtual-dtor -pedantic")
# -fsanitize=address")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set(CMAKE_BUILD_RPATH ${CMAKE_SOURCE_DIR}/bin/)
# set(CMAKE_INSTALL_RPATH ${CMAKE_SOURCE_DIR}/bin/)

include_directories(
  ./extern/GLFW/include
  ./extern/SLD3/include
  ./include
  ./src
)

add_subdirectory(./src)
add_subdirectory(./apps)
# add_subdirectory(./extern/ImGui)
