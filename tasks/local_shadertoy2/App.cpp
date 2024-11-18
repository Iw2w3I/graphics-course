#include "App.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <etna/BlockingTransferHelper.hpp>


App::App()
  : resolution{1280, 720}
  , useVsync{true}
  , texture_resolution{128, 128}
{
  // First, we need to initialize Vulkan, which is not trivial because
  // extensions are required for just about anything.
  {
    // GLFW tells us which extensions it needs to present frames to the OS window.
    // Actually rendering anything to a screen is optional in Vulkan, you can
    // alternatively save rendered frames into files, send them over network, etc.
    // Instance extensions do not depend on the actual GPU, only on the OS.
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    // We also need the swapchain device extension to get access to the OS
    // window from inside of Vulkan on the GPU.
    // Device extensions require HW support from the GPU.
    // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS combination a "host."
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Etna does all of the Vulkan initialization heavy lifting.
    // You can skip figuring out how it works for now.
    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy 2",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      // Replace with an index if etna detects your preferred GPU incorrectly
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = 1,
    });
  }

  // Now we can create an OS window
  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,

  });

  // But we also need to hook the OS window up to Vulkan manually!
  {
    // First, we ask GLFW to provide a "surface" for the window,
    // which is an opaque description of the area where we can actually render.
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    // Then we pass it to Etna to do the complicated work for us
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    // And finally ask Etna to create the actual swapchain so that we can
    // get (different) images each frame to render stuff into.
    // Here, we do not support window resizing, so we only need to call this once.
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    // Technically, Vulkan might fail to initialize a swapchain with the requested
    // resolution and pick a different one. This, however, does not occur on platforms
    // we support. Still, it's better to follow the "intended" path.
    resolution = {w, h};
  }

  // Next, we need a magical Etna helper to send commands to the GPU.
  // How it is actually performed is not trivial, but we can skip this for now.
  commandManager = etna::get_context().createPerFrameCmdMgr();


  // TODO: Initialize any additional resources you require here!
  etna::create_program("local_shadertoy2", {LOCAL_SHADERTOY2_SHADERS_ROOT "toy.vert.spv",
                                            LOCAL_SHADERTOY2_SHADERS_ROOT "toy.frag.spv" });
  pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline("local_shadertoy2",
    etna::GraphicsPipeline::CreateInfo {
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {vkWindow->getCurrentFormat()}
      }
    });
  etna::create_program("local_shadertoy2_textures", {LOCAL_SHADERTOY2_SHADERS_ROOT "toy.vert.spv",
                                            LOCAL_SHADERTOY2_SHADERS_ROOT "texture.frag.spv" });
  pipeline_texture = etna::get_context().getPipelineManager().createGraphicsPipeline("local_shadertoy2_textures",
    etna::GraphicsPipeline::CreateInfo {
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {vkWindow->getCurrentFormat()}
      }
    });
  sampler = etna::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eMirroredRepeat,
    .name = "You should buy pringles"});
  shader_image = etna::get_context().createImage(etna::Image::CreateInfo {
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "resultImage",
    .format = vkWindow->getCurrentFormat(),
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
  });
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    windowing.poll();

    drawFrame();
  }

  // We need to wait for the GPU to execute the last frame before destroying
  // all resources and closing the application.
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}


void App::load_textures(vk::CommandBuffer& currentCmdBuf)
{
  if (load_textures_done) {
    return;
  }
  int x, y, comp;
  auto* texture = stbi_load(LOCAL_SHADERTOY2_TEXTURES_ROOT "texture1.bmp", &x, &y, &comp, 4);
  if (texture == nullptr) {
    throw "texture1.bmp not found";
  }
  texture_image = etna::create_image_from_bytes(etna::Image::CreateInfo {
    .extent = vk::Extent3D{uint32_t(x), uint32_t(y), 1},
    .name = "textureImage",
    .format = vkWindow->getCurrentFormat(),
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
  }, currentCmdBuf, texture);
  stbi_image_free(texture);
  load_textures_done = true;
}

void App::drawFrame()
{
  // First, get a command buffer to write GPU commands into.
  auto currentCmdBuf = commandManager->acquireNext();

  App::load_textures(currentCmdBuf);

  // Next, tell Etna that we are going to start processing the next frame.
  etna::begin_frame();

  // And now get the image we should be rendering the picture into.
  auto nextSwapchainImage = vkWindow->acquireNext();

  // When window is minimized, we can't render anything in Windows
  // because it kills the swapchain, so we skip frames in this case.
  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        currentCmdBuf,
        texture_image.get(),
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      {
        etna::RenderTargetState state{currentCmdBuf, {{}, {texture_resolution.x, texture_resolution.y}}, {{texture_image.get(), texture_image.getView({})}}, {}};
        etna::get_shader_program("local_shadertoy2_textures");

        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_texture.getVkPipeline());

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      etna::set_state(
        currentCmdBuf,
        shader_image.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        currentCmdBuf,
        texture_image.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      {
        etna::RenderTargetState state{currentCmdBuf, {{}, {resolution.x, resolution.y}}, {{backbuffer, backbufferView}}, {}};

        auto set = etna::create_descriptor_set(
          etna::get_shader_program("local_shadertoy2").getDescriptorLayoutId(0),
          currentCmdBuf,
          {
            etna::Binding{0, shader_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{1, texture_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
          });
        vk::DescriptorSet vkSet = set.getVkSet();

        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

        params.resolution = resolution;
        if (osWindow.get()->mouse[MouseButton::mbLeft] == ButtonState::High) {
          mouse_pos = osWindow.get()->mouse.freePos;
        }
        params.mouse_pos = mouse_pos;
        params.time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - init_time).count() / 1000.0f;
        currentCmdBuf.pushConstants(
          pipeline.getVkPipelineLayout(),
          vk::ShaderStageFlagBits::eFragment,
          0,
          sizeof(Params),
          &params
        );

        currentCmdBuf.draw(3, 1, 0, 0);
      }
      

      // At the end of "rendering", we are required to change how the pixels of the
      // swpchain image are laid out in memory to something that is appropriate
      // for presenting to the window (while preserving the content of the pixels!).
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        // This looks weird, but is correct. Ask about it later.
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      // And of course flush the layout transition.
      etna::flush_barriers(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    // We are done recording GPU commands now and we can send them to be executed by the GPU.
    // Note that the GPU won't start executing our commands before the semaphore is
    // signalled, which will happen when the OS says that the next swapchain image is ready.
    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    // Finally, present the backbuffer the screen, but only after the GPU tells the OS
    // that it is done executing the command buffer via the renderingDone semaphore.
    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  // After a window us un-minimized, we need to restore the swapchain to continue rendering.
  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}
