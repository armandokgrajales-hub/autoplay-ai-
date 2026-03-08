#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;
using namespace cocos2d;

static bool g_aiEnabled  = false;
static bool g_buttonHeld = false;

static constexpr float DETECTION_RANGE_X = 120.0f;
static constexpr float DETECTION_RANGE_Y = 80.0f;

static const std::vector<int> HAZARD_IDS = {
    8, 39, 40, 49,
    148, 149, 150,
    184, 185,
    1, 2, 3, 4,
    395, 396,
    1331, 1332,
};

bool isHazardObject(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType == GameObjectType::Decoration) return false;
    int objID = obj->m_objectID;
    for (int id : HAZARD_IDS) {
        if (objID == id) return true;
    }
    return false;
}

class $modify(AIPlayLayer, PlayLayer) {

    void update(float dt) {
        PlayLayer::update(dt);

        if (!g_aiEnabled) return;
        if (!m_player1 || m_player1->m_isDead) return;

        CCPoint playerPos = m_player1->getPosition();
        bool shouldPress = false;

        if (m_objects) {
            for (unsigned int i = 0; i < m_objects->count(); i++) {
                auto* obj = static_cast<GameObject*>(m_objects->objectAtIndex(i));
                if (!obj) continue;
                if (obj->m_objectType == GameObjectType::Decoration) continue;

                CCPoint objPos = obj->getPosition();
                float deltaX = objPos.x - playerPos.x;
                if (deltaX < 0.0f || deltaX > DETECTION_RANGE_X) continue;

                float deltaY = std::abs(objPos.y - playerPos.y);
                if (deltaY > DETECTION_RANGE_Y) continue;

                if (isHazardObject(obj)) {
                    shouldPress = true;
                    break;
                }

                if (obj->m_objectType == GameObjectType::Solid) {
                    if (objPos.y >= playerPos.y - 20.0f && deltaX < 80.0f) {
                        shouldPress = true;
                        break;
                    }
                }
            }
        }

        if (shouldPress && !g_buttonHeld) {
            m_player1->pushButton(PlayerButton::Jump);
            g_buttonHeld = true;
        } else if (!shouldPress && g_buttonHeld) {
            m_player1->releaseButton(PlayerButton::Jump);
            g_buttonHeld = false;
        }
    }

    void resetLevel() {
        AIPlayLayer::resetLevel();
        g_buttonHeld = false;
        if (m_player1) {
            m_player1->releaseButton(PlayerButton::Jump);
        }
    }

    void onQuit() {
        g_buttonHeld = false;
        AIPlayLayer::onQuit();
    }
};

class $modify(AIPauseLayer, PauseLayer) {

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto labelOn  = CCLabelBMFont::create("AI: ON",  "bigFont.fnt");
        auto labelOff = CCLabelBMFont::create("AI: OFF", "bigFont.fnt");
        labelOn->setScale(0.6f);
        labelOff->setScale(0.6f);

        auto itemOn  = CCMenuItemLabel::create(labelOn,  this, menu_selector(AIPauseLayer::onAIToggle));
        auto itemOff = CCMenuItemLabel::create(labelOff, this, menu_selector(AIPauseLayer::onAIToggle));

        auto toggleBtn = CCMenuItemToggle::createWithTarget(
            this, menu_selector(AIPauseLayer::onAIToggle),
            itemOff, itemOn, nullptr
        );

        toggleBtn->setSelectedIndex(g_aiEnabled ? 1 : 0);
        labelOn->setColor({0, 255, 100});
        labelOff->setColor({255, 80, 80});

        auto menu = CCMenu::create();
        menu->addChild(toggleBtn);
        menu->setPosition({winSize.width - 60.0f, 30.0f});
        menu->setZOrder(10);
        this->addChild(menu);

        auto descLabel = CCLabelBMFont::create("AutoPlay AI", "chatFont.fnt");
        descLabel->setScale(0.5f);
        descLabel->setPosition({winSize.width - 60.0f, 52.0f});
        descLabel->setColor({255, 255, 255});
        descLabel->setOpacity(180);
        this->addChild(descLabel, 10);
    }

    void onAIToggle(CCObject* sender) {
        g_aiEnabled = !g_aiEnabled;

        std::string msg = g_aiEnabled ? "AutoPlay AI: ON" : "AutoPlay AI: OFF";
        Notification::create(msg, NotificationIcon::Success)->show();

        if (!g_aiEnabled && g_buttonHeld) {
            auto* pl = PlayLayer::get();
            if (pl && pl->m_player1) {
                pl->m_player1->releaseButton(PlayerButton::Jump);
            }
            g_buttonHeld = false;
        }

        log::info("AutoPlay AI: {}", g_aiEnabled ? "ON" : "OFF");
    }
};

$on_mod(Loaded) {
    log::info("AutoPlay AI v1.0.0 loaded.");
}
