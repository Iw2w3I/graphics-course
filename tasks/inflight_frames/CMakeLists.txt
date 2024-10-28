
add_executable(inflight_frames
  main.cpp
  App.cpp
)

target_link_libraries(inflight_frames
  PRIVATE glfw etna glm::glm wsi gui Tracy::TracyClient)
target_link_libraries(inflight_frames
  PUBLIC render_utils scene)

target_add_shaders(inflight_frames
  shaders/toy.frag
  shaders/toy.vert
  shaders/texture.frag
)

add_compile_definitions(inflight_frames
  PRIVATE INFLIGHT_FRAMES_TEXTURES_ROOT="${PROJECT_SOURCE_DIR}/resources/textures/")