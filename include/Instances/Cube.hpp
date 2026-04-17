#pragma once

#include <include/Math/Matrix4.hpp>
#include <include/Instances/BaseCube.hpp>
#include <include/Instances/Decal.hpp>
#include <vector>

// 頂点構造体
struct Vertex {
    Vector3 Position;  
    Vector3 Normal;    
    float U, V;        

    Vertex() : Position(0, 0, 0), Normal(0, 0, 0), U(0), V(0) {}
};

// 頂点生成関数の宣言
std::vector<Vertex> createCubeVertices(float size);

class Cube : public BaseCube {
    public:
        static unsigned int defaultTextureID;

        // コンストラクタ
        Cube(Vector3 Pos, Vector3 Sz, unsigned int defaultTex);

        // メソッドの宣言
        void draw(int modelLoc, int shaderProgram);

        virtual string GetClassName() override;
        virtual bool IsA(std::string name) override;
};
