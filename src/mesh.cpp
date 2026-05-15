#include "mesh.h"
#include <algorithm>

void GpuMesh::destroy() {
  if (ebo) glDeleteBuffers(1, &ebo);
  if (vbo) glDeleteBuffers(1, &vbo);
  if (vao) glDeleteVertexArrays(1, &vao);
  vao = vbo = ebo = 0;
  indexCount = 0;
}

static void UpdateBounds(float bmin[3], float bmax[3], float x, float y, float z) {
  bmin[0] = std::min(bmin[0], x); bmin[1] = std::min(bmin[1], y); bmin[2] = std::min(bmin[2], z);
  bmax[0] = std::max(bmax[0], x); bmax[1] = std::max(bmax[1], y); bmax[2] = std::max(bmax[2], z);
}

GpuMesh UploadMeshPN(const std::vector<VertexPN>& verts,
                     const std::vector<uint32_t>& indices,
                     const float inMin[3], const float inMax[3]) {
  GpuMesh m;
  m.bmin[0]=inMin[0]; m.bmin[1]=inMin[1]; m.bmin[2]=inMin[2];
  m.bmax[0]=inMax[0]; m.bmax[1]=inMax[1]; m.bmax[2]=inMax[2];

  glGenVertexArrays(1, &m.vao);
  glGenBuffers(1, &m.vbo);
  glGenBuffers(1, &m.ebo);

  glBindVertexArray(m.vao);

  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(VertexPN), verts.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

  // layout(location=0) position
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)0);
  glEnableVertexAttribArray(0);

  // layout(location=1) normal
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  glBindVertexArray(0);

  m.indexCount = (uint32_t)indices.size();
  return m;
}