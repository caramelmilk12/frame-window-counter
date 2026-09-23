#pragma once
#include "../Data/Types.hpp"
#include <vector>
#include <cstdint>
#include <filesystem>

namespace CmlParser {
    struct CmlParseResult {
        std::vector<FrameAction> actions;
        double fps = 240.0;
        bool hasFps = false;
    };

    CmlParseResult parse(const std::filesystem::path& path);
    CmlParseResult parse(const std::vector<uint8_t>& data);
}
