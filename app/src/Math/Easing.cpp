#include <Math/Easing.hpp>
#include <cmath>

float ease(EasingType type, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    const float PI = 3.14159265f;

    switch (type) {
        case EasingType::Linear:
            return t;
        case EasingType::Quadratic:
            return t * t;
        case EasingType::Cosine:
            return (1.0f - std::cos(t * PI)) * 0.5f;
        case EasingType::Sine:
            return std::sin(t * PI * 0.5f);
        case EasingType::Exponential:
            return std::pow(2.0f, 10.0f * (t - 1.0f));
    }
    return t;
}
