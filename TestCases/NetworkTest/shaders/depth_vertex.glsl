#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 5) in mat4 aInstModel;
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
uniform float uInstanced;
void main() {
    mat4 m = (uInstanced > 0.5) ? aInstModel : model;
    gl_Position = lightSpaceMatrix * m * vec4(aPos, 1.0);
}
