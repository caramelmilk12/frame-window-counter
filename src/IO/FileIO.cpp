#include "FileIO.hpp"
#include "../Data/State.hpp"
#include "../Common.hpp"
#include <slc/slc.hpp>
#include <gdr/gdr.hpp>
#include <gdr_convert.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <span>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#endif

using namespace geode::prelude;

namespace FileIO {

    namespace {
        constexpr std::array<uint8_t, 16> TCM_HEADER = {
            0x9f, 0x88, 0x89, 0x84, 0x9f, 0x3b, 0x1d, 0xd8,
            0xcc, 0xa1, 0x86, 0x8a, 0x88, 0x99, 0x84, 0x00
        };
        constexpr size_t TCM_META_SIZE = 0x40;
        constexpr size_t MAX_TCM_INPUTS = 10'000'000;

        uint8_t readU8(std::istream& f) {
            char value = 0;
            if (!f.read(&value, 1)) throw std::runtime_error("Unexpected end of TCM file");
            return static_cast<uint8_t>(value);
        }

        uint32_t readVarU32(std::istream& f) {
            uint32_t value = 0;
            for (int i = 0; i < 5; ++i) {
                uint8_t byte = readU8(f);
                if (i == 4 && (byte & 0xf0) != 0) throw std::runtime_error("Invalid TCM integer");
                value |= static_cast<uint32_t>(byte & 0x7f) << (i * 7);
                if ((byte & 0x80) == 0) return value;
            }
            throw std::runtime_error("Invalid TCM integer");
        }

        template <typename T>
        T readLittleEndian(std::istream& f) {
            std::array<uint8_t, sizeof(T)> bytes{};
            if (!f.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
                throw std::runtime_error("Unexpected end of TCM file");
            }
            T value;
            std::memcpy(&value, bytes.data(), sizeof(T));
            return value;
        }

        uint64_t readTcmDelta(std::istream& f, uint8_t data, uint64_t lastDelta) {
            const uint8_t byteCountCode = (data >> 1) & 0x3;
            const uint64_t base = (data & 1) ? lastDelta : 0;
            switch (byteCountCode) {
                case 0: return base;
                case 1: return base + readU8(f);
                case 2: return base + readLittleEndian<uint16_t>(f);
                case 3: return base + readLittleEndian<uint32_t>(f);
                default: throw std::runtime_error("Invalid TCM frame delta");
            }
        }

        void addTcmAction(std::vector<FrameAction>& actions, uint64_t frame, bool player2) {
            if (frame > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                throw std::runtime_error("TCM frame exceeds supported range");
            }
            if (actions.size() >= MAX_TCM_INPUTS) throw std::runtime_error("Too many TCM inputs");
            actions.push_back({ static_cast<int>(frame), false, 1, player2 });
        }

        void parseTcm(std::istream& f, std::vector<FrameAction>& actions, double& fps) {
            std::array<uint8_t, TCM_HEADER.size()> header{};
            if (!f.read(reinterpret_cast<char*>(header.data()), header.size()) || header != TCM_HEADER) {
                throw std::runtime_error("Invalid TCM header");
            }

            std::array<uint8_t, TCM_META_SIZE> meta{};
            if (!f.read(reinterpret_cast<char*>(meta.data()), meta.size())) {
                throw std::runtime_error("Incomplete TCM metadata");
            }
            const uint8_t version = meta[0];
            float parsedTps = 0.0f;
            std::memcpy(&parsedTps, meta.data() + 4, sizeof(float));
            if (version == 2 && (meta[2] & 0x02) == 0) parsedTps = 1.0f / parsedTps;
            if (!std::isfinite(parsedTps) || parsedTps <= 0.0f) throw std::runtime_error("Invalid TCM TPS");
            fps = parsedTps;

            if (version == 1) {
                uint32_t count = readVarU32(f);
                if (count > MAX_TCM_INPUTS) throw std::runtime_error("Too many TCM inputs");
                actions.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t frame = readVarU32(f);
                    uint8_t input = readU8(f);
                    uint8_t type = input & 0x07;
                    if (type < 3) addTcmAction(actions, frame, (input & 0x40) != 0);
                    else if (type > 5) throw std::runtime_error("Invalid TCM v1 input");
                }
                return;
            }
            if (version != 2) throw std::runtime_error("Unsupported TCM version");

            if (f.peek() == std::char_traits<char>::eof()) return;
            uint64_t currentFrame = readVarU32(f);

            uint64_t lastDelta = 0;
            while (f.peek() != std::char_traits<char>::eof()) {
                uint8_t input = readU8(f);
                uint8_t deltaData = (input >> 5) & 0x07;
                uint8_t button = input & 0x03;

                if (button != 0) {
                    addTcmAction(actions, currentFrame, (input & 0x08) != 0);
                    if ((input & 0x10) != 0) addTcmAction(actions, currentFrame, (input & 0x08) != 0);
                }
                else {
                    uint8_t customType = (input >> 2) & 0x03;
                    bool extra = (input & 0x10) != 0;
                    if (customType == 3 && !extra) {
                        float newTps = readLittleEndian<float>(f);
                        if (!std::isfinite(newTps) || newTps <= 0.0f) throw std::runtime_error("Invalid TCM TPS input");
                    }
                    else if (customType < 3) {
                        if (extra) (void)readLittleEndian<uint64_t>(f);
                        currentFrame = 0;
                    }
                }

                uint64_t delta = readTcmDelta(f, deltaData, lastDelta);
                if (delta != 0) lastDelta = delta;
                if (currentFrame > std::numeric_limits<uint64_t>::max() - delta) {
                    throw std::runtime_error("TCM frame overflow");
                }
                currentFrame += delta;
            }
        }
    }

    void exportFWC() {
#ifdef GEODE_IS_WINDOWS
        std::vector<FrameAction> exportList;
        for (auto& [k, v] : g_frameActions) exportList.push_back(v);

        std::stable_sort(exportList.begin(), exportList.end(), [](const FrameAction& a, const FrameAction& b) {
            return a.frame < b.frame;
            });

        HWND parentHwnd = GetActiveWindow();
        if (!parentHwnd) {
            parentHwnd = WindowFromDC(wglGetCurrentDC());
        }

        std::thread([exportList, parentHwnd]() {
            HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

            char filename[MAX_PATH] = { 0 };
            OPENFILENAMEA ofn;
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = parentHwnd; 
            ofn.lpstrFilter = "Frame Window Counter (*.fwc)\0*.fwc\0NANDL Calculator JSON (*.json)\0*.json\0All Files\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
            ofn.lpstrDefExt = "fwc";

            if (GetSaveFileNameA(&ofn)) {
                std::filesystem::path path(filename);
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                bool isJson = (ext == ".json" || ofn.nFilterIndex == 2);

                std::ofstream f(filename, isJson ? std::ios::out : std::ios::binary);
                if (f) {
                    //1.NaNDL Calculator 标准 JSON 导出
                    if (isJson) {
                        std::vector<matjson::Value> arr;
                        int inputCounter = 1;
                        for (auto& act : exportList) {
                            if (!act.shouldDraw) continue;
                            arr.push_back(matjson::makeObject({
                                {"input", inputCounter++},
                                {"timePosition", act.frame},
                                {"frameWindow", (act.frameWindow == 0) ? 1 : act.frameWindow}
                                }));
                        }
                        matjson::Value rootObject = matjson::makeObject({
                            {"format", "nandl-calculator"},
                            {"version", 1},
                            {"settings", matjson::makeObject({
                                {"gameFps", g_macroFps},
                                {"windowFps", g_macroFps},
                                {"respawnSeconds", 0},
                                {"timeUnit", "frames"}
                            })},
                            {"frameWindows", arr}
                            });
                        f << rootObject.dump(2);
                    }
                    // 2.FWC 二进制格式导出
                    // 二进制结构布局：
                    // [4 Bytes Magic: "FWCB"]
                    // [8 Bytes double: fps]
                    // [4 Bytes uint32: count]
                    // [Per Action: 4B int32 (frame) + 4B int32 (frameWindow) + 1B uint8 (flags)]
                    //  * flags bit0: shouldDraw, bit1: isPlayer2
                    else {
                        f.write("FWCB", 4);
                        double fps = g_macroFps;
                        f.write(reinterpret_cast<const char*>(&fps), sizeof(double));
                        uint32_t count = static_cast<uint32_t>(exportList.size());
                        f.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));

                        for (const auto& act : exportList) {
                            int32_t frame = act.frame;
                            int32_t frameWindow = act.frameWindow;
                            uint8_t flags = (act.shouldDraw ? 1 : 0) | (act.isPlayer2 ? 2 : 0);
                            f.write(reinterpret_cast<const char*>(&frame), sizeof(int32_t));
                            f.write(reinterpret_cast<const char*>(&frameWindow), sizeof(int32_t));
                            f.write(reinterpret_cast<const char*>(&flags), sizeof(uint8_t));
                        }
                    }
                    f.close();
                    geode::Loader::get()->queueInMainThread([]() {
                        auto alert = FLAlertLayer::create("Success", "Successfully exported file!", "OK");
                        alert->show(); stopAlertAnimation(alert);
                        });
                }
                else {
                    geode::Loader::get()->queueInMainThread([]() {
                        auto alert = FLAlertLayer::create("Error", "Failed to save file.", "OK");
                        alert->show(); stopAlertAnimation(alert);
                        });
                }
            }

            if (SUCCEEDED(hr)) {
                CoUninitialize();
            }
            }).detach();
#endif
    }

    void importReplay(std::function<void()> onSuccessCallback) {
#ifdef GEODE_IS_WINDOWS
        loadModData();

        HWND parentHwnd = GetActiveWindow();
        if (!parentHwnd) {
            parentHwnd = WindowFromDC(wglGetCurrentDC());
        }

        std::thread([onSuccessCallback, parentHwnd]() {
            HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

            char filename[MAX_PATH] = { 0 };
            OPENFILENAMEA ofn;
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = parentHwnd; 
            ofn.lpstrFilter = "Supported Formats\0*.fwc;*.json;*.gdr;*.gdr2;*.slc;*.tcm\0Frame Window Counter (*.fwc)\0*.fwc\0NANDL Calculator JSON (*.json)\0*.json\0GD Replay / Silicate / TCBot (*.gdr;*.gdr2;*.slc;*.tcm)\0*.gdr;*.gdr2;*.slc;*.tcm\0All Files\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
            ofn.lpstrDefExt = "fwc";

            if (GetOpenFileNameA(&ofn)) {
                std::filesystem::path path(filename);
                try {
                    std::vector<FrameAction> newActions;
                    double parsedFps = 240.0;
                    bool updateFps = false;
                    std::string ext = path.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    //1.解析.fwc
                    if (ext == ".fwc") {
                        std::ifstream f(path, std::ios::binary);
                        if (!f) throw std::runtime_error("Cannot open .fwc file");
                        char magic[5] = { 0 };
                        f.read(magic, 4);
                        if (std::string(magic) != "FWCB") throw std::runtime_error("Invalid binary FWC file");

                        f.read(reinterpret_cast<char*>(&parsedFps), sizeof(double));
                        updateFps = true;
                        uint32_t count = 0;
                        f.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));

                        for (uint32_t i = 0; i < count; ++i) {
                            int32_t frame = 0, frameWindow = 1;
                            uint8_t flags = 0;
                            f.read(reinterpret_cast<char*>(&frame), sizeof(int32_t));
                            f.read(reinterpret_cast<char*>(&frameWindow), sizeof(int32_t));
                            f.read(reinterpret_cast<char*>(&flags), sizeof(uint8_t));

                            newActions.push_back({ frame, (flags & 1) != 0, frameWindow, (flags & 2) != 0 });
                        }
                    }
                    //2.解析.json
                    else if (ext == ".json") {
                        std::ifstream f(path);
                        if (!f) throw std::runtime_error("Cannot open .json file");
                        std::stringstream buffer; buffer << f.rdbuf();
                        auto res = matjson::parse(buffer.str());
                        if (res.isOk()) {
                            auto root = res.unwrap();
                            if (root.contains("settings")) {
                                parsedFps = root["settings"]["gameFps"].asDouble().unwrapOr(240.0);
                                updateFps = true;
                            }
                            if (root.contains("frameWindows") && root["frameWindows"].isArray()) {
                                for (auto& item : root["frameWindows"].asArray().unwrap()) {
                                    FrameAction act;
                                    act.frame = static_cast<int>(item["timePosition"].asInt().unwrapOr(0));
                                    act.shouldDraw = true;
                                    act.frameWindow = static_cast<int>(item["frameWindow"].asInt().unwrapOr(1));
                                    act.isPlayer2 = false;
                                    newActions.push_back(act);
                                }
                            }
                        }
                    }
                    //3.解析.slc
                    else if (ext == ".slc") {
                        std::ifstream file(path, std::ios::binary);
                        if (!file) throw std::runtime_error("Cannot open .slc file");
                        auto readRes = slc::Replay<>::read(file);
                        if (!readRes) throw std::runtime_error("Failed to parse slc file");
                        auto replay = readRes.value();
                        parsedFps = replay.m_meta.m_tps;
                        updateFps = true;

                        for (const auto& atomVariant : replay.m_atoms.m_atoms) {
                            if (const auto* actionAtom = std::get_if<slc::ActionAtom>(&atomVariant)) {
                                for (const auto& action : actionAtom->m_actions) {
                                    FrameAction act;
                                    act.frame = static_cast<int>(action.m_frame);
                                    act.shouldDraw = false;
                                    act.frameWindow = 1;
                                    act.isPlayer2 = action.m_player2;
                                    newActions.push_back(act);
                                }
                            }
                        }
                    }
                    //4.解析.tcm (TCBot Macro v1/v2)
                    else if (ext == ".tcm") {
                        std::ifstream f(path, std::ios::binary);
                        if (!f) throw std::runtime_error("Cannot open .tcm file");
                        parseTcm(f, newActions, parsedFps);
                        updateFps = true;
                    }
                    //5.解析.gdr和.gdr2
                    else if (ext == ".gdr2" || ext == ".gdr") {
                        std::vector<gdr::Input<>> inputs;
                        if (ext == ".gdr2") {
                            auto importRes = MyReplay::importData(path.string());
                            if (importRes.isOk()) {
                                inputs = importRes.unwrap().inputs;
                                parsedFps = importRes.unwrap().framerate;
                                updateFps = true;
                            }
                            else {
                                throw std::runtime_error("Failed to parse .gdr2 file");
                            }
                        }
                        else {
                            std::ifstream f(path, std::ios::binary);
                            if (!f) throw std::runtime_error("Cannot open .gdr file");
                            f.seekg(0, std::ios::end);
                            size_t size = f.tellg();
                            f.seekg(0, std::ios::beg);
                            std::vector<uint8_t> data(size);
                            f.read(reinterpret_cast<char*>(data.data()), size);
                            auto convertRes = gdr::convert<MyReplay, gdr::Input<>>(std::span<uint8_t>(data));
                            if (convertRes.isOk()) {
                                inputs = convertRes.unwrap().inputs;
                                parsedFps = convertRes.unwrap().framerate;
                                updateFps = true;
                            }
                            else {
                                throw std::runtime_error("Failed to convert .gdr file");
                            }
                        }
                        std::sort(inputs.begin(), inputs.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
                        for (const auto& input : inputs) {
                            FrameAction act;
                            act.frame = static_cast<int>(input.frame);
                            act.shouldDraw = false;
                            act.frameWindow = 1;
                            act.isPlayer2 = input.player2;
                            newActions.push_back(act);
                        }
                    }

                    //合并动作数据并更新全局配置
                    geode::Loader::get()->queueInMainThread([newActions, parsedFps, updateFps, onSuccessCallback]() {
                        if (newActions.empty()) {
                            auto alert = FLAlertLayer::create("Error", "No valid frames found or empty file.", "OK");
                            alert->show(); stopAlertAnimation(alert);
                            return;
                        }
                        if (updateFps && parsedFps > 0.0) {
                            g_macroFps = parsedFps;
                            saveSettings();
                        }

                        g_frameActions.clear();
                        int addedCount = 0;
                        for (auto& act : newActions) {
                            std::string baseStr = std::to_string(act.frame) + (act.isPlayer2 ? "_1" : "_0");
                            int idx = 0;
                            std::string finalKey = baseStr + "_" + std::to_string(idx);
                            while (g_frameActions.contains(finalKey)) {
                                idx++;
                                finalKey = baseStr + "_" + std::to_string(idx);
                            }
                            g_frameActions[finalKey] = act;
                            addedCount++;
                        }

                        saveFrames();
                        auto alert = FLAlertLayer::create("Success", fmt::format("Loaded {} operations.", addedCount), "OK");
                        alert->show(); stopAlertAnimation(alert);
                        if (onSuccessCallback) onSuccessCallback();
                        });
                }
                catch (const std::exception& e) {
                    std::string errMsg = e.what();
                    geode::Loader::get()->queueInMainThread([errMsg]() {
                        auto alert = FLAlertLayer::create("Error", fmt::format("Failed: {}", errMsg).c_str(), "OK");
                        alert->show(); stopAlertAnimation(alert);
                        });
                }
            }

            if (SUCCEEDED(hr)) {
                CoUninitialize();
            }
            }).detach();
#endif
    }

} 
