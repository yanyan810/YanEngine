#pragma once
#include <vector>
#include "Model.h"

namespace GeometryGenerator {

    std::vector<Model::VertexData>
        GenerateRingTriListXY(uint32_t divide, float outerR, float innerR);

}
