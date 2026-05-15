#include "assimp_loader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stdexcept>
#include <vector>
#include <cstdint>
#include <algorithm>

static void UpdateBounds(float bmin[3], float bmax[3], float x, float y, float z) {
  bmin[0] = std::min(bmin[0], x); bmin[1] = std::min(bmin[1], y); bmin[2] = std::min(bmin[2], z);
  bmax[0] = std::max(bmax[0], x); bmax[1] = std::max(bmax[1], y); bmax[2] = std::max(bmax[2], z);
}

GpuMesh LoadWithAssimp_FlattenToSingleMesh(const std::string& path) {
  Assimp::Importer importer;

  // Real-time safe set:
  // - Triangulate for triangles [3](https://the-asset-importer-lib-documentation.readthedocs.io/en/latest/usage/postprocessing.html)
  // - JoinIdenticalVertices for indexed geometry [3](https://the-asset-importer-lib-documentation.readthedocs.io/en/latest/usage/postprocessing.html)
  // - GenNormals if needed (safe even if normals exist) [4](https://documentation.help/assimp/postprocess_8h.html)
  unsigned flags =
      aiProcess_Triangulate |
      aiProcess_JoinIdenticalVertices |
      aiProcess_GenNormals |
      aiProcess_SortByPType;

  const aiScene* scene = importer.ReadFile(path, flags);
  if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
    throw std::runtime_error(std::string("Assimp failed: ") + importer.GetErrorString());
  }

  std::vector<VertexPN> verts;
  std::vector<uint32_t> indices;
  verts.reserve(1 << 20);
  indices.reserve(1 << 20);

  float bmin[3] = {  1e30f,  1e30f,  1e30f };
  float bmax[3] = { -1e30f, -1e30f, -1e30f };

  // Flatten all meshes into one buffer to reduce draw calls.
  // Note: we intentionally do NOT use aiProcess_PreTransformVertices here,
  // because it can duplicate vertices heavily on big scenes.
  for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
    const aiMesh* m = scene->mMeshes[mi];
    if (!m->HasPositions()) continue;

    uint32_t baseVertex = (uint32_t)verts.size();

    // vertices
    for (unsigned v = 0; v < m->mNumVertices; ++v) {
      const aiVector3D& p = m->mVertices[v];
      aiVector3D n(0,0,1);
      if (m->HasNormals()) n = m->mNormals[v];

      verts.push_back(VertexPN{
        p.x, p.y, p.z,
        n.x, n.y, n.z
      });

      UpdateBounds(bmin, bmax, p.x, p.y, p.z);
    }

    // faces (triangles after aiProcess_Triangulate) [3](https://the-asset-importer-lib-documentation.readthedocs.io/en/latest/usage/postprocessing.html)
    for (unsigned f = 0; f < m->mNumFaces; ++f) {
      const aiFace& face = m->mFaces[f];
      if (face.mNumIndices != 3) continue; // should be 3 due to triangulate
      indices.push_back(baseVertex + (uint32_t)face.mIndices[0]);
      indices.push_back(baseVertex + (uint32_t)face.mIndices[1]);
      indices.push_back(baseVertex + (uint32_t)face.mIndices[2]);
    }
  }

  if (indices.empty() || verts.empty()) {
    throw std::runtime_error("No renderable triangles found in file.");
  }

  return UploadMeshPN(verts, indices, bmin, bmax);
}