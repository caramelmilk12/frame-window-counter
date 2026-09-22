#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>

#ifndef GEODE_IS_MOBILE
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#endif

#include <Geode/modify/PauseLayer.hpp>
#include "../Data/State.hpp"
#include "../Common.hpp"
#include "../UI/AddFramePopup.hpp"
#include "../UI/FrameActionPopup.hpp"
#include "../UI/PrecisionSettingsPopup.hpp"
#include "../UI/LStarCalcSettingsPopup.hpp"
#include "../UI/LabelPresetPopup.hpp"
#include "../UI/WindowPresetPopup.hpp"

using namespace geode::prelude;

static bool isAnyTextInputFocused(CCNode* root) {
    if (!root) return false;

    if (auto textInput = typeinfo_cast<CCTextInputNode*>(root)) {
        if (textInput->m_selected) {
            return true;
        }
    }

    if (auto children = root->getChildren()) {
        for (int i = 0; i < children->count(); i++) {
            auto child = static_cast<CCNode*>(children->objectAtIndex(i));
            if (isAnyTextInputFocused(child)) {
                return true;
            }
        }
    }
    return false;
}

static void toggleModPopups(CCScene* scene) {
    if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
    if (isAnyTextInputFocused(scene)) return;

    bool closedAny = false;
    if (auto children = scene->getChildren()) {
        for (int i = children->count() - 1; i >= 0; i--) {
            auto child = static_cast<CCNode*>(children->objectAtIndex(i));

            if (typeinfo_cast<AddFramePopup*>(child) ||
                typeinfo_cast<FrameActionPopup*>(child) ||
                typeinfo_cast<PrecisionSettingsPopup*>(child) ||
                typeinfo_cast<LStarCalcSettingsPopup*>(child) ||
                typeinfo_cast<LabelPresetPopup*>(child) ||
                typeinfo_cast<WindowPresetPopup*>(child)) {
                child->removeFromParentAndCleanup(true);
                closedAny = true;
            }
        }
    }

    if (!closedAny) {
        if (auto popup = FrameActionPopup::create()) {
            popup->setID("FrameActionPopup"_spr);
            popup->showInstant();
        }
    }
}

#ifndef GEODE_IS_MOBILE
class $modify(MyKeyboardDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool isKeyDown, bool isKeyRepeat, double time) {
        if (isKeyDown && !isKeyRepeat && key == KEY_O) {
            if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
                toggleModPopups(scene);
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, isKeyDown, isKeyRepeat, time);
    }
};
#endif

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
class FWCMenuCallback : public cocos2d::CCObject {
public:
    void onOpenFWC(CCObject*) {
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            toggleModPopups(scene);
        }
    }
    static FWCMenuCallback* get() {
        static auto instance = new FWCMenuCallback();
        return instance;
    }
};

class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = this->getChildByID("center-button-menu");
        if (!menu) menu = this->getChildByID("bottom-button-menu");

        if (menu) {
            auto spr = ButtonSprite::create("FWC", 20, true, "goldFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr,
                FWCMenuCallback::get(),
                menu_selector(FWCMenuCallback::onOpenFWC)
            );
            btn->setID("open-fwc-btn"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }
    }
};
#endif

class $modify(MyDirector, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();

        auto scene = this->getRunningScene();
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - g_lastAutoSaveTime).count() >= AUTOSAVETIME) {
            g_lastAutoSaveTime = now;
            if (auto pl = PlayLayer::get(); pl && pl->m_level) {
                doAutoSave(pl->m_level);
            }
        }

        if (g_modEnabled) {
            if (auto playLayer = PlayLayer::get()) {
                if (!playLayer->m_isPaused && playLayer->m_player1 && !playLayer->m_player1->m_isDead) {
                    if (auto popup = typeinfo_cast<FrameActionPopup*>(scene->getChildByID("FrameActionPopup"_spr))) {
                        int currentFrame = static_cast<int>(playLayer->m_gameState.m_levelTime * g_macroFps);
                        popup->doTrackingTick(currentFrame);
                    }
                }
            }
        }
    }
};