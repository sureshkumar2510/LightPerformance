#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNrm;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vNrm;

void main() {
  vNrm = mat3(transpose(inverse(uModel))) * aNrm;
  gl_Position = uMVP * vec4(aPos, 1.0);
}