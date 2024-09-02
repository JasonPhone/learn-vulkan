cmake_minimum_required(VERSION 3.5)

project(vma
  LANGUAGES CXX
  DESCRIPTION "VMA as library."
  HOMEPAGE_URL "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator"
)

add_library(${PROJECT_NAME}
  STATIC
  ./vma.cpp
)

set(ENV{VULKAN_SDK} "C:/VulkanSDK/1.3.290.0")
find_package(Vulkan REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE
  VMA/include
  ${Vulkan_INCLUDE_DIRS}
)

# VMA is too small to avoid warnings.
target_compile_options(${PROJECT_NAME} PRIVATE "-w")
