#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <vector>
#include <string>

class MobileAudioPickerPopup : public geode::Popup {
protected:
    std::function<void(std::string)> m_callback;
    std::vector<std::filesystem::path> m_fileList;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;

    bool init(std::function<void(std::string)> callback);
    void scanFiles();
    void buildList();
    void onSelectFile(cocos2d::CCObject* sender);
    void onRefresh(cocos2d::CCObject* sender);
    void onShowPath(cocos2d::CCObject* sender);

public:
    static MobileAudioPickerPopup* create(std::function<void(std::string)> callback);
    void showInstant();
};