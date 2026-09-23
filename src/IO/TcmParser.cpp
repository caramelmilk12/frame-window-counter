#include "TcmParser.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace TcmParser {
namespace {

// tcm-rs/src/replay.rs: 固定 16 字节文件头，后面紧跟 0x40 字节元数据。
constexpr std::array<uint8_t, 16> HEADER = {
    0x9f, 0x88, 0x89, 0x84, 0x9f, 0x3b, 0x1d, 0xd8,
    0xcc, 0xa1, 0x86, 0x8a, 0x88, 0x99, 0x84, 0x00
};

uint8_t readByte(std::istream& stream) {
    int value = stream.get();
    if (value == std::char_traits<char>::eof()) {
        throw std::runtime_error("Unexpected end of TCM file");
    }
    return static_cast<uint8_t>(value);
}

uint64_t readLittleEndian(std::istream& stream, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        value |= static_cast<uint64_t>(readByte(stream)) << (8 * i);
    }
    return value;
}

float readFloat(std::istream& stream) {
    uint32_t bits = static_cast<uint32_t>(readLittleEndian(stream, 4));
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// v1 的 input 数量和帧号、v2 的首帧，均使用无符号 u32 LEB128。
// u32 是文件格式本身的字段宽度；这里不另设宏 input 条数限制。
uint32_t readVarU32(std::istream& stream) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 5; ++i) {
        uint8_t byte = readByte(stream);
        if (i == 4 && (byte & 0xf0) != 0) {
            throw std::runtime_error("TCM u32 varint overflow");
        }
        result |= static_cast<uint32_t>(byte & 0x7f) << (7 * i);
        if ((byte & 0x80) == 0) return result;
    }
    throw std::runtime_error("TCM u32 varint is too long");
}

int actionFrame(uint64_t frame) {
    // FrameAction.frame 使用 int；超出可表示范围时必须报错，不能截断到错误帧。
    if (frame > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("TCM frame exceeds editor range");
    }
    return static_cast<int>(frame);
}

void parseV1(std::istream& stream, Result& result) {
    const uint32_t count = readVarU32(stream);
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t frame = readVarU32(stream);
        uint8_t flags = readByte(stream);

        // v1: bits 0..2 是按钮编号减一；bit 6 是 2P，bit 7 是按下状态。
        // 编号 0..2 为 Jump/Left/Right；3..5 为重启/完全重启/死亡。
        // 与现有 gdr/slc 导入一致，按下和松开都计为一个 input；重启不计入帧动作。
        uint8_t kind = flags & 0x07;
        if (kind < 3) {
            result.inputs.push_back({ actionFrame(frame), (flags & 0x40) != 0 });
        } else if (kind > 5) {
            throw std::runtime_error("Invalid TCM v1 input byte");
        }
    }
    // Rust 读取器按照 count 停止，末尾的 0xCC 结束标记由此自然忽略。
}

void parseV2(std::istream& stream, Result& result) {
    uint64_t frame = readVarU32(stream);
    uint64_t lastDelta = 0;

    while (true) {
        int next = stream.get();
        if (next == std::char_traits<char>::eof()) {
            if (!stream.eof()) throw std::runtime_error("Failed to read TCM action");
            break; // v2 没有 input 数量字段，以动作边界处的 EOF 结束。
        }
        uint8_t flags = static_cast<uint8_t>(next);

        // v2: bits 0..1 是按钮 (1=Jump, 2=Left, 3=Right, 0=特殊指令)；
        // bit 2=按下，bit 3=2P，bit 4=swift 或特殊指令附加字段。
        // bits 5..7 编码下一动作的帧增量：bit 5 是复用上次增量标志，
        // bits 6..7 决定后续增量占 0/1/2/4 字节，字节序为 little endian。
        uint8_t button = flags & 0x03;
        if (button != 0) {
            bool player2 = (flags & 0x08) != 0;
            int editorFrame = actionFrame(frame);
            result.inputs.push_back({ editorFrame, player2 });
            if ((flags & 0x10) != 0) {
                // swift 在同一帧隐含一个反向按键事件，因此是两个 input。
                result.inputs.push_back({ editorFrame, player2 });
            }
        } else {
            uint8_t custom = (flags >> 2) & 0x03;
            bool extra = (flags & 0x10) != 0;
            if (custom == 3) {
                if (!extra) {
                    // TPS 变更指令的载荷是 little-endian f32；编辑器只有单一全局 FPS。
                    // 消费载荷以保持后续动作对齐，导入 FPS 采用文件元数据中的初始值。
                    readFloat(stream);
                }
                // extra=1 为 bugpoint 标记，无额外载荷，也不产生帧动作。
            } else {
                // custom 0..2 是重启/完全重启/死亡。bit 4 表示随后有 u64 种子。
                if (extra) readLittleEndian(stream, 8);
                frame = 0;
            }
        }

        uint8_t deltaInfo = flags >> 5;
        unsigned deltaBytes = (deltaInfo >> 1) == 3 ? 4 : (deltaInfo >> 1);
        uint64_t encoded = readLittleEndian(stream, deltaBytes);
        uint64_t delta = (deltaInfo & 1) ? lastDelta : 0;
        if (encoded > std::numeric_limits<uint64_t>::max() - delta) {
            throw std::runtime_error("TCM frame delta overflow");
        }
        delta += encoded;
        if (deltaBytes != 0 && delta != 0) lastDelta = delta;
        if (frame > std::numeric_limits<uint64_t>::max() - delta) {
            throw std::runtime_error("TCM frame overflow");
        }
        frame += delta;
    }
}

} // namespace

Result parse(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open .tcm file");

    for (uint8_t expected : HEADER) {
        if (readByte(stream) != expected) {
            throw std::runtime_error("Invalid TCM file signature");
        }
    }

    // 元数据在偏移 0x10 开始，固定 0x40 字节。byte 0=版本，byte 2=v2 flags，
    // bytes 4..7=little-endian f32 TPS 或秒/帧。其余字段与帧导入无关。
    std::array<uint8_t, 0x40> meta{};
    stream.read(reinterpret_cast<char*>(meta.data()), meta.size());
    if (!stream) throw std::runtime_error("Incomplete TCM metadata");
    uint8_t version = meta[0];
    if (version != 1 && version != 2) {
        throw std::runtime_error("Unsupported TCM version: " + std::to_string(version));
    }
    uint32_t fpsBits = static_cast<uint32_t>(meta[4]) |
                       (static_cast<uint32_t>(meta[5]) << 8) |
                       (static_cast<uint32_t>(meta[6]) << 16) |
                       (static_cast<uint32_t>(meta[7]) << 24);
    float fpsValue;
    std::memcpy(&fpsValue, &fpsBits, sizeof(fpsValue));
    // 与 Rust MetaV2::tps() 相同，先以 f32 计算倒数，再转换成编辑器的 double。
    float tps = (version == 2 && (meta[2] & 0x02) == 0)
                    ? 1.0f / fpsValue : fpsValue;
    double fps = tps;
    if (!std::isfinite(fps) || fps <= 0.0) {
        throw std::runtime_error("Invalid TCM TPS value");
    }

    Result result{{}, fps};
    if (version == 1) parseV1(stream, result);
    else parseV2(stream, result);
    return result;
}

} // namespace TcmParser
