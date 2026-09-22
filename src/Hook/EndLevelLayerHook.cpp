#include <Geode/Geode.hpp>

#if defined(GEODE_IS_MOBILE)
#include <Geode/modify/EndLevelLayer.hpp>
#include "../Common.hpp"

using namespace geode::prelude;

class $modify(MyEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();

        // 当通关结算界面正式生成并弹出时，彻底清理场上遗留的所有 Mod 弹窗（移动端专属）
        closeAllModPopups();
    }
};
#endif