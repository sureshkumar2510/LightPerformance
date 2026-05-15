#pragma once
#include <vector>
#include <cstdint>
#include <glad/glad.h>

struct VertexPN {
  float px, py, pz;
  float nx, ny, nz;
};

struct GpuMesh {
  GLuint vao = 0, vbo = 0, ebo = 0;
  uint32_t indexCount = 0;

  // bounding box (for auto framing / camera target)
  float bmin[3] = {  1e30f,  1e30f,  1e30f };
  float bmax[3] = { -1e30f, -1e30f, -1e30f };

  void destroy();
};

GpuMesh UploadMeshPN(const std::vector<VertexPN>& verts,
                     const std::vector<uint32_t>& indices,
                     const float bmin[3], const float bmax[3]);