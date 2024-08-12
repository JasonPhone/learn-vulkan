add_subdirectory(tinyobjloader)
add_subdirectory(SDL EXCLUDE_FROM_ALL)
add_subdirectory(vk-bootstrap)

include(imgui.cmake)
include(vma.cmake)
include(stb.cmake)