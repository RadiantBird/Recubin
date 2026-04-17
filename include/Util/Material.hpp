#pragma once

enum class MaterialType {
    Plastic, // レゴ相当
    Wood,
    Metal,
    Stone
};

struct Material {
    MaterialType type = MaterialType::Plastic;
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.1f;

    static Material GetDefault(MaterialType t) {
        Material m;
        m.type = t;
        switch (t) {
            case MaterialType::Plastic:
                m.staticFriction = 0.5f;
                m.dynamicFriction = 0.5f;
                m.restitution = 0.1f; // あまり弾まない
                break;
            case MaterialType::Wood:
                m.staticFriction = 0.4f;
                m.dynamicFriction = 0.4f;
                m.restitution = 0.3f;
                break;
            case MaterialType::Metal:
                m.staticFriction = 0.3f;
                m.dynamicFriction = 0.3f;
                m.restitution = 0.05f;
                break;
            case MaterialType::Stone:
                m.staticFriction = 0.6f;
                m.dynamicFriction = 0.6f;
                m.restitution = 0.1f;
                break;
        }
        return m;
    }
};
