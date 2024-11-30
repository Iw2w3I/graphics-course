#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <glm/ext.hpp>


WorldRenderer::WorldRenderer()
  : sceneMgr{std::make_unique<SceneManager>()}
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  backbuffer = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "backbuffer",
    .format = vk::Format::eB10G11R11UfloatPack32,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage,
  });

  prefsum = ctx.createBuffer({
    .size = 128 * sizeof(int),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "prefsum",
  });

  density = ctx.createBuffer({
    .size = 128 * sizeof(float),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "density",
  });
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectScene(path);
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh_material",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("postprocess", {TONEMAPPING_RENDERER_SHADERS_ROOT "postprocess.comp.spv"});
  etna::create_program("density_hist", {TONEMAPPING_RENDERER_SHADERS_ROOT "density_hist.comp.spv"});
  etna::create_program(
    "tonemapping", 
    {TONEMAPPING_RENDERER_SHADERS_ROOT "tonemapping.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "tonemapping.frag.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  staticMeshPipeline = {};
  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
    "static_mesh_material",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {vk::Format::eB10G11R11UfloatPack32},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
  densityHistPipeline = pipelineManager.createComputePipeline("density_hist", {});
  postprocessPipeline = pipelineManager.createComputePipeline("postprocess", {});
  tonemappingPipeline = pipelineManager.createGraphicsPipeline(
    "tonemapping",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
  sampler = etna::Sampler({
        .filter = vk::Filter::eLinear,
        .name = "sampler",
  });
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
  }
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout)
{
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  pushConst2M.projView = glob_tm;

  auto instanceMeshes = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();

  auto meshes = sceneMgr->getMeshes();
  auto relems = sceneMgr->getRenderElements();

  for (std::size_t instIdx = 0; instIdx < instanceMeshes.size(); ++instIdx)
  {
    pushConst2M.model = instanceMatrices[instIdx];

    cmd_buf.pushConstants<PushConstants>(
      pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {pushConst2M});

    const auto meshIdx = instanceMeshes[instIdx];

    for (std::size_t j = 0; j < meshes[meshIdx].relemCount; ++j)
    {
      const auto relemIdx = meshes[meshIdx].firstRelem + j;
      const auto& relem = relems[relemIdx];
      cmd_buf.drawIndexed(relem.indexCount, 1, relem.indexOffset, relem.vertexOffset, 0);
    }
  }
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);
    {
      // etna::set_state(
      //   cmd_buf, 
      //   backbuffer.get(), 
      //   vk::PipelineStageFlagBits2::eFragmentShader, 
      //   {}, 
      //   vk::ImageLayout::eColorAttachmentOptimal, 
      //   vk::ImageAspectFlagBits::eColor
      // );
      // etna::flush_barriers(cmd_buf);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image, .view = target_image_view, .loadOp = vk::AttachmentLoadOp::eLoad}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({}), .loadOp=vk::AttachmentLoadOp::eLoad});

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
      renderScene(cmd_buf, worldViewProj, staticMeshPipeline.getVkPipelineLayout());
    }
    // postprocess(cmd_buf, target_image, target_image_view);
  }
}

void WorldRenderer::postprocess(vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view) {
    ETNA_PROFILE_GPU(cmd_buf, postprocess);

    if (true)
    {
      return;
      etna::set_state(cmd_buf, 
        target_image, 
        vk::PipelineStageFlagBits2::eTransfer, 
        {}, 
        vk::ImageLayout::eTransferDstOptimal, 
        vk::ImageAspectFlagBits::eColor
      );
      etna::set_state(cmd_buf, 
        backbuffer.get(), 
        vk::PipelineStageFlagBits2::eTransfer, 
        {}, 
        vk::ImageLayout::eTransferSrcOptimal, 
        vk::ImageAspectFlagBits::eColor
      );
      etna::flush_barriers(cmd_buf);
      std::array<vk::Offset3D, 2> offs = {
          vk::Offset3D{},
          vk::Offset3D{.x =static_cast<int32_t>(resolution.x), .y=static_cast<int32_t>(resolution.y), .z = 1}
      };
      std::array<vk::ImageBlit, 1> blit = {
        vk::ImageBlit{
            .srcSubresource = {.aspectMask=vk::ImageAspectFlagBits::eColor, .layerCount=1,},
            .srcOffsets = offs,
            .dstSubresource = {.aspectMask=vk::ImageAspectFlagBits::eColor, .layerCount=1},
            .dstOffsets = offs,
          }
      };
      cmd_buf.blitImage(backbuffer.get(), vk::ImageLayout::eTransferSrcOptimal,
        target_image, vk::ImageLayout::eTransferDstOptimal,
        blit,
        vk::Filter::eLinear
      );

      etna::set_state(
        cmd_buf, 
        target_image, 
        vk::PipelineStageFlagBits2::eAllCommands, 
        {}, 
        vk::ImageLayout::ePresentSrcKHR, 
        vk::ImageAspectFlagBits::eColor
      );
      etna::flush_barriers(cmd_buf);
      return;
    }

    etna::set_state(
      cmd_buf,
      backbuffer.get(),
      vk::PipelineStageFlagBits2::eComputeShader, 
      vk::AccessFlagBits2::eShaderRead, 
      vk::ImageLayout::eGeneral, 
      vk::ImageAspectFlagBits::eColor
    );
    etna::flush_barriers(cmd_buf);

    {
      auto set = etna::create_descriptor_set(
        etna::get_shader_program("postprocess").getDescriptorLayoutId(0),
        cmd_buf,
        {
          etna::Binding{0, backbuffer.genBinding(sampler.get(), vk::ImageLayout::eGeneral)},
          etna::Binding{1, density.genBinding()}
        });
      vk::DescriptorSet vkSet = set.getVkSet();
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, postprocessPipeline.getVkPipeline());
      cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, postprocessPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

      cmd_buf.dispatch((resolution.x + 31) / 32, (resolution.y + 31) / 32, 1);
    }

    {
      auto set = etna::create_descriptor_set(
        etna::get_shader_program("density_hist").getDescriptorLayoutId(0),
        cmd_buf,
        {
          etna::Binding{0, prefsum.genBinding()},
          etna::Binding{1, density.genBinding()}
        });
      vk::DescriptorSet vkSet = set.getVkSet();
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, densityHistPipeline.getVkPipeline());
      cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, densityHistPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

      cmd_buf.dispatch(1, 1, 1);
    }

    etna::set_state(
      cmd_buf,
      backbuffer.get(),
      vk::PipelineStageFlagBits2::eFragmentShader, 
      vk::AccessFlagBits2::eShaderSampledRead, 
      vk::ImageLayout::eShaderReadOnlyOptimal, 
      vk::ImageAspectFlagBits::eColor
    );
    etna::flush_barriers(cmd_buf);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = target_image, .view = target_image_view}}, {});

    {
      auto set = etna::create_descriptor_set(
        etna::get_shader_program("tonemapping").getDescriptorLayoutId(0),
        cmd_buf,
        {
          etna::Binding{0, backbuffer.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
          etna::Binding{1, density.genBinding()}
        });
      vk::DescriptorSet vkSet = set.getVkSet();
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, tonemappingPipeline.getVkPipeline());
      cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, tonemappingPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

      cmd_buf.draw(3, 1, 0, 0);
    }
}
  
