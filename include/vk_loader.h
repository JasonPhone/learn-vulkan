#pragma once
#include "vk_types.h"
#include <unordered_map>
#include <filesystem>

struct GLTFMaterial {
  MaterialInstance data;
};
struct GeometrySurface {
  uint32_t start_index;
  uint32_t count;
  std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset {
  std::string name;
  std::vector<GeometrySurface> surfaces;
  GPUMeshBuffers mesh_buffers;
};

class Engine;

std::optional<std::vector<std::shared_ptr<MeshAsset>>>
loadGltfMeshes(Engine *engine, std::filesystem::path file_path);