#include "LabelPresetPopup.hpp"
#include "FrameActionPopup.hpp"
#include "../Data/State.hpp"
#include "../Common.hpp"
#include <Geode/ui/ColorPickPopup.hpp>
#include <thread>
#include <set>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#include <commdlg.h>
#endif

using namespace geode::prelude;

bool LabelPresetPopup::init() {
    if (!Popup::init(400.f, 280.f)) return false;
    this->setTitle("Label Interval Settings (0-99)");

    auto size = m_mainLayer->getContentSize();
    float centerX = size.width / 2;

    auto menu = CCMenu::create();
    menu->setPosition({ 0, 0 });
    m_mainLayer->addChild(menu);

    // ----------------- 第一行：ID 与 LOAD -----------------
    auto idLbl = CCLabelBMFont::create("ID:", "bigFont.fnt");
    idLbl->setScale(0.45f);
    idLbl->setPosition({ centerX - 90.f, 235.f });
    m_mainLayer->addChild(idLbl);

    m_idInput = TextInput::create(60.f, "0");
    m_idInput->setPosition({ centerX - 35.f, 235.f });
    m_idInput->setFilter("0123456789");
    m_idInput->setString("0");
    m_mainLayer->addChild(m_idInput);

    auto loadBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Load"), this, menu_selector(LabelPresetPopup::onLoad));
    loadBtn->setPosition({ centerX + 65.f, 235.f });
    menu->addChild(loadBtn);

    // ----------------- 第二行：Swift 切换按钮、Min 与 Max -----------------
    auto swiftLbl = CCLabelBMFont::create("Swift:", "bigFont.fnt");
    swiftLbl->setScale(0.38f);
    swiftLbl->setPosition({ centerX - 145.f, 195.f });
    m_mainLayer->addChild(swiftLbl);

    // Swift 切换按钮：勾选即切换为 Swift 维度，取消勾选即切换为 Frame Window 维度
    m_swiftToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(LabelPresetPopup::onSwiftToggle), 0.65f);
    m_swiftToggle->setPosition({ centerX - 105.f, 195.f });
    menu->addChild(m_swiftToggle);

    m_minLbl = CCLabelBMFont::create("Min Win:", "bigFont.fnt");
    m_minLbl->setScale(0.38f);
    m_minLbl->setPosition({ centerX - 55.f, 195.f });
    m_mainLayer->addChild(m_minLbl);

    m_minInput = TextInput::create(50.f, "0");
    m_minInput->setPosition({ centerX - 5.f, 195.f });
    m_minInput->setFilter("0123456789./");
    m_minInput->setCallback([this](std::string const& text) {
        if (m_currentUseSwift) m_currentMinSwiftStr = text;
        else m_currentMinWindowStr = text;
        this->autoSave();
        });
    m_mainLayer->addChild(m_minInput);

    m_maxLbl = CCLabelBMFont::create("Max Win:", "bigFont.fnt");
    m_maxLbl->setScale(0.38f);
    m_maxLbl->setPosition({ centerX + 55.f, 195.f });
    m_mainLayer->addChild(m_maxLbl);

    m_maxInput = TextInput::create(60.f, "999999");
    m_maxInput->setPosition({ centerX + 115.f, 195.f });
    m_maxInput->setFilter("0123456789./");
    m_maxInput->setCallback([this](std::string const& text) {
        if (m_currentUseSwift) m_currentMaxSwiftStr = text;
        else m_currentMaxWindowStr = text;
        this->autoSave();
        });
    m_mainLayer->addChild(m_maxInput);

    // ----------------- 第三行至底部：原有设置 -----------------
    m_textInput = TextInput::create(260.f, "HUD Display Text");
    m_textInput->setPosition({ centerX, 155.f });
    m_textInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
    m_textInput->setCallback([this](std::string const&) { this->autoSave(); });
    m_mainLayer->addChild(m_textInput);

    m_audioInput = TextInput::create(190.f, "Audio Path");
    m_audioInput->setPosition({ centerX - 50.f, 115.f });
    m_audioInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
    m_audioInput->setCallback([this](std::string const&) { this->autoSave(); });
    m_mainLayer->addChild(m_audioInput);

    auto browseSpr = ButtonSprite::create("Browse");
    browseSpr->setScale(0.7f);
    auto browseBtn = CCMenuItemSpriteExtra::create(browseSpr, this, menu_selector(LabelPresetPopup::onBrowseAudio));
    browseBtn->setPosition({ centerX + 110.f, 115.f });
    menu->addChild(browseBtn);

    auto colorLabel = CCLabelBMFont::create("HUD Color:", "bigFont.fnt");
    colorLabel->setScale(0.5f);
    colorLabel->setPosition({ centerX - 110.f, 75.f });
    m_mainLayer->addChild(colorLabel);

    m_colorSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    m_colorSprite->setColor({
        static_cast<GLubyte>(m_currentColor.r * 255),
        static_cast<GLubyte>(m_currentColor.g * 255),
        static_cast<GLubyte>(m_currentColor.b * 255)
        });
    m_colorSprite->setOpacity(static_cast<GLubyte>(m_currentColor.a * 255));

    auto colorWrapper = CCNode::create();
    colorWrapper->setContentSize(m_colorSprite->getContentSize());
    m_colorSprite->setPosition(colorWrapper->getContentSize() / 2);
    colorWrapper->addChild(m_colorSprite);
    colorWrapper->setScale(0.65f);

    auto colorBtn = CCMenuItemSpriteExtra::create(colorWrapper, this, menu_selector(LabelPresetPopup::onColorBtn));
    colorBtn->setPosition({ centerX - 50.f, 75.f });
    menu->addChild(colorBtn);

    auto hudLabel = CCLabelBMFont::create("Show in HUD:", "bigFont.fnt");
    hudLabel->setScale(0.5f);
    hudLabel->setPosition({ centerX + 40.f, 75.f });
    m_mainLayer->addChild(hudLabel);

    m_hudToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(LabelPresetPopup::onHudToggle), 0.7f);
    m_hudToggle->setPosition({ centerX + 120.f, 75.f });
    menu->addChild(m_hudToggle);

    auto switchBtnSpr = ButtonSprite::create("<- Back");
    switchBtnSpr->setScale(0.6f);
    auto switchBtn = CCMenuItemSpriteExtra::create(switchBtnSpr, this, menu_selector(LabelPresetPopup::onSwitchToFrames));
    switchBtn->setPosition({ centerX - 110.f, 30.f });
    menu->addChild(switchBtn);

    auto applyBtnSpr = ButtonSprite::create("Apply to Wins");
    applyBtnSpr->setScale(0.6f);
    auto applyBtn = CCMenuItemSpriteExtra::create(applyBtnSpr, this, menu_selector(LabelPresetPopup::onApplyColorToWins));
    applyBtn->setPosition({ centerX, 30.f });
    menu->addChild(applyBtn);

    auto resetBtnSpr = ButtonSprite::create("Reset All");
    resetBtnSpr->setScale(0.6f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetBtnSpr, this, menu_selector(LabelPresetPopup::onResetAll));
    resetBtn->setPosition({ centerX + 110.f, 30.f });
    menu->addChild(resetBtn);

    this->onLoad(nullptr);
    return true;
}

void LabelPresetPopup::onSwiftToggle(CCObject* sender) {
    if (auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender)) {
        // 1. 保存切换前的当前数值
        if (m_currentUseSwift) {
            m_currentMinSwiftStr = m_minInput->getString();
            m_currentMaxSwiftStr = m_maxInput->getString();
        }
        else {
            m_currentMinWindowStr = m_minInput->getString();
            m_currentMaxWindowStr = m_maxInput->getString();
        }

        // 2. 状态取反
        m_currentUseSwift = !toggle->isToggled();

        // 3. 动态切换输入框文本及 Min/Max 提示文字
        if (m_currentUseSwift) {
            if (m_minLbl) m_minLbl->setString("Min Sw:");
            if (m_maxLbl) m_maxLbl->setString("Max Sw:");
            m_minInput->setString(m_currentMinSwiftStr);
            m_maxInput->setString(m_currentMaxSwiftStr);
        }
        else {
            if (m_minLbl) m_minLbl->setString("Min Win:");
            if (m_maxLbl) m_maxLbl->setString("Max Win:");
            m_minInput->setString(m_currentMinWindowStr);
            m_maxInput->setString(m_currentMaxWindowStr);
        }

        this->autoSave();
    }
}

void LabelPresetPopup::onApplyColorToWins(CCObject*) {
    std::string minStr = m_minInput->getString();
    std::string maxStr = m_maxInput->getString();

    int minV = static_cast<int>(std::round(parseWindowExpr(minStr, 0.0)));
    int maxV = static_cast<int>(std::round(parseWindowExpr(maxStr, 999999.0)));

    if (minV < 0) minV = 0;
    if (minV > maxV) {
        auto alert = FLAlertLayer::create("Error", "Min value cannot be greater than Max value.", "OK");
        alert->show(); stopAlertAnimation(alert);
        return;
    }
    if (maxV - minV + 1 > 1000) {
        auto alert = FLAlertLayer::create("Error", "Range too large!\nTotal count cannot exceed 1000", "OK");
        alert->show(); stopAlertAnimation(alert);
        return;
    }

    int count = 0;

    if (m_currentUseSwift) {
        // ----------------- 勾选 Swift 状态 -----------------
        // 同步 Swift 区间 [minV, maxV] 内的所有预设颜色（不限 window）
        for (auto& [key, preset] : g_windowPresets) {
            if (preset.swift >= minV && preset.swift <= maxV) {
                preset.color = m_currentColor;
                count++;
            }
        }

        // 收集已有预设中出现过的所有window值（若为空则默认 1.0），补全区间内缺失的预设
        std::set<double> existingWindows;
        for (auto const& [key, preset] : g_windowPresets) {
            existingWindows.insert(preset.window);
        }
        if (existingWindows.empty()) {
            existingWindows.insert(1.0);
        }

        for (int s = minV; s <= maxV; s++) {
            for (double w : existingWindows) {
                auto key = makeWindowPresetKey(s, w);
                if (!g_windowPresets.contains(key)) {
                    FrameWindowPreset p;
                    p.swift = s;
                    p.window = w;
                    p.color = m_currentColor;
                    g_windowPresets[key] = p;
                    count++;
                }
            }
        }

        saveSettings();
        triggerHUDRefresh();
        auto alert = FLAlertLayer::create("Success", fmt::format("Applied color to {} presets under Swift range ({} - {})!", count, minV, maxV), "OK");
        alert->show(); stopAlertAnimation(alert);
    }
    else {
        // ----------------- 未勾选 Swift 状态 -----------------
        // 同步 window 在 [minV, maxV] 区间内的所有预设颜色
        for (auto& [key, preset] : g_windowPresets) {
            if (preset.window >= minV && preset.window <= maxV) {
                preset.color = m_currentColor;
                count++;
            }
        }

        // 收集已有预设中出现过的所有swift，补全区间内缺失的预设
        std::set<int> existingSwifts;
        for (auto const& [key, preset] : g_windowPresets) {
            existingSwifts.insert(preset.swift);
        }
        existingSwifts.insert(0);

        for (int s : existingSwifts) {
            for (int i = minV; i <= maxV; i++) {
                auto key = makeWindowPresetKey(s, static_cast<double>(i));
                if (!g_windowPresets.contains(key)) {
                    FrameWindowPreset p;
                    p.swift = s;
                    p.window = static_cast<double>(i);
                    p.color = m_currentColor;
                    g_windowPresets[key] = p;
                    count++;
                }
            }
        }

        saveSettings();
        triggerHUDRefresh();
        auto alert = FLAlertLayer::create("Success", fmt::format("Applied color to {} presets across all Swifts for Windows ({} - {})!", count, minV, maxV), "OK");
        alert->show(); stopAlertAnimation(alert);
    }
}

void LabelPresetPopup::onBrowseAudio(CCObject*) {
#ifdef GEODE_IS_WINDOWS
    Ref<LabelPresetPopup> safeThis = this;

    HWND parentHwnd = GetActiveWindow();
    if (!parentHwnd) {
        parentHwnd = WindowFromDC(wglGetCurrentDC());
    }

    std::thread([safeThis, parentHwnd]() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        char filename[MAX_PATH] = { 0 };
        OPENFILENAMEA ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = parentHwnd;
        ofn.lpstrFilter = "Audio Files (*.ogg;*.mp3;*.wav)\0*.ogg;*.mp3;*.wav\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        ofn.lpstrDefExt = "ogg";

        if (GetOpenFileNameA(&ofn)) {
            std::string pathStr(filename);
            geode::Loader::get()->queueInMainThread([safeThis, pathStr]() {
                if (safeThis && safeThis->getParent() && safeThis->m_audioInput) {
                    safeThis->m_audioInput->setString(pathStr);
                    safeThis->autoSave();
                }
                });
        }

        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
        }).detach();
#endif
}

void LabelPresetPopup::onColorBtn(CCObject*) {
    Ref<LabelPresetPopup> safeThis = this;

    auto popup = geode::ColorPickPopup::create({
        static_cast<GLubyte>(m_currentColor.r * 255),
        static_cast<GLubyte>(m_currentColor.g * 255),
        static_cast<GLubyte>(m_currentColor.b * 255),
        static_cast<GLubyte>(m_currentColor.a * 255)
        });

    popup->setCallback([safeThis](cocos2d::ccColor4B color) {
        if (!safeThis || !safeThis->getParent()) return;

        safeThis->m_currentColor = {
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            color.a / 255.f
        };

        if (safeThis->m_colorSprite) {
            safeThis->m_colorSprite->setColor({ color.r, color.g, color.b });
            safeThis->m_colorSprite->setOpacity(color.a);
        }

        safeThis->autoSave();
        });

    popup->show();
}

void LabelPresetPopup::onHudToggle(CCObject* sender) {
    if (auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender)) {
        m_currentShowInHud = !toggle->isToggled();
        this->autoSave();
    }
}

void LabelPresetPopup::autoSave() {
    std::string idStr = m_idInput->getString();
    if (idStr.empty()) return;

    LabelPreset p;
    try { p.id = std::stoi(idStr); }
    catch (...) { p.id = 0; }

    p.useSwift = m_currentUseSwift;
    p.minWindowStr = m_currentMinWindowStr;
    p.maxWindowStr = m_currentMaxWindowStr;
    p.minSwiftStr = m_currentMinSwiftStr;
    p.maxSwiftStr = m_currentMaxSwiftStr;
    p.text = m_textInput->getString();
    p.audioPath = m_audioInput->getString();
    p.color = m_currentColor;
    p.showInHud = m_currentShowInHud;
    p.updateBounds();

    g_labelPresets[idStr] = p;
    saveSettings();

    triggerHUDRefresh();
}

void LabelPresetPopup::onLoad(CCObject*) {
    std::string idStr = m_idInput->getString();
    if (g_labelPresets.contains(idStr)) {
        auto& p = g_labelPresets[idStr];
        m_currentUseSwift = p.useSwift;
        m_swiftToggle->toggle(m_currentUseSwift);

        m_currentMinWindowStr = p.minWindowStr;
        m_currentMaxWindowStr = p.maxWindowStr;
        m_currentMinSwiftStr = p.minSwiftStr;
        m_currentMaxSwiftStr = p.maxSwiftStr;

        if (m_currentUseSwift) {
            if (m_minLbl) m_minLbl->setString("Min Sw:");
            if (m_maxLbl) m_maxLbl->setString("Max Sw:");
            m_minInput->setString(p.minSwiftStr);
            m_maxInput->setString(p.maxSwiftStr);
        }
        else {
            if (m_minLbl) m_minLbl->setString("Min Win:");
            if (m_maxLbl) m_maxLbl->setString("Max Win:");
            m_minInput->setString(p.minWindowStr);
            m_maxInput->setString(p.maxWindowStr);
        }

        m_textInput->setString(p.text);
        m_audioInput->setString(p.audioPath);

        m_currentColor = p.color;
        if (m_colorSprite) {
            m_colorSprite->setColor({
                static_cast<GLubyte>(p.color.r * 255),
                static_cast<GLubyte>(p.color.g * 255),
                static_cast<GLubyte>(p.color.b * 255)
                });
            m_colorSprite->setOpacity(static_cast<GLubyte>(p.color.a * 255));
        }

        m_currentShowInHud = p.showInHud;
        m_hudToggle->toggle(m_currentShowInHud);
    }
}

void LabelPresetPopup::onResetAll(CCObject*) {
    Ref<LabelPresetPopup> safeThis = this;

    auto alert = geode::createQuickPopup(
        "Reset All Labels",
        "Are you sure you want to reset <cr>ALL 100 labels</c>?",
        "Cancel", "Reset",
        [safeThis](auto, bool btn2) {
            if (btn2) {
                g_labelPresets.clear();
                for (int i = 0; i <= 99; i++) {
                    LabelPreset p;
                    p.id = i;
                    p.useSwift = false;
                    p.minWindowStr = "";
                    p.maxWindowStr = "";
                    p.minSwiftStr = "";
                    p.maxSwiftStr = "";
                    p.text = std::to_string(i);
                    p.audioPath = "";
                    p.color = { 1.f, 1.f, 1.f, 1.f };
                    p.showInHud = false;
                    p.updateBounds();
                    g_labelPresets[std::to_string(i)] = p;
                }
                saveSettings();
                triggerHUDRefresh();

                if (safeThis && safeThis->getParent()) {
                    safeThis->onLoad(nullptr);
                }

                auto successAlert = FLAlertLayer::create("Success", "All labels have been reset.", "OK");
                successAlert->show();
                stopAlertAnimation(successAlert);
            }
        }
    );
    stopAlertAnimation(alert);
}

void LabelPresetPopup::onSwitchToFrames(CCObject*) {
    this->removeFromParentAndCleanup(true);
    auto popup = FrameActionPopup::create();
    if (popup) {
        popup->setID("FrameActionPopup"_spr);
        popup->showInstant();
    }
}

LabelPresetPopup* LabelPresetPopup::create() {
    auto ret = new LabelPresetPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void LabelPresetPopup::showInstant() {
    this->show();
    if (this->m_mainLayer) {
        this->m_mainLayer->stopAllActions();
        this->m_mainLayer->setScale(1.0f);
    }
    if (this->m_bgSprite) {
        this->m_bgSprite->stopAllActions();
        this->m_bgSprite->setOpacity(150);
    }
}

void LabelPresetPopup::instantClose() {
    this->removeFromParentAndCleanup(true);
}