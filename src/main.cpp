#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;
using namespace cocos2d;

static bool g_aiEnabled  = false;
static bool g_buttonHeld = false;

static constexpr float LOOKAHEAD_X     = 150.0f;
static constexpr float LOOKAHEAD_Y     = 90.0f;
static constexpr float SOLID_LOOKAHEAD = 90.0f;
static constexpr float SOLID_Y_MARGIN  = 25.0f;

static const std::unordered_set<int> HAZARD_IDS = {
    8, 39, 40, 49,
    395, 396,
    148, 149, 150,
    184, 185,
    1616, 1617,
    1619, 1620,
    1331, 1332,
    2015, 2016,
};

static bool isHazard(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType == GameObjectType::Decoration) return false;
    return HAZARD_IDS.count(obj->m_objectID) > 0;
}

class $modify(AIPlayLayer, PlayLayer) {

    void update(float dt) {
        PlayLayer::update(dt);

        if (!g_aiEnabled) return;

        auto* player = m_player1;
        if (!player) return;

        if (player->m_isDead) {
            if (g_buttonHeld) {
                player->releaseButton(PlayerButton::Jump);
                g_buttonHeld = false;
            }
            return;
        }

        CCPoint playerPos = player->getPosition();
        bool shouldPress = false;

        auto* objects = m_objects;
        if (!objects) return;

        unsigned int count = objects->count();

        for (unsigned int i = 0; i < count; i++) {
            if (!m_objects || i >= m_objects->count()) break;

            auto* raw = objects->objectAtIndex(i);
            if (!raw) continue;

            auto* obj = typeinfo_cast<GameObject*>(raw);
            if (!obj) continue;

            if (obj->m_objectType == GameObjectType::Decoration) continue;
            if (!obj->isVisible()) continue;

            CCPoint objPos = obj->getPosition();
            float deltaX = objPos.x - playerPos.x;
            if (deltaX < -10.0f || deltaX > LOOKAHEAD_X) continue;

            float deltaY = std::abs(objPos.y - playerPos.y);
            if (deltaY > LOOKAHEAD_Y) continue;

            if (isHazard(obj)) {
                shouldPress = true;
                break;
            }

            if (obj->m_objectType == GameObjectType::Solid) {
                if (deltaX < SOLID_LOOKAHEAD &&
                    objPos.y >= playerPos.y - SOLID_Y_MARGIN) {
                    shouldPress = true;
                    break;
                }
            }
        }

        if (shouldPress && !g_buttonHeld) {
            if (m_player1 && !m_player1->m_isDead) {
                m_player1->pushButton(PlayerButton::Jump);
                g_buttonHeld = true;
            }
        } else if (!shouldPress && g_buttonHeld) {
            if (m_player1) {
                m_player1->releaseButton(PlayerButton::Jump);
            }
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
        if (m_player1) {
            m_player1->releaseButton(PlayerButton::Jump);
        }
        AIPlayLayer::onQuit();
    }
};

class $modify(AIPauseLayer, PauseLayer) {

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto labelOn  = CCLabelBMFont::create("AI: ON",  "bigFont.fnt");
        auto labelOff = CCLabelBMFont::create("AI: OFF", "bigFont.fnt");
        labelOn->setScale(0.55f);
        labelOff->setScale(0.55f);
        labelOn->setColor({0, 255, 100});
        labelOff->setColor({255, 80, 80});

        auto itemOn  = CCMenuItemLabel::create(labelOn,  this, menu_selector(AIPauseLayer::onAIToggle));
        auto itemOff = CCMenuItemLabel::create(labelOff, this, menu_selector(AIPauseLayer::onAIToggle));

        auto toggleBtn = CCMenuItemToggle::createWithTarget(
            this, menu_selector(AIPauseLayer::onAIToggle),
            itemOff, itemOn, nullptr
        );
        toggleBtn->setSelectedIndex(g_aiEnabled ? 1 : 0);

        auto menu = CCMenu::create();
        menu->addChild(toggleBtn);
        menu->setPosition({winSize.width - 55.0f, 28.0f});
        menu->setZOrder(10);
        this->addChild(menu);

        auto descLabel = CCLabelBMFont::create("AutoPlay AI", "chatFont.fnt");
        descLabel->setScale(0.45f);
        descLabel->setPosition({winSize.width - 55.0f, 50.0f});
        descLabel->setColor({255, 255, 255});
        descLabel->setOpacity(160);
        this->addChild(descLabel, 10);
    }

    void onAIToggle(CCObject* sender) {
        g_aiEnabled = !g_aiEnabled;

        Notification::create(
            g_aiEnabled ? "AutoPlay AI: ON" : "AutoPlay AI: OFF",
            NotificationIcon::Success
        )->show();

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
    log::info("AutoPlay AI v1.0.0 loaded - Extreme Demon ready.");
}
