#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include "shaders/UniformParams.h"
#include <chrono>

#include "wsi/OsWindowingManager.hpp"


class App
{
public:
  App();
  ~App();

  void run();
  void load_textures(vk::CommandBuffer& currentCmdBuf);

private:
  void drawFrame();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  UniformParams uniformParams;
  static const uint32_t numFramesInFlight = 3;
  std::array<std::optional<etna::GpuSharedResource<etna::Buffer>>, numFramesInFlight> gpuSharedResource;
  uint32_t frameCount = 0;

  etna::GraphicsPipeline pipeline;
  etna::GraphicsPipeline pipeline_texture;
  etna::Image shader_image;
  etna::Image texture_image;
  glm::uvec2 texture_resolution;
  bool load_textures_done = false;
  etna::Sampler sampler;
  glm::vec2 mouse_pos;
  std::chrono::time_point<std::chrono::system_clock> init_time = std::chrono::system_clock::now();
};
