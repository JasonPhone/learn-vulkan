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

DescriptorPool for ImGui may *change*, the destroy callback should use value capture.

vma causes too much compile warning, suppressed using `#pragma clang diagnostic` around header.