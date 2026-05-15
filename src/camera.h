#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
  // Target/orbit style camera
  glm::vec3 target {0.0f, 0.0f, 0.0f};
  float distance = 3.0f;          // dolly distance
  float yaw = 0.0f;               // radians
  float pitch = 0.0f;             // radians

  // Projection
  float fovY = glm::radians(45.0f);
  float nearZ = 0.01f;
  float farZ  = 10000.0f;

  // Derived
  glm::vec3 Position() const;
  glm::mat4 View() const;
  glm::mat4 Proj(float aspect) const;

  // Controls
  void Orbit(float dx, float dy); // pixels -> radians (scaled)
  void Pan(float dx, float dy, float viewportH); // pixels -> world units
  void Zoom(float scrollDelta);   // scroll -> distance change

  // Helpers for fitting model
  void FrameSphere(const glm::vec3& center, float radius);
};