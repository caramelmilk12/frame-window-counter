#pragma once
#include <Geode/Geode.hpp>

class LabelPresetPopup : public geode::Popup {
protected:
    geode::TextInput* m_idInput = nullptr;
    geode::TextInput* m_minInput = nullptr;
    geode::TextInput* m_maxInput = nullptr;
    geode::TextInput* m_textInput = nullptr;
    geode::TextInput* m_audioInput = nullptr;

    cocos2d::CCLabelBMFont* m_minLbl = nullptr;
    cocos2d::CCLabelBMFont* m_maxLbl = nullptr;
    CCMenuItemToggler* m_swiftToggle = nullptr;             // Swift 切换按钮
    CCMenuItemToggler* m_hudToggle = nullptr;
    cocos2d::CCSprite* m_colorSprite = nullptr;

    bool m_currentUseSwift = false;                         // 当前是否处于 Swift 模式
    std::string m_currentMinWindowStr = "";
    std::string m_currentMaxWindowStr = "";
    std::string m_currentMinSwiftStr = "";
    std::string m_currentMaxSwiftStr = "";
    bool m_currentShowInHud = false;
    cocos2d::ccColor4F m_currentColor = { 1.f, 1.f, 1.f, 1.f };

    bool init();
    void onLoad(cocos2d::CCObject*);
    void autoSave();
    void onColorBtn(cocos2d::CCObject*);
    void onBrowseAudio(cocos2d::CCObject*);
    void onHudToggle(cocos2d::CCObject*);
    void onApplyColorToWins(cocos2d::CCObject*);
    void onResetAll(cocos2d::CCObject*);
    void onSwitchToFrames(cocos2d::CCObject*);
    void onSwiftToggle(cocos2d::CCObject* sender);

public:
    static LabelPresetPopup* create();
    void showInstant();
    void instantClose();
};