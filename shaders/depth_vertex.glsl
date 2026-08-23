#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 5) in mat4 aInstModel;
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
uniform float uInstanced;
uniform float uTime;
uniform float uIsLiquid;
void main() {
    mat4 m = (uInstanced > 0.5) ? aInstModel : model;
    vec3 p = aPos;
    if (uIsLiquid > 0.5 && aPos.y > 0.0) {
        p.y += sin(uTime * 1.5 + aPos.x * 4.0 + aPos.z * 4.0) * 0.06;
    }
    gl_Position = lightSpaceMatrix * m * vec4(p, 1.0);
}
