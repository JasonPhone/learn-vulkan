#pragma once
#include "vk_types.h"
#include "vk_descriptors.h"

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
  std::vector<RenderObject> transparent_surfaces;
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

  void updateTransform(const glm::mat4 &parent_matrix);

  virtual void draw(const glm::mat4 &top_matrix, DrawContext &context) override;
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
  virtual void draw(const glm::mat4 &top_matrix, DrawContext &context) override;
};
struct LoadedGLTF : public IRenderable {
  // Storage all the data on a given glTF file.
  std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
  std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
  std::unordered_map<std::string, AllocatedImage> images;
  std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

  // Nodes having no parent, for iterating through the file in tree order.
  std::vector<std::shared_ptr<Node>> top_nodes;
  std::vector<VkSampler> samplers;
  DescriptorAllocator descriptor_pool;
  AllocatedBuffer material_data_buffer;
  Engine *creator;

  ~LoadedGLTF() { clearAll(); };

  virtual void draw(const glm::mat4 &top_mat, DrawContext &context) override;

private:
  void clearAll();
};