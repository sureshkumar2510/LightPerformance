#include "camera.h"
#include <algorithm>
#include <cmath>

glm::vec3 Camera::Position() const {
  // Spherical to Cartesian around target
  float cp = std::cos(pitch);
  glm::vec3 dir(
    cp * std::cos(yaw),
    std::sin(pitch),
    cp * std::sin(yaw)
  );
  return target - dir * distance; // camera behind direction looking at target
}

glm::mat4 Camera::View() const {
  return glm::lookAt(Position(), target, glm::vec3(0,1,0));
}

glm::mat4 Camera::Proj(float aspect) const {
  return glm::perspective(fovY, aspect, nearZ, farZ);
}

void Camera::Orbit(float dx, float dy) {
  const float rotSpeed = 0.005f; // radians per pixel
  yaw   -= dx * rotSpeed;
  pitch -= dy * rotSpeed;

  // Clamp pitch to avoid flipping (gimbal singularity)
  const float lim = glm::radians(89.0f);
  pitch = std::clamp(pitch, -lim, lim);
}

void Camera::Pan(float dx, float dy, float viewportH) {
  // Pan speed scales with distance and FOV: screen-space drag -> world-space move
  // At target plane: worldUnitsPerPixel ~ 2 * distance * tan(fov/2) / viewportH
  float worldPerPixel = (2.0f * distance * std::tan(fovY * 0.5f)) / std::max(1.0f, viewportH);

  glm::mat4 V = View();
  // Camera right and up vectors from inverse view (or derive from lookAt)
  glm::vec3 right = glm::vec3(glm::inverse(V)[0]);
  glm::vec3 up    = glm::vec3(glm::inverse(V)[1]);

  target += (-right * dx + up * dy) * worldPerPixel;
}

void Camera::Zoom(float scrollDelta) {
  // Exponential zoom feels CAD-like
  float zoomFactor = std::pow(0.9f, scrollDelta);
  distance = std::clamp(distance * zoomFactor, 0.01f, 1e7f);
}

void Camera::FrameSphere(const glm::vec3& center, float radius) {
  target = center;
  // Place camera so sphere fits in view vertically
  float halfFov = fovY * 0.5f;
  distance = radius / std::sin(std::max(halfFov, 0.0001f));
  distance = std::max(distance, 0.01f);

  nearZ = std::max(0.001f, distance * 0.001f);
  farZ  = std::max(1000.0f, distance + radius * 10.0f);
}
