#pragma once
#include <Geode/Geode.hpp>
#include <string>

class MobileExportPopup : public geode::Popup {
protected:
    geode::TextInput* m_nameInput = nullptr;

    bool init();
    void onExportFWC(cocos2d::CCObject* sender);
    void onExportJSON(cocos2d::CCObject* sender);
    void doExport(bool isJson);

public:
    static MobileExportPopup* create();
    void showInstant();
};