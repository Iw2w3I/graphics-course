#include <cmath>
#include <array>
#include <iostream>
#include <filesystem>
#include <optional>
#include <vector>
#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include "scene/SceneManager.hpp"

class Bakery : SceneManager {
public:
  void bake(std::filesystem::path path) {
    std::cout << "1";
    auto maybeModel = loadModel(path);
    if (!maybeModel.has_value())
      return;
    auto model = *maybeModel;
    auto [verts, inds, relems, meshes] = processMeshes(model);

    model.buffers.clear();

    tinygltf::Buffer buffer;
    auto name = path.parent_path() / path.stem();
    buffer.name = name.stem().string();
    buffer.uri = name.filename().string() + "_baked.bin";

    std::size_t indsSize = inds.size() * sizeof(uint32_t);
    std::size_t vertsSize = verts.size() * sizeof(Vertex);
    buffer.data.resize(indsSize + vertsSize);
    std::memcpy(buffer.data.data(), inds.data(), indsSize);
    std::memcpy(buffer.data.data() + indsSize, verts.data(), vertsSize);
    model.buffers.push_back(buffer);

    model.bufferViews.clear();

    auto& bufferView1 = model.bufferViews.emplace_back();
    bufferView1.buffer = 0;
    bufferView1.byteLength = vertsSize;
    bufferView1.byteOffset = 0;
    bufferView1.byteStride = sizeof(Vertex);
    bufferView1.target = TINYGLTF_TARGET_ARRAY_BUFFER;

    auto& bufferView2 = model.bufferViews.emplace_back();
    bufferView2.buffer = 0;
    bufferView2.byteLength = indsSize;
    bufferView2.byteOffset = vertsSize;
    bufferView2.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

    std::cout << "1";
    bakeGLTF(model, relems, meshes);
    std::cout << "1";

    auto output = name.string() + "_baked.gltf";
    loader.WriteGltfSceneToFile(&model, output, false, false, true, false);
  };
private:
  struct RenderElementExtention {
    std::uint32_t vertexCount;
    std::array<std::vector<double>, 2> positionBound;
    std::array<std::vector<double>, 2> texcoordBound;
  };

  void bakeGLTF(tinygltf::Model& model, std::vector<RenderElement>& relems, std::vector<Mesh>& meshes) {
    tinygltf::Accessor indices;
    indices.bufferView = 0;
    indices.byteOffset = 0;
    indices.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    indices.type = TINYGLTF_TYPE_SCALAR;

    tinygltf::Accessor position;
    position.bufferView = 1;
    position.byteOffset = 0;
    position.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    position.type = TINYGLTF_TYPE_VEC3;

    tinygltf::Accessor normal;
    normal.bufferView = 1;
    normal.byteOffset = 12;
    normal.normalized = true;
    normal.componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
    normal.type = TINYGLTF_TYPE_VEC3;

    tinygltf::Accessor tangent;
    tangent.bufferView = 1;
    tangent.byteOffset = 24;
    tangent.normalized = true;
    tangent.componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
    tangent.type = TINYGLTF_TYPE_VEC4;

    tinygltf::Accessor texcoord_0;
    texcoord_0.bufferView = 1;
    texcoord_0.byteOffset = 16;
    texcoord_0.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    texcoord_0.type = TINYGLTF_TYPE_VEC2;

    std::map<std::string, tinygltf::Accessor> accessors = {
      {"POSITION", position},
      {"NORMAL", normal},
      {"TANGENT", tangent},
      {"TEXCOORD_0", texcoord_0},
    };

    std::vector<RenderElementExtention> additionalRelems;
    additionalRelems.reserve(relems.size());
    for (const auto& mesh : model.meshes) {
      for (const auto& prim : mesh.primitives) {
        if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
          continue;
        }
        const auto& vertexInfo = model.accessors[prim.attributes.at("POSITION")];
        const auto& texcoordInfo = prim.attributes.find("TEXCOORD_0") != prim.attributes.end() ? model.accessors[prim.attributes.at("TEXCOORD_0")]
                                                                                               : tinygltf::Accessor();
        additionalRelems.push_back(RenderElementExtention{
          .vertexCount = static_cast<std::uint32_t>(vertexInfo.count),
          .positionBound = std::array{vertexInfo.minValues, vertexInfo.maxValues},
          .texcoordBound = std::array{texcoordInfo.minValues, texcoordInfo.maxValues},
        });
      }
    }

    model.accessors.clear();

    for (size_t i = 0; i < model.meshes.size(); i++) {
      auto& mesh = model.meshes[i];
      for (size_t j = 0; j < mesh.primitives.size(); j++) {
        auto& prim = mesh.primitives[j];
        if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
          continue;
        }

        std::erase_if(prim.attributes, [&](const auto& attribute) {
          for (const auto& accessor : accessors) {
            if (attribute.first == accessor.first) {
              return false;
            }
          }
          return true;
        });

        auto& relem = relems[meshes[i].firstRelem + j];
        auto& addRelem = additionalRelems[meshes[i].firstRelem + j];

        prim.indices = static_cast<int>(model.accessors.size());
        auto& currentIndicesAccessor = model.accessors.emplace_back(indices);
        currentIndicesAccessor.byteOffset += relem.indexOffset * sizeof(uint32_t);
        currentIndicesAccessor.count = relem.indexCount;

        accessors["POSITION"].minValues = addRelem.positionBound[0];
        accessors["POSITION"].maxValues = addRelem.positionBound[1];
        accessors["TEXCOORD_0"].minValues = addRelem.texcoordBound[0];
        accessors["TEXCOORD_0"].maxValues = addRelem.texcoordBound[1];

        for (const auto& accessor : accessors) {
          if (!prim.attributes.contains(accessor.first)) {
            continue;
          }
          prim.attributes[accessor.first] = static_cast<int>(model.accessors.size());
          auto& new_accessor = model.accessors.emplace_back(accessor.second);
          new_accessor.byteOffset += relem.vertexOffset * sizeof(Vertex);
          new_accessor.count = addRelem.vertexCount;
        }
      }
    }
  };
};

int main(int argc, char* argv[]) {
  auto path = std::filesystem::path(argv[1]);
  if (!std::filesystem::exists(path) || path.extension() != ".gltf" || argc < 0) {
    spdlog::error("No .gltf file found");
    return EXIT_FAILURE;
  }

  Bakery b;
  b.bake(path);

  return EXIT_SUCCESS;
}
