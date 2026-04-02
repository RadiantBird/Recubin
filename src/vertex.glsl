#version 330 core
layout (location = 0) in vec3 aPos;
uniform vec2 offset;
uniform float deg;
void main() {
    float rad = deg * 3.1415927 / 180.0;
    float x = aPos.x * cos(rad) - aPos.y * sin(rad) + offset.x;
    float y = aPos.x * sin(rad) + aPos.y * cos(rad) + offset.y;
    gl_Position = vec4(x, y, aPos.z, 1.0);
}