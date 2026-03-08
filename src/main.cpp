#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <unordered_set>
#include <cmath>
#include <string>

using namespace geode::prelude;
using namespace cocos2d;

// ============================================================
//  CONFIGURACION GLOBAL
// ============================================================

struct AIConfig {
    bool  aiEnabled     = false;
    bool  safeMode      = false;
    bool  speedhack     = false;
    float speedMult     = 1.0f;
    bool  speedMusic    = false;
    bool  speedProgress = false;
    bool  tpsBypass     = false;
    float tpsValue      = 240.0f;
};

static AIConfig g_cfg;
static bool     g_buttonHeld = false;

// ============================================================
//  IDs PELIGROSOS - hitbox mortal confirmada en GD 2.2
// ============================================================

static const std::unordered_set<int> HAZARD_IDS = {
    // Spikes normales
    8, 39, 40, 49,
    // Spikes invertidos
    395, 396,
    // Saws
    148, 149, 150,
    // Sawblades
    184, 185,
    // Spikes de esquina
    1616, 1617,
    // Spikes de pared
    1619, 1620,
    // Obstaculos especiales
    1331, 1332,
    // Nuevos en 2.2
    2015, 2016, 1705, 1706,
};

// Distancias de deteccion (en unidades del juego)
static constexpr float HAZARD_X    = 160.0f;
static constexpr float HAZARD_Y    = 95.0f;
static constexpr float SOLID_X     = 95.0f;
static constexpr float SOLID_Y     = 28.0f;
// Maximo de objetos revisados por frame - evita lag/crash
static constexpr unsigned int MAX_OBJ_PER_FRAME = 250;

// ============================================================
//  HELPERS
// ============================================================

static bool isHazard(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType == GameObjectType::Decoration) return false;
    if (obj->m_isNoTouch) return false;
    return HAZARD_IDS.count(obj->m_objectID) > 0;
}

static bool isSolid(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType != GameObjectType::Solid) return false;
    if (obj->m_isNoTouch) return false;
    return true;
}

static float parseFloat(CCTextInputNode* input, float fallback) {
    if (!input) return fallback;
    std::string s(input->getString());
    if (s.empty()) return fallback;
    try {
        return std::stof(s);
    } catch (...) {
        return fallback;
    }
}

// ============================================================
//  HOOK: CCScheduler - TPS Bypass
// ============================================================

class $modify(AIScheduler, CCScheduler) {
    void update(float dt) {
        if (g_cfg.tpsBypass && PlayLayer::get()) {
            CCScheduler::update(1.0f / g_cfg.tpsValue);
        } else {
            CCScheduler::update(dt);
        }
    }
};

// ============================================================
//  HOOK: FMODAudioEngine - Speed en musica
// ============================================================

class $modify(AIAudio, FMODAudioEngine) {
    void update(float dt) {
        if (g_cfg.speedhack && g_cfg.speedMusic && PlayLayer::get()) {
            FMODAudioEngine::update(dt * g_cfg.speedMult);
        } else {
            FMODAudioEngine::update(dt);
        }
    }
};

// ============================================================
//  HOOK: PlayLayer - IA + SafeMode + Speedhack
// ============================================================

class $modify(AIPlayLayer, PlayLayer) {

    void update(float dt) {
        float useDt = (g_cfg.speedhack && !g_cfg.speedProgress)
                      ? dt * g_cfg.speedMult : dt;
        PlayLayer::update(useDt);

        if (!g_cfg.aiEnabled) return;

        auto* player = m_player1;
        if (!player) return;

        if (player->m_isDead) {
            if (g_buttonHeld) {
                player->releaseButton(PlayerButton::Jump);
                g_buttonHeld = false;
            }
            return;
        }

        auto* objects = m_objects;
        if (!objects || objects->count() == 0) return;

        CCPoint playerPos    = player->getPosition();
        bool    shouldPress  = false;
        unsigned int total   = objects->count();
        unsigned int checked = 0;

        for (unsigned int i = 0; i < total; i++) {
            // Seguridad: si el array cambio de tamano, abortar
            if (!m_objects || m_objects->count() != total) break;
            if (checked >= MAX_OBJ_PER_FRAME) break;

            CCObject* raw = objects->objectAtIndex(i);
            if (!raw) continue;

            auto* obj = typeinfo_cast<GameObject*>(raw);
            if (!obj) continue;
            if (!obj->isVisible()) continue;
            if (obj->m_objectType == GameObjectType::Decoration) continue;
            if (obj->m_isNoTouch) continue;

            checked++;

            CCPoint objPos = obj->getPosition();
            float   dX     = objPos.x - playerPos.x;

            if (dX < -10.0f || dX > HAZARD_X) continue;

            float dY = std::abs(objPos.y - playerPos.y);
            if (dY > HAZARD_Y) continue;

            if (isHazard(obj)) {
                shouldPress = true;
                break;
            }

            if (isSolid(obj)) {
                if (dX < SOLID_X && objPos.y >= playerPos.y - SOLID_Y) {
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
            if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
            g_buttonHeld = false;
        }
    }

    // Safe Mode: noclip sin registrar verificacion
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_cfg.safeMode) return;
        AIPlayLayer::destroyPlayer(player, obj);
    }

    void resetLevel() {
        AIPlayLayer::resetLevel();
        g_buttonHeld = false;
        if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
    }

    void onQuit() {
        g_buttonHeld = false;
        if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
        AIPlayLayer::onQuit();
    }
};

// ============================================================
//  GUI QOL - PauseLayer
// ============================================================

// Tags para los inputs
static constexpr int TAG_SPEED_INPUT = 300;
static constexpr int TAG_TPS_INPUT   = 301;

class $modify(AIPauseLayer, PauseLayer) {

    // --- Helpers de UI ---

    CCMenuItemToggle* makeToggle(const char* onText, const char* offText,
                                  bool state, SEL_MenuHandler sel) {
        auto lOn  = CCLabelBMFont::create(onText,  "bigFont.fnt");
        auto lOff = CCLabelBMFont::create(offText, "bigFont.fnt");
        lOn->setScale(0.38f);
        lOff->setScale(0.38f);
        lOn->setColor({0, 220, 80});
        lOff->setColor({220, 60, 60});

        auto iOn  = CCMenuItemLabel::create(lOn,  this, sel);
        auto iOff = CCMenuItemLabel::create(lOff, this, sel);

        auto tog = CCMenuItemToggle::createWithTarget(this, sel, iOff, iOn, nullptr);
        tog->setSelectedIndex(state ? 1 : 0);
        return tog;
    }

    CCLabelBMFont* makeRowLabel(const char* text) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.34f);
        lbl->setColor({200, 220, 255});
        return lbl;
    }

    CCTextInputNode* makeInput(const char* placeholder, float w, int tag) {
        auto node = CCTextInputNode::create(w, 20.0f, placeholder, "bigFont.fnt");
        node->setMaxLabelScale(0.38f);
        node->setAllowedChars("0123456789.");
        node->setTag(tag);
        return node;
    }

    // --- Setup principal ---

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        float px = winSize.width - 92.0f;
        float py = winSize.height * 0.5f;

        // Fondo del panel
        auto bg = CCScale9Sprite::create("square02_small.png");
        bg->setContentSize({176.0f, 215.0f});
        bg->setPosition({px, py});
        bg->setOpacity(210);
        bg->setColor({8, 8, 18});
        bg->setZOrder(9);
        this->addChild(bg);

        // Titulo
        auto title = CCLabelBMFont::create("AutoPlay AI", "goldFont.fnt");
        title->setScale(0.46f);
        title->setPosition({px, py + 96.0f});
        this->addChild(title, 11);

        // Linea separadora
        auto line = CCLayerColor::create({80, 80, 120, 180}, 150.0f, 1.0f);
        line->setPosition({px - 75.0f, py + 84.0f});
        this->addChild(line, 11);

        // Menu principal
        auto menu = CCMenu::create();
        menu->setPosition({px, py});
        menu->setZOrder(10);
        menu->setTag(99);

        float y = 78.0f;

        // ── Activate AI ──
        {
            auto lbl = makeRowLabel("Activate AI");
            lbl->setPosition({-42.0f, y});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.aiEnabled,
                menu_selector(AIPauseLayer::onToggleAI));
            tog->setPosition({60.0f, y});
            menu->addChild(tog);
            y -= 34.0f;
        }

        // ── Safe Mode ──
        {
            auto lbl = makeRowLabel("Safe Mode");
            lbl->setPosition({-42.0f, y});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.safeMode,
                menu_selector(AIPauseLayer::onToggleSafeMode));
            tog->setPosition({60.0f, y});
            menu->addChild(tog);
            y -= 34.0f;
        }

        // ── Speedhack ──
        {
            auto lbl = makeRowLabel("Speedhack");
            lbl->setPosition({-42.0f, y});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.speedhack,
                menu_selector(AIPauseLayer::onToggleSpeedhack));
            tog->setPosition({60.0f, y});
            menu->addChild(tog);
            y -= 22.0f;

            // Input velocidad (porcentaje, ej: 150 = 1.5x)
            auto spInput = makeInput("Speed%", 55.0f, TAG_SPEED_INPUT);
            spInput->setPosition({-10.0f, y});
            menu->addChild(spInput);
            y -= 20.0f;

            // Sub-toggle: musica
            auto lblM = CCLabelBMFont::create("Music", "bigFont.fnt");
            lblM->setScale(0.28f);
            lblM->setColor({170, 200, 255});
            lblM->setPosition({-42.0f, y});
            menu->addChild(lblM);

            auto togM = makeToggle("Y", "N", g_cfg.speedMusic,
                menu_selector(AIPauseLayer::onToggleSpeedMusic));
            togM->setScale(0.75f);
            togM->setPosition({-5.0f, y});
            menu->addChild(togM);

            // Sub-toggle: progreso
            auto lblP = CCLabelBMFont::create("Progress", "bigFont.fnt");
            lblP->setScale(0.28f);
            lblP->setColor({170, 200, 255});
            lblP->setPosition({30.0f, y});
            menu->addChild(lblP);

            auto togP = makeToggle("Y", "N", g_cfg.speedProgress,
                menu_selector(AIPauseLayer::onToggleSpeedProgress));
            togP->setScale(0.75f);
            togP->setPosition({75.0f, y});
            menu->addChild(togP);
            y -= 30.0f;
        }

        // ── TPS Bypass ──
        {
            auto lbl = makeRowLabel("TPS Bypass");
            lbl->setPosition({-42.0f, y});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.tpsBypass,
                menu_selector(AIPauseLayer::onToggleTPS));
            tog->setPosition({60.0f, y});
            menu->addChild(tog);
            y -= 22.0f;

            // Input TPS (ej: 240)
            auto tpsInput = makeInput("TPS (fps)", 65.0f, TAG_TPS_INPUT);
            tpsInput->setPosition({0.0f, y});
            menu->addChild(tpsInput);
        }

        this->addChild(menu);
        this->schedule(schedule_selector(AIPauseLayer::syncInputs), 0.3f);
    }

    // Leer inputs periodicamente
    void syncInputs(float) {
        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByTag(99));
        if (!menu) return;

        auto* sNode = typeinfo_cast<CCTextInputNode*>(menu->getChildByTag(TAG_SPEED_INPUT));
        if (sNode) {
            float v = parseFloat(sNode, 100.0f);
            if (v >= 10.0f && v <= 2000.0f) g_cfg.speedMult = v / 100.0f;
        }

        auto* tNode = typeinfo_cast<CCTextInputNode*>(menu->getChildByTag(TAG_TPS_INPUT));
        if (tNode) {
            float v = parseFloat(tNode, 240.0f);
            if (v >= 30.0f && v <= 1000.0f) g_cfg.tpsValue = v;
        }
    }

    // --- Callbacks de toggles ---

    void onToggleAI(CCObject*) {
        g_cfg.aiEnabled = !g_cfg.aiEnabled;
        Notification::create(g_cfg.aiEnabled ? "AI: ON" : "AI: OFF",
            NotificationIcon::Success)->show();
        if (!g_cfg.aiEnabled && g_buttonHeld) {
            auto* pl = PlayLayer::get();
            if (pl && pl->m_player1) pl->m_player1->releaseButton(PlayerButton::Jump);
            g_buttonHeld = false;
        }
    }

    void onToggleSafeMode(CCObject*) {
        g_cfg.safeMode = !g_cfg.safeMode;
        Notification::create(g_cfg.safeMode ? "Safe Mode: ON" : "Safe Mode: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleSpeedhack(CCObject*) {
        g_cfg.speedhack = !g_cfg.speedhack;
        Notification::create(g_cfg.speedhack ? "Speedhack: ON" : "Speedhack: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleSpeedMusic(CCObject*) {
        g_cfg.speedMusic = !g_cfg.speedMusic;
        Notification::create(g_cfg.speedMusic ? "Speed Music: ON" : "Speed Music: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleSpeedProgress(CCObject*) {
        g_cfg.speedProgress = !g_cfg.speedProgress;
        Notification::create(g_cfg.speedProgress ? "Speed Progress: ON" : "Speed Progress: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleTPS(CCObject*) {
        g_cfg.tpsBypass = !g_cfg.tpsBypass;
        Notification::create(g_cfg.tpsBypass ? "TPS Bypass: ON" : "TPS Bypass: OFF",
            NotificationIcon::Success)->show();
    }
};

// ============================================================
//  ENTRADA DEL MOD
// ============================================================

$on_mod(Loaded) {
    log::info("AutoPlay AI v1.2.0 loaded.");
}
