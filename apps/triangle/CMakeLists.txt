project("triangle")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/")

# message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
# message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
# message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
# set(glfw3_DIR "${CMAKE_SOURCE_DIR}/lib/cmake/glfw3")
# find_package(glfw3 CONFIG REQUIRED)
if(UNIX)
  message(STATUS "Running on Unix.")

  find_package(glfw3 REQUIRED)
  find_package(Vulkan REQUIRED)
  add_executable(${PROJECT_NAME} main.cpp)
  target_link_libraries(${PROJECT_NAME} glfw vulkan)
elseif(WIN32)
  message(STATUS "Running on Windows.")

  set(ENV{VULKAN_SDK} "C:/VulkanSDK/1.3.290.0")
  find_package(Vulkan REQUIRED)
  include_directories(${Vulkan_INCLUDE_DIRS})

  add_executable(${PROJECT_NAME} main.cpp)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/lib/libglfw3.a"
    ${Vulkan_LIBRARIES}
  )
else()
  message(FATAL_ERROR "Unknown host system.")
endif()
