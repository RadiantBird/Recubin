// 深度だけを書くシェーダー（シャドウマップと SurfaceMark の投影深度。Renderer::renderViewport）
@module depth

@vs depth_vs
layout(binding = 0) uniform vs_params {
    mat4  lightSpaceMatrix;
    mat4  model;
    float uInstanced; // 1: インスタンス属性(location 5-8)のモデル行列を使う
    float uTime;
    float uIsLiquid;
};

layout(location = 0) in vec3 aPos;
// インスタンスのモデル行列の列（Renderer::setInstanceAttribPointers の属性 5-8）
layout(location = 5) in vec4 aInstModel0;
layout(location = 6) in vec4 aInstModel1;
layout(location = 7) in vec4 aInstModel2;
layout(location = 8) in vec4 aInstModel3;

void main() {
    mat4 m = (uInstanced > 0.5) ? mat4(aInstModel0, aInstModel1, aInstModel2, aInstModel3) : model;
    vec3 p = aPos;
    if (uIsLiquid > 0.5 && aPos.y > 0.0) {
        p.y += sin(uTime * 1.5 + aPos.x * 4.0 + aPos.z * 4.0) * 0.06;
    }
    gl_Position = lightSpaceMatrix * m * vec4(p, 1.0);
}
@end

@fs depth_fs
void main() {}
@end

@program pass depth_vs depth_fs
