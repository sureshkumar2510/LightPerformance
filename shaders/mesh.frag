#version 430 core
in vec3 vNrm;
out vec4 FragColor;

uniform vec3 uLightDir = normalize(vec3(0.3, 0.6, 0.7));
uniform vec3 uBaseColor = vec3(0.75);

void main() {
  float ndl = max(dot(normalize(vNrm), uLightDir), 0.0);
  vec3 col = uBaseColor * (0.25 + 0.75 * ndl);
  FragColor = vec4(col, 1.0);
}
