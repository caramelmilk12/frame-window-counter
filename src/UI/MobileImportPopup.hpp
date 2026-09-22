#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <vector>
#include <string>

struct MacroFileItem {
    std::filesystem::path path;
    std::string filename;
    bool isExport = false; // false = imports 目录, true = exports 目录
};

class MobileImportPopup : public geode::Popup {
protected:
    std::function<void()> m_onSuccessCallback;
    std::vector<MacroFileItem> m_fileList;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;

    bool init(std::function<void()> onSuccessCallback);
    void scanFiles();
    void buildList();
    void onSelectFile(cocos2d::CCObject* sender);
    void onRefresh(cocos2d::CCObject* sender);
    void onShowPath(cocos2d::CCObject* sender);

public:
    static MobileImportPopup* create(std::function<void()> onSuccessCallback);
    void showInstant();
};