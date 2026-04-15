#include "GeometryGenerator.h"
#include <numbers>
#include <cmath>

std::vector<Model::VertexData>
GeometryGenerator::GenerateRingTriListXY(uint32_t divide, float outerR, float innerR) {
    std::vector<Model::VertexData> v;
    v.reserve(divide * 6);

    const float pi = std::numbers::pi_v<float>;
    const float step = (2.0f * pi) / float(divide);

    auto MakeV = [](float x, float y, float u, float vv) -> Model::VertexData {
        Model::VertexData out{};
        out.position = { x, y, 0.0f, 1.0f };
        out.texcoord = { u, vv };
        out.normal = { 0.0f, 0.0f, 1.0f }; // XY平面
        return out;
        };

    for (uint32_t i = 0; i < divide; ++i) {
        float a0 = step * float(i);
        float a1 = step * float(i + 1);

        float c0 = std::cosf(a0), s0 = std::sinf(a0);
        float c1 = std::cosf(a1), s1 = std::sinf(a1);

        float ox0 = c0 * outerR, oy0 = s0 * outerR;
        float ix0 = c0 * innerR, iy0 = s0 * innerR;
        float ox1 = c1 * outerR, oy1 = s1 * outerR;
        float ix1 = c1 * innerR, iy1 = s1 * innerR;

        float u0 = float(i) / float(divide);
        float u1 = float(i + 1) / float(divide);

        // (outer0, inner0, outer1)
        v.push_back(MakeV(ox0, oy0, u0, 0.0f));
        v.push_back(MakeV(ix0, iy0, u0, 1.0f));
        v.push_back(MakeV(ox1, oy1, u1, 0.0f));

        // (outer1, inner0, inner1)
        v.push_back(MakeV(ox1, oy1, u1, 0.0f));
        v.push_back(MakeV(ix0, iy0, u0, 1.0f));
        v.push_back(MakeV(ix1, iy1, u1, 1.0f));
    }

    return v;
}
