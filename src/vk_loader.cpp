#include <stb_image.h>
#include <iostream>
#include "vk_loader.h"

#include "engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

std::optional<std::vector<std::shared_ptr<MeshAsset>>>
loadGltfMeshes(Engine *engine, std::filesystem::path file_path) {
  fmt::println("Loading GLTF {}", file_path.string());

  fastgltf::Parser parser{};
  auto data = fastgltf::GltfDataBuffer::FromPath(file_path);
  if (data.error() != fastgltf::Error::None) {
    fmt::println("Error loading GLTF from file.");
    return {};
  }

  constexpr auto kGltfOptions = fastgltf::Options::LoadExternalBuffers;

  fastgltf::Asset gltf;

  auto load =
      parser.loadGltfBinary(data.get(), file_path.parent_path(), kGltfOptions);
  if (load) {
    gltf = std::move(load.get());
  } else {
    fmt::println("Failed to load glTF: {} \n",
                 fastgltf::to_underlying(load.error()));
    return {};
  }

  std::vector<std::shared_ptr<MeshAsset>> meshes;
  std::vector<uint32_t> indices;
  std::vector<Vertex> vertices;
  for (fastgltf::Mesh &mesh : gltf.meshes) {
    MeshAsset new_mesh;

    new_mesh.name = mesh.name;

    // clear the mesh arrays each mesh, we dont want to merge them by error
    indices.clear();
    vertices.clear();

    for (auto &&p : mesh.primitives) {
      GeometrySurface newSurface;
      newSurface.start_index = (uint32_t)indices.size();
      newSurface.count =
          (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

      size_t initial_vtx = vertices.size();

      // load indexes
      {
        fastgltf::Accessor &idx_accessor =
            gltf.accessors[p.indicesAccessor.value()];
        indices.reserve(indices.size() + idx_accessor.count);

        fastgltf::iterateAccessor<std::uint32_t>(
            gltf, idx_accessor,
            [&](std::uint32_t idx) { indices.push_back(idx + initial_vtx); });
      }

      // load vertex positions
      {
        fastgltf::Accessor &pos_accessor =
            gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
        vertices.resize(vertices.size() + pos_accessor.count);

        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            gltf, pos_accessor, [&](glm::vec3 v, size_t index) {
              Vertex newvtx;
              newvtx.position = v;
              newvtx.normal = {1, 0, 0};
              newvtx.color = glm::vec4{1.f};
              newvtx.uv_x = 0;
              newvtx.uv_y = 0;
              vertices[initial_vtx + index] = newvtx;
            });
      }

      // load vertex normals
      auto normals = p.findAttribute("NORMAL");
      if (normals != p.attributes.end()) {

        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            gltf, gltf.accessors[(*normals).accessorIndex],
            [&](glm::vec3 v, size_t index) {
              vertices[initial_vtx + index].normal = v;
            });
      }

      // load UVs
      auto uv = p.findAttribute("TEXCOORD_0");
      if (uv != p.attributes.end()) {

        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            gltf, gltf.accessors[(*uv).accessorIndex],
            [&](glm::vec2 v, size_t index) {
              vertices[initial_vtx + index].uv_x = v.x;
              vertices[initial_vtx + index].uv_y = v.y;
            });
      }

      // load vertex colors
      auto colors = p.findAttribute("COLOR_0");
      if (colors != p.attributes.end()) {

        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            gltf, gltf.accessors[(*colors).accessorIndex],
            [&](glm::vec4 v, size_t index) {
              vertices[initial_vtx + index].color = v;
            });
      }
      new_mesh.surfaces.push_back(newSurface);
    }

    // display the vertex normals
    constexpr bool OverrideColors = false;
    if (OverrideColors) {
      for (Vertex &vtx : vertices) {
        vtx.color = glm::vec4(vtx.normal, 1.f);
      }
    }
    new_mesh.mesh_buffers = engine->uploadMesh(indices, vertices);

    meshes.emplace_back(std::make_shared<MeshAsset>(std::move(new_mesh)));
  }
  fmt::println("{} meshes loaded", meshes.size());
  return meshes;
}