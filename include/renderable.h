#pragma once

#include "vk_loader.h"
#include "vk_types.h"

struct RenderObject {
  uint32_t n_index;
  uint32_t first_index;
  VkBuffer index_buffer;
  MaterialInstance *material;
  glm::mat4 transform;
  VkDeviceAddress vertex_buffer_address;
};

struct DrawContext {
  std::vector<RenderObject> opaque_surfaces;
};
struct IRenderable {
  virtual ~IRenderable() {}
  virtual void draw(const glm::mat4 &top_matrix, DrawContext &context) = 0;
};

struct Node : public IRenderable {
  std::weak_ptr<Node> parent; // Avoid circular dependence.
  std::vector<std::shared_ptr<Node>> children;

  glm::mat4 transform_local;
  glm::mat4 transform_world;

  void updateTransform(const glm::mat4 &parent_matrix) {
    transform_world = parent_matrix * transform_local;
    for (auto c : children) {
      c->updateTransform(transform_world);
    }
  }

  virtual void draw(const glm::mat4 &top_matrix,
                    DrawContext &context) override {
    for (const auto &c : children)
      c->draw(top_matrix, context);
  }
  virtual ~Node() {}
};

struct MeshNode : public Node {
  std::shared_ptr<MeshAsset> mesh;
  /**
   * @note  Not drawing to screen, just "draw" to the context,
   *        will be submit to Vulkan later.
   *
   * @param top_matrix
   * @param context
   */
  virtual void draw(const glm::mat4 &top_matrix,
                    DrawContext &context) override {
    glm::mat4 node_matrix = top_matrix * transform_world;
    for (auto &s : mesh->surfaces) {
      RenderObject surface;
      surface.first_index = s.start_index;
      surface.n_index = s.count;
      surface.index_buffer = mesh->mesh_buffers.index_buffer.buffer;
      surface.material = &s.material->data;
      surface.transform = node_matrix;
      surface.vertex_buffer_address = mesh->mesh_buffers.vertex_buffer_address;

      context.opaque_surfaces.push_back(surface);
    }
    Node::draw(top_matrix, context);
  }
};