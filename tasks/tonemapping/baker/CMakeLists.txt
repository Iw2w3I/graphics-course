
add_executable(tonemapping_baker
  main.cpp
)

target_link_libraries(tonemapping_baker
  PRIVATE tinygltf)
