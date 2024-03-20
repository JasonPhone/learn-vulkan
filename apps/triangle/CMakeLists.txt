project("triangle")

find_package(glfw3 REQUIRED)

find_package(Vulkan REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} glfw vulkan)