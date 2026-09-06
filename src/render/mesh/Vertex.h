#pragma once

#include <array>

namespace engine {

struct Vertex {
    std::array<float, 2> position;
    std::array<float, 3> color;
};

} // namespace engine
