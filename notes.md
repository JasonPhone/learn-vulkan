# 0.1 Intro

## 前置要求

- 一张兼容 Vulkan 的显卡和对应驱动
- 会 C++（RAII 和初始化列表）
- 支持 C++17 的编译器
- 图形学知识（教程里默认掌握基础概念）

## 教程

基于 [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/)、[GLM 数学库](https://github.com/g-truc/glm)和 [GLFW 窗口管理库](https://www.glfw.org/)。

# Issues

ImGui still needs dynamic rendering now?

How we pass data to and from shader?

Scene structure, material model and shading model are all to be explored.

# Notes

DescriptorPool for ImGui may *change*, the destroy callback should use value capture.

vma causes too much compile warning, suppressed using `#pragma clang diagnostic` around header.

Using dynamic rendering instead of `VkRenderPass`. May not work on mobile device where tile rendering is common.

<!-- TODO Try impl reversed-z: projection mat, depth compare operator, depth attachment clear value. -->
Reversed-z takes depth value 1(INF in glm::perspective()) as near plane and 0 as far.
Can mitigate z-fighting because
1) objects are "pushed back" to far plane through perspective projection;
2) IEEE754 float value has higher precision when its abs is small.

By now (1419b16) the descriptor set is used to bind output image of compute shader.

