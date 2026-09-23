#pragma once

#include <filesystem>
#include <vector>

namespace TcmParser {
    struct Input {
        int frame;
        bool player2;
    };

    struct Result {
        std::vector<Input> inputs;
        double fps;
    };

    Result parse(const std::filesystem::path& path);
}
