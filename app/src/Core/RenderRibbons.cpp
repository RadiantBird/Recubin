#include <Core/RenderRibbons.hpp>

#include <cmath>

void buildSegmentRibbons(const std::vector<float>& segmentEndpoints,
                         const Vector3& cameraPosition, float fovYDegrees,
                         int viewportHeightPx, float pixelWidth,
                         std::vector<float>& outVertices) {
    outVertices.clear();
    if (viewportHeightPx <= 0 || pixelWidth <= 0.001f) return;
    const float tanHalfFov = std::tan(fovYDegrees * 3.14159265f / 360.0f);
    for (std::size_t i = 0; i + 5 < segmentEndpoints.size(); i += 6) {
        Vector3 p0(segmentEndpoints[i], segmentEndpoints[i + 1], segmentEndpoints[i + 2]);
        Vector3 p1(segmentEndpoints[i + 3], segmentEndpoints[i + 4], segmentEndpoints[i + 5]);
        Vector3 segment = p1 - p0;
        const float length = segment.length();
        if (length < 1e-6f) continue;
        const Vector3 direction = segment / length;
        const Vector3 midpoint = (p0 + p1) * 0.5f;
        const Vector3 toCamera = cameraPosition - midpoint;
        const Vector3 viewDirection = toCamera.length() > 1e-6f ? toCamera.normalize() : Vector3(0, 1, 0);
        Vector3 right = Vector3::Cross(direction, viewDirection);
        if (right.length() < 1e-5f) right = Vector3::Cross(direction, Vector3(0, 1, 0));
        if (right.length() < 1e-5f) right = Vector3::Cross(direction, Vector3(1, 0, 0));
        if (right.length() < 1e-5f) right = Vector3::Cross(direction, Vector3(0, 0, 1));
        right = right.normalize();
        const float half0 = pixelWidth * (cameraPosition - p0).length() * tanHalfFov / static_cast<float>(viewportHeightPx);
        const float half1 = pixelWidth * (cameraPosition - p1).length() * tanHalfFov / static_cast<float>(viewportHeightPx);
        const Vector3 a0 = p0 + right * half0, a1 = p0 - right * half0;
        const Vector3 b0 = p1 + right * half1, b1 = p1 - right * half1;
        const auto append = [&outVertices](const Vector3& value) { outVertices.insert(outVertices.end(), {value.x, value.y, value.z}); };
        append(a0); append(a1); append(b1); append(a0); append(b1); append(b0);
    }
}

void buildRibbonStrip(const std::vector<float>& points, float width,
                      const Vector3& cameraPosition, std::vector<float>& outVertices) {
    const std::size_t count = points.size() / 3;
    if (count < 2 || width <= 0.0f) return;
    const float halfWidth = width * 0.5f;
    std::vector<Vector3> positions(count), left(count), right(count);
    for (std::size_t i = 0; i < count; ++i) positions[i] = Vector3(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
    for (std::size_t i = 0; i < count; ++i) {
        const Vector3 previous = i > 0 ? (positions[i] - positions[i - 1]).normalize() : (positions[i + 1] - positions[i]).normalize();
        const Vector3 next = i + 1 < count ? (positions[i + 1] - positions[i]).normalize() : previous;
        const Vector3 tangent = (previous + next).normalize();
        Vector3 side = Vector3::Cross(tangent, (cameraPosition - positions[i]).normalize());
        const float length = side.length();
        side = length > 1e-5f ? (side / length) * halfWidth : Vector3(halfWidth, 0, 0);
        left[i] = positions[i] + side; right[i] = positions[i] - side;
    }
    outVertices.reserve(outVertices.size() + (count - 1) * 18);
    const auto append = [&outVertices](const Vector3& value) { outVertices.insert(outVertices.end(), {value.x, value.y, value.z}); };
    for (std::size_t i = 0; i + 1 < count; ++i) {
        append(left[i]); append(right[i]); append(left[i + 1]); append(right[i]); append(right[i + 1]); append(left[i + 1]);
    }
}
