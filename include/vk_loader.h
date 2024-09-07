#pragma once
#include "vk_types.h"
#include <unordered_map>
#include <filesystem>

struct GeometrySurface {
  uint32_t start_index;
  uint32_t count;
};

struct GeometryMesh {
  std::string name;
  std::vector<GeometrySurface> surfaces;
  GPUMeshBuffers mesh_buffers;
};

class Engine;

std::optional<std::vector<std::shared_ptr<GeometryMesh>>>
loadGltfMeshes(Engine *engine, std::filesystem::path file_path);