#include <stdafx.h>
#include "touch_controls.h"
#include "imgui_utils.h"
#include "button_guide.h"
#include "game_window.h"
#include "options_menu.h"
#include "decompressor.h"
#include <api/SWA.h>
#include <gpu/video.h>
#include <app.h>
#include <hid/hid.h>
#include <os/logger.h>
#include <sdl_listener.h>
#include <user/config.h>
#include <res/images/touch_controls/SonicBoostXbox.png.h>
#include <res/images/touch_controls/SonicBoostPlaystation.png.h>
#include <res/images/touch_controls/SonicDashXbox.png.h>
#include <res/images/touch_controls/SonicDashPlaystation.png.h>
#include <res/images/touch_controls/SonicDriftXbox.png.h>
#include <res/images/touch_controls/SonicDriftPlaystation.png.h>
#include <res/images/touch_controls/SonicJumpXbox.png.h>
#include <res/images/touch_controls/SonicJumpPlaystation.png.h>
#include <res/images/touch_controls/SonicSSXbox.png.h>
#include <res/images/touch_controls/SonicSSPlaystation.png.h>
#include <res/images/touch_controls/SonicConnect.png.h>
#include <res/images/touch_controls/ChipAtkLeftXbox.png.h>
#include <res/images/touch_controls/ChipAtkLeftPlaystation.png.h>
#include <res/images/touch_controls/ChipAtkRightXbox.png.h>
#include <res/images/touch_controls/ChipAtkRightPlaystation.png.h>
#include <res/images/touch_controls/ChipBoostXbox.png.h>
#include <res/images/touch_controls/ChipBoostPlaystation.png.h>
#include <res/images/touch_controls/ChipGuardXbox.png.h>
#include <res/images/touch_controls/ChipGuardPlaystation.png.h>
#include <res/images/touch_controls/SuperSonicBosstXbox.png.h>
#include <res/images/touch_controls/SuperSonicBosstPlaystation.png.h>
#include <res/images/touch_controls/WerehogAtk1Xbox.png.h>
#include <res/images/touch_controls/WerehogAtk1Playstation.png.h>
#include <res/images/touch_controls/WerehogAtk2Xbox.png.h>
#include <res/images/touch_controls/WerehogAtk2Playstation.png.h>
#include <res/images/touch_controls/WerehogDashXbox.png.h>
#include <res/images/touch_controls/WerehogDashPlaystation.png.h>
#include <res/images/touch_controls/WerehogGrabXbox.png.h>
#include <res/images/touch_controls/WerehogGrabPlaystation.png.h>
#include <res/images/touch_controls/WerehogGuardXbox.png.h>
#include <res/images/touch_controls/WerehogGuardPlaystation.png.h>
#include <res/images/touch_controls/WerehogJumpXbox.png.h>
#include <res/images/touch_controls/WerehogJumpPlaystation.png.h>
#include <res/images/touch_controls/WerehogUnleashXbox.png.h>
#include <res/images/touch_controls/WerehogUnleashPlaystation.png.h>
#include <res/images/touch_controls/WerehogConnect.png.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_set>

#ifdef __ANDROID__
#include "os/android/storage_android.h"
#endif

// ---------------------------------------------------------------------------
// On-screen touch controls with a drag-to-arrange layout editor.
//
// Every control (stick, A/B/X/Y, LB/RB, LT/RT, Start/Back) has its own editable
// position. The Android launcher requests the editor for one launch; each control
// can be dragged, the whole set resized, or reset to defaults. The layout is saved
// to <data>/touch_layout.ini and reloaded on the next launch.
//
// Positions are fractions of the viewport; X of the viewport width, Y of the
// viewport height. Sizes are fractions of the viewport height so the layout keeps
// its proportions across resolutions/aspect ratios.
// ---------------------------------------------------------------------------

namespace
{
    // Logical controls. Order is load-bearing: it must match kDefault, kIcon and
    // the save-file keys below.
    enum
    {
        TC_STICK = 0,
        TC_A, TC_B, TC_X, TC_Y,
        TC_LB, TC_RB,
        TC_LT, TC_RT,
        TC_START, TC_BACK,
        TC_RSTICK, // appended last so saved layouts from older builds stay valid
        TC_COUNT
    };

    // Base sizes (fractions of viewport height), multiplied by the global scale.
    constexpr float STICK_BASE_R  = 0.150f;
    constexpr float STICK_THUMB_R = 0.070f;
    constexpr float STICK_ZONE_R  = 0.210f;
    // The camera stick is smaller: it shares the right side with the face buttons.
    constexpr float RSTICK_BASE_R  = 0.105f;
    constexpr float RSTICK_THUMB_R = 0.050f;
    constexpr float RSTICK_ZONE_R  = 0.150f;
    // Full camera-stick deflection in touch-area mode at this finger speed
    // (fraction of viewport height per frame).
    constexpr float CAM_DRAG_SENS = 0.0075f;
    constexpr float FACE_BTN_R    = 0.058f;
    constexpr float SHOULDER_HW   = 0.075f;
    constexpr float SHOULDER_HH   = 0.036f;
    constexpr float MENU_HW       = 0.032f;
    constexpr float MENU_HH       = 0.032f;

    constexpr float SCALE_MIN = 0.60f;
    constexpr float SCALE_MAX = 1.60f;

    struct Layout
    {
        float x[TC_COUNT];
        float y[TC_COUNT];
        float scale;
    };

    // Default layout. X-offsets that were expressed in height units in the old
    // fixed layout are baked here at the reference 2400x1080 aspect.
    //
    // The A/B/X/Y diamond is centred on (0.865, 0.760); its half-extents are
    // widened from the original (0.049 x, 0.108 y) so the four face buttons sit
    // further apart and are harder to mishit - especially the horizontal pair,
    // which the compressed default packed almost edge to edge.
    const Layout kDefault =
    {
        //  stick    A       B       X       Y      LB      RB      LT      RT     Start   Back   rstick
        {  0.135f, 0.865f, 0.927f, 0.865f, 0.865f, 0.075f, 0.925f, 0.075f, 0.803f, 0.555f, 0.445f, 0.680f },
        {  0.760f, 0.885f, 0.760f, 0.650f, 0.570f, 0.090f, 0.090f, 0.185f, 0.570f, 0.070f, 0.070f, 0.820f },
        1.0f
    };

    const Layout kLegacy =
    {
        // stick    A       B       X       Y      LB      RB      LT      RT     Start   Back   rstick
        {  0.135f, 0.865f, 0.927f, 0.803f, 0.865f, 0.075f, 0.925f, 0.075f, 0.925f, 0.555f, 0.445f, 0.680f },
        {  0.760f, 0.885f, 0.760f, 0.760f, 0.650f, 0.185f, 0.185f, 0.090f, 0.090f, 0.070f, 0.070f, 0.820f },
        1.0f
    };

    Layout g_layout = kDefault;
    Layout g_qteLayout = kDefault;

    const EButtonIcon kIcon[TC_COUNT] =
    {
        EButtonIcon::A, // stick (unused)
        EButtonIcon::A, EButtonIcon::B, EButtonIcon::X, EButtonIcon::Y,
        EButtonIcon::LB, EButtonIcon::RB,
        EButtonIcon::LT, EButtonIcon::RT,
        EButtonIcon::Start, EButtonIcon::Back,
        EButtonIcon::A // rstick (unused)
    };

    const char* const kKey[TC_COUNT] =
    {
        "stick", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back", "rstick"
    };

    // ---- Finger tracking (SDL touch thread) --------------------------------

    struct Finger
    {
        SDL_FingerID id;
        float nx; // normalised [0,1] over the window
        float ny;
        float startNx;
        float startNy;
    };

    std::mutex g_mutex;
    std::vector<Finger> g_fingers;

    std::atomic<bool> g_autoVisible{ true };
    XAMINPUT_GAMEPAD g_state{};

    // ---- Adaptive context (menu / cutscene) --------------------------------
    // Menus stamp a timestamp every frame they are visible (guest update thread);
    // the render thread treats the flag as active while the stamp is fresh, so no
    // destructor hooks are needed. The cutscene flag is edge-triggered from the
    // Inspire scene ctor/dtor hooks.
    std::atomic<uint64_t> g_menuSeenAtMs{ 0 };
    std::atomic<uint64_t> g_titleMenuSeenAtMs{ 0 };
    std::atomic<uint64_t> g_pauseMenuSeenAtMs{ 0 };
    std::atomic<uint64_t> g_bossGaugeSeenAtMs{ 0 };
    std::atomic<uint64_t> g_suSonicGaugeSeenAtMs{ 0 };
    std::atomic<uint64_t> g_gaiaGaugeSeenAtMs{ 0 };
    std::atomic<uint64_t> g_finalHudFooterSeenAtMs{ 0 };
    std::atomic<uint64_t> g_gameplayHudSeenAtMs{ 0 };
    std::mutex g_bossGaugeMutex;
    std::string g_bossGaugePath;
    std::atomic<bool> g_inspireSceneActive{ false };
    constexpr uint64_t MENU_STAMP_FRESH_MS = 1000;

    // WMV playback stamp (movie renderer, guest render path). Slightly longer
    // freshness than the menu stamp: movies decode at ~30 FPS and may hiccup.
    std::atomic<uint64_t> g_movieSeenAtMs{ 0 };
    constexpr uint64_t MOVIE_STAMP_FRESH_MS = 400;
    std::atomic<uint64_t> g_qteSeenAtMs{ 0 };
    std::atomic<uint64_t> g_qteGuideSeenAtMs{ 0 };
    std::atomic<uint64_t> g_tornadoDefenseSeenAtMs{ 0 };
    constexpr uint64_t QTE_STAMP_FRESH_MS = 1000;
    constexpr uint64_t QTE_GUIDE_SUPPRESSION_MS = 500;

    enum class ETouchContext { Normal, Menu, Title, Pause, Settings, Cutscene };

    bool IsFinalDarkGaiaStage()
    {
        auto* gameDocument = SWA::CGameDocument::GetInstance();
        if (!gameDocument || !gameDocument->m_pMember)
            return false;

        const char* stageName = gameDocument->m_pMember->m_StageName.c_str();
        return stageName && strcmp(stageName, "BossFinalDarkGaia") == 0;
    }

    bool IsStageLoaded()
    {
        auto* gameDocument = SWA::CGameDocument::GetInstance();
        if (!gameDocument || !gameDocument->m_pMember)
            return false;

        const char* stageName = gameDocument->m_pMember->m_StageName.c_str();
        return stageName && stageName[0] != '\0';
    }

    void LogFinalBossStageState(bool active, bool bossGaugeVisible, const std::string& bossGaugePath,
        bool gaiaGaugeVisible, bool suSonicGaugeVisible, bool footerVisible, const char* profile)
    {
        static bool previousActive = false;
        static bool previousWerehog = false;
        static bool previousBossGauge = false;
        static std::string previousBossGaugePath;
        static bool previousGaiaGauge = false;
        static bool previousSuSonicGauge = false;
        static bool previousFooter = false;
        static std::string previousProfile;
        const bool werehog = App::s_isWerehog;
        if (active == previousActive && (!active || werehog == previousWerehog) &&
            bossGaugeVisible == previousBossGauge && bossGaugePath == previousBossGaugePath &&
            gaiaGaugeVisible == previousGaiaGauge && suSonicGaugeVisible == previousSuSonicGauge &&
            footerVisible == previousFooter && previousProfile == profile)
            return;

        previousActive = active;
        previousWerehog = werehog;
        previousBossGauge = bossGaugeVisible;
        previousBossGaugePath = bossGaugePath;
        previousGaiaGauge = gaiaGaugeVisible;
        previousSuSonicGauge = suSonicGaugeVisible;
        previousFooter = footerVisible;
        previousProfile = profile;
        const std::string message = std::string("TOUCH_FINAL_BOSS_STATE stage=") +
            (active ? "BossFinalDarkGaia" : "hud_only") + " character=" +
            (werehog ? "werehog" : "sonic_or_super_sonic") + " controls=" +
            (active && werehog ? "default" : "custom") + " hud=" +
            (bossGaugeVisible ? "visible" : "hidden") + " hud_path=" +
            (bossGaugePath.empty() ? "none" : bossGaugePath) + " final_hud=" +
            "gaia:" + (gaiaGaugeVisible ? "1" : "0") +
            ",su:" + (suSonicGaugeVisible ? "1" : "0") +
            ",footer:" + (footerVisible ? "1" : "0") +
            " profile=" + profile;
        LOGN(message.c_str());
    }

    // Finger driving the analog stick (-1 = none). Render-thread only.
    SDL_FingerID g_stickFingerId = (SDL_FingerID)-1;

    // Camera control fingers (render thread only): the virtual right stick, or the
    // free-area camera drag with its last position for per-frame deltas.
    SDL_FingerID g_rstickFingerId = (SDL_FingerID)-1;
    SDL_FingerID g_camFingerId = (SDL_FingerID)-1;
    ImVec2 g_camLastPos{};
    std::unordered_set<SDL_FingerID> g_swipeTriggered;
    std::unordered_set<SDL_FingerID> g_boostFingerIds;
    std::unordered_set<SDL_FingerID> g_werehogDefenseFingerIds;
    bool g_werehogRunActive = false;
    bool g_werehogJumpWasDown = false;
    bool g_werehogAttackWasDown = false;
    bool g_werehogStickWasActive = false;
    uint64_t g_werehogRunJumpUntilMs = 0;
    uint64_t g_werehogRunAttackUntilMs = 0;
    constexpr uint64_t WEREHOG_RUN_JUMP_GRACE_MS = 180;
    constexpr uint64_t WEREHOG_RUN_ATTACK_CANCEL_MS = 5;
    std::unique_ptr<GuestTexture> g_sonicBoostXbox, g_sonicBoostPlaystation;
    std::unique_ptr<GuestTexture> g_sonicDashXbox, g_sonicDashPlaystation;
    std::unique_ptr<GuestTexture> g_sonicDriftXbox, g_sonicDriftPlaystation;
    std::unique_ptr<GuestTexture> g_sonicJumpXbox, g_sonicJumpPlaystation;
    std::unique_ptr<GuestTexture> g_sonicSSXbox, g_sonicSSPlaystation;
    std::unique_ptr<GuestTexture> g_sonicConnect;
    std::unique_ptr<GuestTexture> g_chipAtkLeftXbox, g_chipAtkLeftPlaystation;
    std::unique_ptr<GuestTexture> g_chipAtkRightXbox, g_chipAtkRightPlaystation;
    std::unique_ptr<GuestTexture> g_chipBoostXbox, g_chipBoostPlaystation;
    std::unique_ptr<GuestTexture> g_chipGuardXbox, g_chipGuardPlaystation;
    std::unique_ptr<GuestTexture> g_superSonicBoostXbox, g_superSonicBoostPlaystation;
    std::unique_ptr<GuestTexture> g_werehogAtk1Xbox, g_werehogAtk1Playstation;
    std::unique_ptr<GuestTexture> g_werehogAtk2Xbox, g_werehogAtk2Playstation;
    std::unique_ptr<GuestTexture> g_werehogDashXbox, g_werehogDashPlaystation;
    std::unique_ptr<GuestTexture> g_werehogGrabXbox, g_werehogGrabPlaystation;
    std::unique_ptr<GuestTexture> g_werehogGuardXbox, g_werehogGuardPlaystation;
    std::unique_ptr<GuestTexture> g_werehogJumpXbox, g_werehogJumpPlaystation;
    std::unique_ptr<GuestTexture> g_werehogUnleashXbox, g_werehogUnleashPlaystation;
    std::unique_ptr<GuestTexture> g_werehogConnect;

    // ---- Editor state (render thread only) ---------------------------------

    bool g_edit = false;
    int  g_dragElem = -1;                       // control being dragged (-1 = none)
    SDL_FingerID g_dragFinger = (SDL_FingerID)-1;
    float g_grabX = 0.0f, g_grabY = 0.0f;       // element-centre minus finger, in fractions
    std::vector<SDL_FingerID> g_prevIds;        // finger ids present last frame (for fresh-down detection)
    bool g_layoutLoaded = false;
    bool g_loadedWerehog = false;
    int g_loadedPreset = 1;
    bool g_legacyTouchControls = false;
    bool g_legacySonic = false;
    bool g_legacyWerehog = false;
    int g_sonicPreset = 1;
    int g_werehogPreset = 1;
    bool g_finalGaiaEncounterActive = false;
    bool g_finalQteTransitionPending = false;

    struct FingerPt
    {
        SDL_FingerID id;
        ImVec2 pos;
        float nx;
        float ny;
        float startNx;
        float startNy;
    };

    struct ElemRect { ImVec2 c; float hw; float hh; bool round; };

        ElemRect ElemRectOf(const Layout& layout, int i, float vw, float vh)
    {
            ImVec2 c(layout.x[i] * vw, layout.y[i] * vh);
        const float s = layout.scale;
        if (i == TC_STICK)                { const float r = STICK_BASE_R * vh * s; return { c, r, r, true }; }
        if (i == TC_RSTICK)               { const float r = RSTICK_BASE_R * vh * s; return { c, r, r, true }; }
        if (i >= TC_A && i <= TC_Y)       { const float r = FACE_BTN_R  * vh * s; return { c, r, r, true }; }
        if (i >= TC_LB && i <= TC_RT)     { return { c, SHOULDER_HW * vh * s, SHOULDER_HH * vh * s, false }; }
        return { c, MENU_HW * vh * s, MENU_HH * vh * s, false }; // Start / Back
    }

    ElemRect ElemRectOf(int i, float vw, float vh)
    {
        return ElemRectOf(g_layout, i, vw, vh);
    }

    ElemRect SonicElemRectOf(int i, float vw, float vh)
    {
        return ElemRectOf(g_layout, i, vw, vh);
    }

    ElemRect GaiaColossusElemRectOf(int i, float vw, float vh)
    {
        Layout layout = g_layout;
        switch (i)
        {
            case TC_X:  layout.x[i] = 0.850f; layout.y[i] = 0.850f; break;
            case TC_LB: layout.x[i] = 0.850f; layout.y[i] = 0.530f; break;
            case TC_LT: layout.x[i] = 0.790f; layout.y[i] = 0.670f; break;
            case TC_RT: layout.x[i] = 0.910f; layout.y[i] = 0.670f; break;
            case TC_START: layout.x[i] = 0.555f; layout.y[i] = 0.070f; break;
            default: break;
        }
        return ElemRectOf(layout, i, vw, vh);
    }

    ElemRect WerehogElemRectOf(int i, float vw, float vh)
    {
        return ElemRectOf(g_layout, i, vw, vh);
    }

    bool IsHiddenDaytimeControl(int i)
    {
        return i == TC_LB || i == TC_RB || i == TC_LT;
    }

    // ---- Persistence -------------------------------------------------------

    std::filesystem::path LayoutFilePath(bool werehog, int preset)
    { 
#ifdef __ANDROID__
        const std::filesystem::path& root = os::android::GetDataRoot();
        if (!root.empty())
            return root / (std::string("touch_layout_") + (werehog ? "werehog_" : "sonic_") +
                std::to_string(std::clamp(preset, 1, 3)) + ".ini");
#endif
        return {};
    }

    std::filesystem::path TouchPreferencesPath()
    {
#ifdef __ANDROID__
        const std::filesystem::path& root = os::android::GetDataRoot();
        if (!root.empty())
            return root / "touch_controls.ini";
#endif
        return {};
    }

    void LoadTouchPreferences()
    {
        const std::filesystem::path path = TouchPreferencesPath();
        if (path.empty())
            return;

        std::ifstream f(path, std::ios::binary);
        if (!f)
            return;

        std::string line;
        while (std::getline(f, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);
            if (key == "sonic_legacy") g_legacySonic = atoi(value.c_str()) != 0;
            else if (key == "werehog_legacy") g_legacyWerehog = atoi(value.c_str()) != 0;
            else if (key == "sonic_preset") g_sonicPreset = std::clamp(atoi(value.c_str()), 1, 3);
            else if (key == "werehog_preset") g_werehogPreset = std::clamp(atoi(value.c_str()), 1, 3);
        }
    }

    void SaveLayout()
    {
        const std::filesystem::path path = LayoutFilePath(App::s_isWerehog,
            App::s_isWerehog ? g_werehogPreset : g_sonicPreset);
        if (path.empty())
            return;

        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
            return;

        f << "version=1\n";
        f << "scale=" << g_layout.scale << "\n";
        for (int i = 0; i < TC_COUNT; ++i)
            f << kKey[i] << "=" << g_layout.x[i] << "," << g_layout.y[i] << "\n";
    }

    void LoadLayout()
    {
        g_layout = g_legacyTouchControls ? kLegacy : kDefault;
        if (g_legacyTouchControls)
        {
            g_layout.x[TC_LB] = 0.075f; g_layout.y[TC_LB] = 0.185f;
            g_layout.x[TC_RB] = 0.925f; g_layout.y[TC_RB] = 0.185f;
            g_layout.x[TC_LT] = 0.075f; g_layout.y[TC_LT] = 0.090f;
            g_layout.x[TC_RT] = 0.925f; g_layout.y[TC_RT] = 0.090f;
        }
        else if (App::s_isWerehog)
        {
            g_layout.x[TC_RB] = 0.92f; g_layout.y[TC_RB] = 0.29f;
            g_layout.x[TC_Y] = 0.90f; g_layout.y[TC_Y] = 0.49f;
            g_layout.x[TC_X] = 0.79f; g_layout.y[TC_X] = 0.66f;
            g_layout.x[TC_LB] = 0.89f; g_layout.y[TC_LB] = 0.68f;
            g_layout.x[TC_B] = 0.74f; g_layout.y[TC_B] = 0.84f;
            g_layout.x[TC_A] = 0.85f; g_layout.y[TC_A] = 0.86f;
            g_layout.x[TC_RT] = 0.70f; g_layout.y[TC_RT] = 0.68f;
        }
        else
        {
            g_layout.x[TC_A] = 0.850f; g_layout.y[TC_A] = 0.850f;
            g_layout.x[TC_X] = 0.780f; g_layout.y[TC_X] = 0.680f;
            g_layout.x[TC_Y] = 0.880f; g_layout.y[TC_Y] = 0.680f;
            g_layout.x[TC_RT] = 0.780f; g_layout.y[TC_RT] = 0.480f;
            g_layout.x[TC_B] = 0.760f; g_layout.y[TC_B] = 0.850f;
            g_layout.x[TC_START] = 0.555f; g_layout.y[TC_START] = 0.070f;
        }
        g_layoutLoaded = true;

        const std::filesystem::path path = LayoutFilePath(App::s_isWerehog,
            App::s_isWerehog ? g_werehogPreset : g_sonicPreset);
        if (path.empty())
            return;

        std::ifstream f(path, std::ios::binary);
        if (!f)
            return;

        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            const std::string key = line.substr(0, eq);
            const std::string val = line.substr(eq + 1);

            if (key == "scale")
            {
                g_layout.scale = std::clamp((float)atof(val.c_str()), SCALE_MIN, SCALE_MAX);
                continue;
            }

            for (int i = 0; i < TC_COUNT; ++i)
            {
                if (key == kKey[i])
                {
                    const size_t comma = val.find(',');
                    if (comma != std::string::npos)
                    {
                        const float x = (float)atof(val.substr(0, comma).c_str());
                        const float y = (float)atof(val.substr(comma + 1).c_str());
                        g_layout.x[i] = std::clamp(x, 0.0f, 1.0f);
                        g_layout.y[i] = std::clamp(y, 0.0f, 1.0f);
                    }
                    break;
                }
            }
        }
    }

    // ---- Drawing helpers ---------------------------------------------------

    bool AnyFingerInCircle(const std::vector<ImVec2>& pts, ImVec2 c, float r)
    {
        const float r2 = r * r;
        for (const auto& p : pts)
        {
            const float dx = p.x - c.x;
            const float dy = p.y - c.y;
            if (dx * dx + dy * dy <= r2)
                return true;
        }
        return false;
    }

    bool AnyFingerInRect(const std::vector<ImVec2>& pts, ImVec2 mn, ImVec2 mx)
    {
        for (const auto& p : pts)
        {
            if (p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y)
                return true;
        }
        return false;
    }

    void DrawGlyph(ImDrawList* dl, ImVec2 c, float halfW, float halfH, EButtonIcon icon, int alpha,
        GuestTexture* customTexture = nullptr, ImVec4 customUv = { 0.0f, 0.0f, 1.0f, 1.0f })
    {
        auto ic = GetButtonIcon(icon);
        auto* tex = customTexture ? customTexture : std::get<1>(ic);
        if (!tex)
            return;

        ImVec2 uv0;
        ImVec2 uv1;
        if (customTexture)
        {
            uv0 = { customUv.x, customUv.y };
            uv1 = { customUv.z, customUv.w };
        }
        else
        {
            const auto& iconUv = std::get<0>(ic);
            uv0 = std::get<0>(iconUv);
            uv1 = std::get<1>(iconUv);
        }
        dl->AddImage(tex, { c.x - halfW, c.y - halfH }, { c.x + halfW, c.y + halfH },
            uv0, uv1, IM_COL32(255, 255, 255, alpha));
    }

    bool UsePlayStationTouchAssets()
    {
        if (Config::ControllerIcons == EControllerIcons::PlayStation)
            return true;
        if (Config::ControllerIcons == EControllerIcons::Xbox)
            return false;
        return hid::g_inputDeviceController == hid::EInputDevice::PlayStation;
    }

    GuestTexture* SelectTouchAsset(GuestTexture* xbox, GuestTexture* playStation)
    {
        if (g_legacyTouchControls)
            return nullptr;
        return UsePlayStationTouchAssets() ? playStation : xbox;
    }

    // Connection textures are vertical. The quad keeps their width fixed and
    // stretches only the local vertical axis between the two controls.
    void DrawTouchConnection(ImDrawList* dl, GuestTexture* texture, ImVec2 from, ImVec2 to,
        float halfWidth, int alpha)
    {
        if (!texture)
            return;

        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1.0f)
            return;

        const float px = -dy / length * halfWidth;
        const float py = dx / length * halfWidth;
        const ImVec2 topLeft { from.x - px, from.y - py };
        const ImVec2 topRight { from.x + px, from.y + py };
        const ImVec2 bottomRight { to.x + px, to.y + py };
        const ImVec2 bottomLeft { to.x - px, to.y - py };
        dl->AddImageQuad(texture, topLeft, topRight, bottomRight, bottomLeft,
            { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f },
            IM_COL32(255, 255, 255, alpha));
    }

    // Custom touch assets already contain their own button artwork. Draw them
    // directly and preserve their native aspect ratio.
    void DrawFaceButton(ImDrawList* dl, const std::vector<ImVec2>& pts, ImVec2 c, float r,
        EButtonIcon icon, uint16_t bit, XAMINPUT_GAMEPAD& st, GuestTexture* customTexture = nullptr,
        ImVec4 customUv = { 0.0f, 0.0f, 1.0f, 1.0f })
    {
        const bool pressed = AnyFingerInCircle(pts, c, r * 1.3f);
        if (pressed)
            st.wButtons |= bit;

        if (customTexture)
        {
            const float halfH = r * 1.08f;
            const float aspect = customTexture->height > 0
                ? float(customTexture->width) / float(customTexture->height) : 1.0f;
            DrawGlyph(dl, c, halfH * aspect, halfH, icon, pressed ? 255 : 210,
                customTexture, customUv);
        }
        else
        {
            dl->AddCircleFilled(c, r * 1.25f, IM_COL32(0, 0, 0, pressed ? 120 : 70), 32);
            DrawGlyph(dl, c, r, r, icon, pressed ? 255 : 210);
        }
    }

    // Wide button (shoulders/triggers/start/back): rounded backing + glyph.
    bool DrawWideButton(ImDrawList* dl, const std::vector<ImVec2>& pts, ImVec2 c,
        float halfW, float halfH, float glyphHalfW, float glyphHalfH, EButtonIcon icon,
        GuestTexture* customTexture = nullptr, ImVec4 customUv = { 0.0f, 0.0f, 1.0f, 1.0f })
    {
        const bool pressed = AnyFingerInRect(pts, { c.x - halfW, c.y - halfH }, { c.x + halfW, c.y + halfH });

        if (customTexture)
        {
            const float halfH = glyphHalfH * 1.08f;
            const float aspect = customTexture->height > 0
                ? float(customTexture->width) / float(customTexture->height) : 1.0f;
            DrawGlyph(dl, c, halfH * aspect, halfH, icon, pressed ? 255 : 210,
                customTexture, customUv);
        }
        else
        {
            dl->AddRectFilled({ c.x - halfW, c.y - halfH }, { c.x + halfW, c.y + halfH },
                IM_COL32(0, 0, 0, pressed ? 120 : 70), halfH * 0.5f);
            DrawGlyph(dl, c, glyphHalfW, glyphHalfH, icon, pressed ? 255 : 210);
        }

        return pressed;
    }

    bool DrawCircularButton(ImDrawList* dl, const std::vector<ImVec2>& pts, ImVec2 c,
        float radius, EButtonIcon icon, GuestTexture* customTexture = nullptr)
    {
        const bool pressed = AnyFingerInCircle(pts, c, radius * 1.3f);
        if (customTexture)
        {
            const float halfH = radius * 0.93f;
            const float aspect = customTexture->height > 0
                ? float(customTexture->width) / float(customTexture->height) : 1.0f;
            DrawGlyph(dl, c, halfH * aspect, halfH, icon, pressed ? 255 : 210, customTexture);
        }
        else
        {
            dl->AddCircleFilled(c, radius, IM_COL32(0, 0, 0, pressed ? 120 : 70), 40);
            DrawGlyph(dl, c, radius * 0.72f, radius * 0.72f, icon, pressed ? 255 : 210);
        }
        return pressed;
    }

    // D-pad drawn in place of the left stick while a menu is open. The whole
    // stick zone is the hit area; direction comes from the finger's angle with
    // an 8-way split (diagonals press two directions), matching how the game's
    // menus read the physical D-pad.
    void DrawDpad(ImDrawList* dl, const std::vector<FingerPt>& fps, ImVec2 c,
        float baseR, float zoneR, XAMINPUT_GAMEPAD& st)
    {
        uint16_t bits = 0;
        for (const auto& fp : fps)
        {
            const float dx = fp.pos.x - c.x;
            const float dy = fp.pos.y - c.y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 > zoneR * zoneR || dist2 < baseR * baseR * 0.04f)
                continue;

            // 8-way: a component counts when it carries at least half the other.
            if (std::fabs(dx) >= std::fabs(dy) * 0.5f)
                bits |= dx > 0.0f ? XAMINPUT_GAMEPAD_DPAD_RIGHT : XAMINPUT_GAMEPAD_DPAD_LEFT;
            if (std::fabs(dy) >= std::fabs(dx) * 0.5f)
                bits |= dy > 0.0f ? XAMINPUT_GAMEPAD_DPAD_DOWN : XAMINPUT_GAMEPAD_DPAD_UP;
        }
        st.wButtons |= bits;

        dl->AddCircleFilled(c, baseR, IM_COL32(0, 0, 0, bits ? 90 : 55), 48);
        dl->AddCircle(c, baseR, IM_COL32(255, 255, 255, 130), 48, 3.0f);

        const float armHalf = baseR * 0.24f;
        const float armLength = baseR * 0.76f;
        struct { float ox, oy; uint16_t bit; } dirs[4] =
        {
            {  0.0f, -1.0f, XAMINPUT_GAMEPAD_DPAD_UP },
            {  1.0f,  0.0f, XAMINPUT_GAMEPAD_DPAD_RIGHT },
            {  0.0f,  1.0f, XAMINPUT_GAMEPAD_DPAD_DOWN },
            { -1.0f,  0.0f, XAMINPUT_GAMEPAD_DPAD_LEFT },
        };
        for (const auto& d : dirs)
        {
            const bool on = (st.wButtons & d.bit) != 0;
            const ImVec2 min = d.ox != 0.0f
                ? ImVec2(c.x + (d.ox < 0.0f ? -armLength : -armHalf), c.y - armHalf)
                : ImVec2(c.x - armHalf, c.y + (d.oy < 0.0f ? -armLength : -armHalf));
            const ImVec2 max = d.ox != 0.0f
                ? ImVec2(c.x + (d.ox < 0.0f ? armHalf : armLength), c.y + armHalf)
                : ImVec2(c.x + armHalf, c.y + (d.oy < 0.0f ? armHalf : armLength));
            dl->AddRectFilled(min, max, IM_COL32(255, 255, 255, on ? 230 : 120));
        }
    }

    // Draw a control's static visual (no press detection) - used by the editor.
    void DrawElemVisual(ImDrawList* dl, int i, const ElemRect& r)
    {
        if (i == TC_STICK || i == TC_RSTICK)
        {
            dl->AddCircleFilled(r.c, r.hw, IM_COL32(0, 0, 0, 55), 48);
            dl->AddCircle(r.c, r.hw, IM_COL32(255, 255, 255, 130), 48, 3.0f);
            const float thumb = r.hw * (i == TC_RSTICK ? RSTICK_THUMB_R / RSTICK_BASE_R
                                                       : STICK_THUMB_R / STICK_BASE_R);
            dl->AddCircleFilled(r.c, thumb, IM_COL32(255, 255, 255, 110), 32);
            return;
        }

        if (i >= TC_A && i <= TC_Y)
        {
            dl->AddCircleFilled(r.c, r.hw * 1.25f, IM_COL32(0, 0, 0, 70), 32);
            DrawGlyph(dl, r.c, r.hw, r.hw, kIcon[i], 210);
            return;
        }

        dl->AddRectFilled({ r.c.x - r.hw, r.c.y - r.hh }, { r.c.x + r.hw, r.c.y + r.hh },
            IM_COL32(0, 0, 0, 70), r.hh * 0.5f);

        float gw, gh;
        if (i == TC_LB || i == TC_RB)      { gw = r.hw * 0.75f; gh = r.hh * 0.85f; }
        else if (i == TC_LT || i == TC_RT) { gw = r.hh * 0.95f; gh = r.hh * 0.95f; }
        else                               { gw = r.hw;         gh = r.hh; } // Start / Back

        DrawGlyph(dl, r.c, gw, gh, kIcon[i], 210);
    }
}

// ---------------------------------------------------------------------------
// SDL touch event handling.
// ---------------------------------------------------------------------------

class SDLEventListenerForTouchControls : public SDLEventListener
{
public:
    bool OnSDLEvent(SDL_Event* event) override
    {
        switch (event->type)
        {
            case SDL_FINGERDOWN:
            {
                {
                    std::lock_guard lock(g_mutex);
                    g_fingers.push_back({ event->tfinger.fingerId, event->tfinger.x, event->tfinger.y,
                        event->tfinger.x, event->tfinger.y });
                }

                // Any touch brings the overlay back.
                TouchControls::SetVisible(true);
                break;
            }

            case SDL_FINGERMOTION:
            {
                std::lock_guard lock(g_mutex);
                for (auto& f : g_fingers)
                {
                    if (f.id == event->tfinger.fingerId)
                    {
                        f.nx = event->tfinger.x;
                        f.ny = event->tfinger.y;
                        break;
                    }
                }
                break;
            }

            case SDL_FINGERUP:
            {
                std::lock_guard lock(g_mutex);
                std::erase_if(g_fingers, [&](const Finger& f) { return f.id == event->tfinger.fingerId; });
                break;
            }
        }

        return false;
    }
}
g_sdlEventListenerForTouchControls;

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

bool TouchControls::IsVisible()
{
    // The launcher-requested layout editor must remain reachable even when the
    // user's normal touch-control policy is Off.
    if (g_edit)
        return true;

    // Loading screens own the whole viewport and do not accept gameplay input.
    // This takes precedence over Always On so no gameplay controls leak into
    // transitions.
    if (App::s_isLoading)
    {
        g_finalGaiaEncounterActive = false;
        g_finalQteTransitionPending = false;
        return false;
    }
#ifdef __ANDROID__
    switch (Config::TouchControls.Value)
    {
        case EAndroidTouchControlsPolicy::AlwaysOn:
            return true;
        case EAndroidTouchControlsPolicy::Off:
            return false;
        case EAndroidTouchControlsPolicy::Auto:
        default:
            break;
    }
#endif

    return g_autoVisible.load(std::memory_order_relaxed);
}

void TouchControls::SetVisible(bool visible)
{
    g_autoVisible.store(visible, std::memory_order_relaxed);
}

const XAMINPUT_GAMEPAD& TouchControls::GetGamepadState()
{
    return g_state;
}

void TouchControls::NotifyMenuVisible()
{
    g_menuSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
}

void TouchControls::NotifyTitleMenuVisible()
{
    g_titleMenuSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
}

void TouchControls::NotifyPauseMenuVisible()
{
    g_pauseMenuSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
}

void TouchControls::NotifyPauseMenuHidden()
{
    g_pauseMenuSeenAtMs.store(0, std::memory_order_relaxed);
}

void TouchControls::NotifyBossGaugeVisible(const char* path)
{
    g_bossGaugeSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
    const std::string currentPath = path ? path : "";
    std::lock_guard lock(g_bossGaugeMutex);
    g_bossGaugePath = currentPath;
}

void TouchControls::NotifyFinalHudGaugeVisible(const char* path)
{
    if (!path)
        return;

    const uint64_t now = SDL_GetTicks64();
    const std::string_view scenePath(path);
    const auto isPathOrChild = [&scenePath](std::string_view parent)
    {
        return scenePath == parent ||
            (scenePath.size() > parent.size() && scenePath.starts_with(parent) && scenePath[parent.size()] == '/');
    };

    if (isPathOrChild("ui_playscreen_su/su_sonic_gauge"))
        g_suSonicGaugeSeenAtMs.store(now, std::memory_order_relaxed);
    else if (isPathOrChild("ui_playscreen_su/gaia_gauge"))
        g_gaiaGaugeSeenAtMs.store(now, std::memory_order_relaxed);
    else if (isPathOrChild("ui_playscreen_su/footer"))
        g_finalHudFooterSeenAtMs.store(now, std::memory_order_relaxed);
}

void TouchControls::NotifyGameplayHudVisible()
{
    g_gameplayHudSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
}

void TouchControls::NotifyCutsceneActive(bool active)
{
    g_inspireSceneActive.store(active, std::memory_order_relaxed);
}

void TouchControls::NotifyMovieVisible()
{
    g_movieSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
}

void TouchControls::NotifyQteActive(bool active)
{
    if (!active)
    {
        g_qteSeenAtMs.store(0, std::memory_order_relaxed);
        return;
    }

    const uint64_t now = SDL_GetTicks64();
    if (now - g_qteGuideSeenAtMs.load(std::memory_order_relaxed) < QTE_GUIDE_SUPPRESSION_MS)
        return;

    g_qteSeenAtMs.store(now, std::memory_order_relaxed);
}

void TouchControls::NotifyQteGuideVisible()
{
    g_qteGuideSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
    g_qteSeenAtMs.store(0, std::memory_order_relaxed);
}

void TouchControls::NotifyQteCompleted()
{
    g_qteSeenAtMs.store(0, std::memory_order_relaxed);
    g_qteGuideSeenAtMs.store(0, std::memory_order_relaxed);
}

void TouchControls::NotifyTornadoDefenseActive(bool active)
{
        if (active)
            g_tornadoDefenseSeenAtMs.store(SDL_GetTicks64(), std::memory_order_relaxed);
}

void TouchControls::Init()
{
    LOGN("TOUCH_DIAG_READY");

    LoadTouchPreferences();
    g_layout = kDefault;
    g_layoutLoaded = false;
    g_sonicBoostXbox = LOAD_ZSTD_TEXTURE(g_sonic_boost_xbox);
    g_sonicBoostPlaystation = LOAD_ZSTD_TEXTURE(g_sonic_boost_playstation);
    g_sonicDashXbox = LOAD_ZSTD_TEXTURE(g_sonic_dash_xbox);
    g_sonicDashPlaystation = LOAD_ZSTD_TEXTURE(g_sonic_dash_playstation);
    g_sonicDriftXbox = LOAD_ZSTD_TEXTURE(g_sonic_drift_xbox);
    g_sonicDriftPlaystation = LOAD_ZSTD_TEXTURE(g_sonic_drift_playstation);
    g_sonicJumpXbox = LOAD_ZSTD_TEXTURE(g_sonic_jump_xbox);
    g_sonicJumpPlaystation = LOAD_ZSTD_TEXTURE(g_sonic_jump_playstation);
    g_sonicSSXbox = LOAD_ZSTD_TEXTURE(g_sonic_ss_xbox);
    g_sonicSSPlaystation = LOAD_ZSTD_TEXTURE(g_sonic_ss_playstation);
    g_sonicConnect = LOAD_ZSTD_TEXTURE(g_sonic_connect);
    g_chipAtkLeftXbox = LOAD_ZSTD_TEXTURE(g_chip_atk_left_xbox);
    g_chipAtkLeftPlaystation = LOAD_ZSTD_TEXTURE(g_chip_atk_left_playstation);
    g_chipAtkRightXbox = LOAD_ZSTD_TEXTURE(g_chip_atk_right_xbox);
    g_chipAtkRightPlaystation = LOAD_ZSTD_TEXTURE(g_chip_atk_right_playstation);
    g_chipBoostXbox = LOAD_ZSTD_TEXTURE(g_chip_boost_xbox);
    g_chipBoostPlaystation = LOAD_ZSTD_TEXTURE(g_chip_boost_playstation);
    g_chipGuardXbox = LOAD_ZSTD_TEXTURE(g_chip_guard_xbox);
    g_chipGuardPlaystation = LOAD_ZSTD_TEXTURE(g_chip_guard_playstation);
    g_superSonicBoostXbox = LOAD_ZSTD_TEXTURE(g_super_sonic_bosst_xbox);
    g_superSonicBoostPlaystation = LOAD_ZSTD_TEXTURE(g_super_sonic_bosst_playstation);
    g_werehogAtk1Xbox = LOAD_ZSTD_TEXTURE(g_werehog_atk1_xbox);
    g_werehogAtk1Playstation = LOAD_ZSTD_TEXTURE(g_werehog_atk1_playstation);
    g_werehogAtk2Xbox = LOAD_ZSTD_TEXTURE(g_werehog_atk2_xbox);
    g_werehogAtk2Playstation = LOAD_ZSTD_TEXTURE(g_werehog_atk2_playstation);
    g_werehogDashXbox = LOAD_ZSTD_TEXTURE(g_werehog_dash_xbox);
    g_werehogDashPlaystation = LOAD_ZSTD_TEXTURE(g_werehog_dash_playstation);
    g_werehogGrabXbox = LOAD_ZSTD_TEXTURE(g_werehog_grab_xbox);
    g_werehogGrabPlaystation = LOAD_ZSTD_TEXTURE(g_werehog_grab_playstation);
    g_werehogGuardXbox = LOAD_ZSTD_TEXTURE(g_werehog_guard_xbox);
    g_werehogGuardPlaystation = LOAD_ZSTD_TEXTURE(g_werehog_guard_playstation);
    g_werehogJumpXbox = LOAD_ZSTD_TEXTURE(g_werehog_jump_xbox);
    g_werehogJumpPlaystation = LOAD_ZSTD_TEXTURE(g_werehog_jump_playstation);
    g_werehogUnleashXbox = LOAD_ZSTD_TEXTURE(g_werehog_unleash_xbox);
    g_werehogUnleashPlaystation = LOAD_ZSTD_TEXTURE(g_werehog_unleash_playstation);
    g_werehogConnect = LOAD_ZSTD_TEXTURE(g_werehog_connect);
}

void TouchControls::Draw()
{
    if (!IsVisible())
    {
        g_state = {};
        g_stickFingerId = (SDL_FingerID)-1;
        g_rstickFingerId = (SDL_FingerID)-1;
        g_camFingerId = (SDL_FingerID)-1;
        g_swipeTriggered.clear();
        g_boostFingerIds.clear();
        g_werehogDefenseFingerIds.clear();
        g_werehogRunActive = false;
        g_werehogJumpWasDown = false;
        g_werehogAttackWasDown = false;
        g_werehogStickWasActive = false;
        g_werehogRunJumpUntilMs = 0;
        g_werehogRunAttackUntilMs = 0;
        g_dragElem = -1;
        g_prevIds.clear();
        return;
    }

    const bool werehogCharacter = App::s_isWerehog;
    const int activePreset = werehogCharacter ? g_werehogPreset : g_sonicPreset;
    g_legacyTouchControls = werehogCharacter ? g_legacyWerehog : g_legacySonic;
    if (!g_layoutLoaded || g_loadedWerehog != werehogCharacter || g_loadedPreset != activePreset)
    {
        LoadLayout();
        g_loadedWerehog = werehogCharacter;
        g_loadedPreset = activePreset;
    }

    const float vw = float(Video::s_viewportWidth);
    const float vh = float(Video::s_viewportHeight);
    if (vw <= 0.0f || vh <= 0.0f)
        return;

    // Both playable parts of the ending share this stage. Keep the stage
    // signal independent from the player-character signal: CEvilSonicContext
    // means Werehog here and does not identify Light Gaia or Super Sonic.
    const bool stageMatchesFinalBoss = IsFinalDarkGaiaStage();
    const bool stageLoaded = IsStageLoaded();
    const uint64_t now = SDL_GetTicks64();
    const bool bossGaugeVisible = now -
        g_bossGaugeSeenAtMs.load(std::memory_order_relaxed) < MENU_STAMP_FRESH_MS;
    const bool suSonicGaugeVisible = now -
        g_suSonicGaugeSeenAtMs.load(std::memory_order_relaxed) < QTE_STAMP_FRESH_MS;
    const bool gaiaGaugeVisible = now -
        g_gaiaGaugeSeenAtMs.load(std::memory_order_relaxed) < QTE_STAMP_FRESH_MS;
    const bool finalHudFooterVisible = now -
        g_finalHudFooterSeenAtMs.load(std::memory_order_relaxed) < QTE_STAMP_FRESH_MS;
    const bool gameplayHudVisible = now -
        g_gameplayHudSeenAtMs.load(std::memory_order_relaxed) < MENU_STAMP_FRESH_MS;
    const bool finalHudVisible = gaiaGaugeVisible || suSonicGaugeVisible || finalHudFooterVisible;
    const bool finalBossStage = stageMatchesFinalBoss || finalHudVisible;
    std::string bossGaugePath;
    {
        std::lock_guard lock(g_bossGaugeMutex);
        bossGaugePath = g_bossGaugePath;
    }
    // The two final encounters share ui_playscreen_su and both render
    // gaia_gauge. The footer is the observed discriminator: it is present for
    // Gaia Colossus and absent during Super Sonic.
    const bool gaiaColossusProfile = gaiaGaugeVisible && finalHudFooterVisible;
    const bool superSonicProfile = suSonicGaugeVisible && !finalHudFooterVisible;
        if (gaiaColossusProfile)
            g_finalGaiaEncounterActive = true;
    const char* finalProfile = gaiaColossusProfile ? "gaia" :
        superSonicProfile ? "super_sonic" : "other";
    LogFinalBossStageState(stageMatchesFinalBoss, bossGaugeVisible, bossGaugePath,
        gaiaGaugeVisible, suSonicGaugeVisible, finalHudFooterVisible, finalProfile);

    // Map normalised finger coordinates (over the window/swapchain) into ImGui
    // viewport space (the viewport is centred within the swapchain for some
    // aspect-ratio settings).
    int pw = 0, ph = 0;
    GameWindow::GetSizeInPixels(&pw, &ph);
    const float sw = pw > 0 ? float(pw) : vw;
    const float sh = ph > 0 ? float(ph) : vh;
    const float offX = (sw - vw) * 0.5f;
    const float offY = (sh - vh) * 0.5f;

    std::vector<FingerPt> fps;
    {
        std::lock_guard lock(g_mutex);
        fps.reserve(g_fingers.size());
        for (const auto& f : g_fingers)
            fps.push_back({ f.id, { f.nx * sw - offX, f.ny * sh - offY }, f.nx, f.ny, f.startNx, f.startNy });
    }

    // Fresh finger-downs = ids present now but not last frame (one-shot taps).
    std::unordered_set<SDL_FingerID> prevSet(g_prevIds.begin(), g_prevIds.end());
    std::vector<FingerPt> fresh;
    for (const auto& fp : fps)
        if (!prevSet.count(fp.id))
            fresh.push_back(fp);

    std::vector<SDL_FingerID> curIds;
    curIds.reserve(fps.size());
    for (const auto& fp : fps)
        curIds.push_back(fp.id);

    auto isCurrentFinger = [&](SDL_FingerID id)
    {
        return std::find(curIds.begin(), curIds.end(), id) != curIds.end();
    };
    std::erase_if(g_swipeTriggered, [&](SDL_FingerID id) { return !isCurrentFinger(id); });
    std::erase_if(g_boostFingerIds, [&](SDL_FingerID id) { return !isCurrentFinger(id); });
    std::erase_if(g_werehogDefenseFingerIds, [&](SDL_FingerID id) { return !isCurrentFinger(id); });

    auto* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontPx = vh * 0.030f;

    auto tapBox = [&](ImVec2 c, float hw, float hh, const char* label, bool accent)
    {
        const ImU32 bg = accent ? IM_COL32(70, 120, 200, 220) : IM_COL32(0, 0, 0, 180);
        dl->AddRectFilled({ c.x - hw, c.y - hh }, { c.x + hw, c.y + hh }, bg, 8.0f);
        dl->AddRect({ c.x - hw, c.y - hh }, { c.x + hw, c.y + hh }, IM_COL32(255, 255, 255, 190), 8.0f, 0, 2.0f);
        const ImVec2 ts = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, label);
        dl->AddText(font, fontPx, { c.x - ts.x * 0.5f, c.y - ts.y * 0.5f }, IM_COL32(255, 255, 255, 255), label);
    };

    // -----------------------------------------------------------------------
    // Gameplay mode.
    // -----------------------------------------------------------------------
    if (!g_edit)
    {
        XAMINPUT_GAMEPAD st{};

        // Adaptive context: cutscenes collapse everything into one SKIP button,
        // menus swap the analog stick for a D-pad and release the camera finger.
        // Cutscenes are Inspire scenes (edge-triggered ctor/dtor hooks) plus live
        // WMV playback such as the attract movie (per-frame stamp from the movie
        // renderer - the movie manager singleton outlives playback, so its mere
        // presence is not usable as a signal: it locked the whole title screen
        // into SKIP mode when tried).
        ETouchContext context = ETouchContext::Normal;
        if (OptionsMenu::s_isVisible)
        {
            context = ETouchContext::Settings;
        }
        else if (SDL_GetTicks64() - g_pauseMenuSeenAtMs.load(std::memory_order_relaxed) < MENU_STAMP_FRESH_MS)
        {
            context = ETouchContext::Pause;
        }
        else if (SDL_GetTicks64() - g_titleMenuSeenAtMs.load(std::memory_order_relaxed) < MENU_STAMP_FRESH_MS)
        {
            context = ETouchContext::Title;
        }
        else if (SDL_GetTicks64() - g_menuSeenAtMs.load(std::memory_order_relaxed) < MENU_STAMP_FRESH_MS)
        {
            context = ETouchContext::Menu;
        }
        else if (g_inspireSceneActive.load(std::memory_order_relaxed) ||
            SDL_GetTicks64() - g_movieSeenAtMs.load(std::memory_order_relaxed) < MOVIE_STAMP_FRESH_MS)
        {
            context = ETouchContext::Cutscene;
        }

        // The left input renders and reads as a D-pad in menus always, and in
        // gameplay too when the player opted in (issue #68). Cutscenes use neither.
        const bool useDpad = (context != ETouchContext::Normal && context != ETouchContext::Cutscene) ||
            (context == ETouchContext::Normal &&
             Config::TouchStickMode == EAndroidTouchStickMode::Dpad);
        // The final boss currently exposes no public Light Gaia/Super Sonic
        // flag. Keep normal Sonic on the custom profile and use the complete
        // standard set for the non-Sonic character state while logging both
        // signals for identifying the remaining final-boss transition.
        const bool finalBossDefaultProfile = finalBossStage && App::s_isWerehog &&
            !gaiaColossusProfile && !superSonicProfile;
        const bool daytimeProfile = !g_legacyTouchControls && context == ETouchContext::Normal &&
            !App::s_isWerehog && !finalBossStage;
        const uint64_t qteStamp = g_qteSeenAtMs.load(std::memory_order_relaxed);
        const bool actualQteActive = qteStamp != 0 &&
            SDL_GetTicks64() - qteStamp < QTE_STAMP_FRESH_MS;
        const bool tornadoDefenseActive = SDL_GetTicks64() -
            g_tornadoDefenseSeenAtMs.load(std::memory_order_relaxed) < QTE_STAMP_FRESH_MS;
        const bool qteSignalActive = actualQteActive || tornadoDefenseActive;
        const bool qteActive = qteSignalActive && context != ETouchContext::Pause;

        const bool qteLayoutActive = qteActive && !finalHudFooterVisible &&
            (context == ETouchContext::Normal || context == ETouchContext::Cutscene);
        const bool finalBossLayoutUnknown = context == ETouchContext::Normal &&
            finalBossStage && !gaiaColossusProfile && !superSonicProfile && !qteLayoutActive;

        // Do not guess a gameplay layout during the final-boss transitions.
        // Keep pause available while the HUD signals settle on Gaia or Super Sonic.
        if (finalBossLayoutUnknown)
        {
            g_stickFingerId = (SDL_FingerID)-1;
            g_rstickFingerId = (SDL_FingerID)-1;
            g_camFingerId = (SDL_FingerID)-1;

            std::vector<ImVec2> pausePts;
            pausePts.reserve(fps.size());
            for (const auto& fp : fps)
                pausePts.push_back(fp.pos);

            const float pauseHW = MENU_HW * vh * g_layout.scale;
            const float pauseHH = MENU_HH * vh * g_layout.scale;
            if (DrawWideButton(dl, pausePts, ElemRectOf(g_layout, TC_START, vw, vh).c,
                pauseHW, pauseHH, pauseHW, pauseHH, EButtonIcon::Start))
                st.wButtons |= XAMINPUT_GAMEPAD_START;

            g_state = st;
            g_prevIds = std::move(curIds);
            return;
        }

        if (context == ETouchContext::Normal && !gameplayHudVisible && !stageLoaded && !qteLayoutActive)
        {
            g_state = {};
            g_stickFingerId = (SDL_FingerID)-1;
            g_rstickFingerId = (SDL_FingerID)-1;
            g_camFingerId = (SDL_FingerID)-1;
            g_prevIds = std::move(curIds);
            return;
        }

        if (context == ETouchContext::Cutscene && !actualQteActive && !tornadoDefenseActive)
        {
            g_stickFingerId = (SDL_FingerID)-1;
            g_rstickFingerId = (SDL_FingerID)-1;
            g_camFingerId = (SDL_FingerID)-1;

            // One wide SKIP button tucked into the top-right corner, away from the
            // achievement overlay (top centre) and any subtitles (bottom).
            const float skipHW = MENU_HW * vh * g_layout.scale * 2.2f;
            const float skipHH = MENU_HH * vh * g_layout.scale;
            const ImVec2 skipC = { vw - skipHW - vh * 0.03f, vh * 0.03f + skipHH };

            std::vector<ImVec2> pts;
            pts.reserve(fps.size());
            for (const auto& fp : fps)
                pts.push_back(fp.pos);

            const bool pressed = AnyFingerInRect(pts,
                { skipC.x - skipHW, skipC.y - skipHH }, { skipC.x + skipHW, skipC.y + skipHH });
            if (pressed)
                st.wButtons |= XAMINPUT_GAMEPAD_START;

            dl->AddRectFilled({ skipC.x - skipHW, skipC.y - skipHH }, { skipC.x + skipHW, skipC.y + skipHH },
                IM_COL32(0, 0, 0, pressed ? 150 : 90), skipHH * 0.5f);
            dl->AddRect({ skipC.x - skipHW, skipC.y - skipHH }, { skipC.x + skipHW, skipC.y + skipHH },
                IM_COL32(255, 255, 255, 150), skipHH * 0.5f, 0, 2.0f);
            const char* skipLabel = "SKIP >>";
            const ImVec2 ts = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, skipLabel);
            dl->AddText(font, fontPx, { skipC.x - ts.x * 0.5f, skipC.y - ts.y * 0.5f },
                IM_COL32(255, 255, 255, pressed ? 255 : 220), skipLabel);

            g_state = st;
            g_prevIds = std::move(curIds);
            return;
        }

        if (qteLayoutActive)
        {
            g_stickFingerId = (SDL_FingerID)-1;
            g_rstickFingerId = (SDL_FingerID)-1;
            g_camFingerId = (SDL_FingerID)-1;

            g_qteLayout.scale = g_layout.scale;
            const float qteX[] = { 0.20f, 0.13f, 0.27f, 0.73f, 0.87f, 0.80f };
            const float qteY[] = { 0.68f, 0.84f, 0.84f, 0.84f, 0.84f, 0.68f };
            const int qteControl[] = { TC_LB, TC_A, TC_B, TC_X, TC_Y, TC_RB };
            for (int i = 0; i < 6; ++i)
            {
                g_qteLayout.x[qteControl[i]] = qteX[i];
                g_qteLayout.y[qteControl[i]] = qteY[i];
            }
            g_qteLayout.x[TC_START] = 0.91f;
            g_qteLayout.y[TC_START] = 0.09f;

            const float qteFaceR = FACE_BTN_R * vh * g_qteLayout.scale;
            const float qteShoulderHW = SHOULDER_HW * vh * g_qteLayout.scale;
            const float qteShoulderHH = SHOULDER_HH * vh * g_qteLayout.scale;
            std::vector<ImVec2> qtePts;
            qtePts.reserve(fps.size());
            for (const auto& fp : fps)
                qtePts.push_back(fp.pos);

            if (DrawWideButton(dl, qtePts, ElemRectOf(g_qteLayout, TC_LB, vw, vh).c,
                qteShoulderHW, qteShoulderHH, qteShoulderHW * 0.75f, qteShoulderHH * 0.85f, EButtonIcon::LB))
                st.wButtons |= XAMINPUT_GAMEPAD_LEFT_SHOULDER;
            DrawFaceButton(dl, qtePts, ElemRectOf(g_qteLayout, TC_A, vw, vh).c, qteFaceR,
                EButtonIcon::A, XAMINPUT_GAMEPAD_A, st);
            DrawFaceButton(dl, qtePts, ElemRectOf(g_qteLayout, TC_B, vw, vh).c, qteFaceR,
                EButtonIcon::B, XAMINPUT_GAMEPAD_B, st);
            DrawFaceButton(dl, qtePts, ElemRectOf(g_qteLayout, TC_X, vw, vh).c, qteFaceR,
                EButtonIcon::X, XAMINPUT_GAMEPAD_X, st);
            DrawFaceButton(dl, qtePts, ElemRectOf(g_qteLayout, TC_Y, vw, vh).c, qteFaceR,
                EButtonIcon::Y, XAMINPUT_GAMEPAD_Y, st);
            if (DrawWideButton(dl, qtePts, ElemRectOf(g_qteLayout, TC_RB, vw, vh).c,
                qteShoulderHW, qteShoulderHH, qteShoulderHW * 0.75f, qteShoulderHH * 0.85f, EButtonIcon::RB))
                st.wButtons |= XAMINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (DrawWideButton(dl, qtePts, ElemRectOf(g_qteLayout, TC_START, vw, vh).c,
                MENU_HW * vh * g_qteLayout.scale, MENU_HH * vh * g_qteLayout.scale,
                MENU_HW * vh * g_qteLayout.scale, MENU_HH * vh * g_qteLayout.scale, EButtonIcon::Start))
                st.wButtons |= XAMINPUT_GAMEPAD_START;

            g_state = st;
            g_prevIds = std::move(curIds);
            return;
        }

        // ---- Left analog stick ----
        const bool werehogProfile = !g_legacyTouchControls && context == ETouchContext::Normal &&
            App::s_isWerehog && !finalBossDefaultProfile;
        auto controlRect = [&](int i)
        {
            if (context != ETouchContext::Normal && context != ETouchContext::Cutscene)
            {
                Layout menuLayout = g_layout;
                // Keep the menu's X action directly above A for a compact,
                // predictable confirm/action column.
                if (i == TC_A)
                {
                    menuLayout.x[i] = 0.865f;
                    menuLayout.y[i] = 0.845f;
                }
                else if (i == TC_X)
                {
                    menuLayout.x[i] = 0.865f;
                    menuLayout.y[i] = 0.690f;
                }
                return ElemRectOf(menuLayout, i, vw, vh);
            }
            if (gaiaColossusProfile)
                return GaiaColossusElemRectOf(i, vw, vh);
            return werehogProfile ? WerehogElemRectOf(i, vw, vh) : SonicElemRectOf(i, vw, vh);
        };
        const ImVec2 stickC(g_layout.x[TC_STICK] * vw, g_layout.y[TC_STICK] * vh);
        const float baseR  = STICK_BASE_R  * vh * g_layout.scale;
        const float thumbR = STICK_THUMB_R * vh * g_layout.scale;
        const float zoneR  = STICK_ZONE_R  * vh * g_layout.scale;

        const ImVec2* stickPos = nullptr;
        if (g_stickFingerId != (SDL_FingerID)-1)
        {
            for (const auto& fp : fps)
                if (fp.id == g_stickFingerId) { stickPos = &fp.pos; break; }

            if (!stickPos)
                g_stickFingerId = (SDL_FingerID)-1;
        }
        if (g_stickFingerId == (SDL_FingerID)-1)
        {
            for (const auto& fp : fps)
            {
                const float dx = fp.pos.x - stickC.x;
                const float dy = fp.pos.y - stickC.y;
                if (dx * dx + dy * dy <= zoneR * zoneR)
                {
                    g_stickFingerId = fp.id;
                    stickPos = &fp.pos;
                    break;
                }
            }
        }

        ImVec2 thumbPos = stickC;
        bool stickActive = false;
        if (stickPos && !useDpad)
        {
            const float dx = stickPos->x - stickC.x;
            const float dy = stickPos->y - stickC.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float cl = std::min(len, baseR);
            const float ux = len > 0.0f ? dx / len : 0.0f;
            const float uy = len > 0.0f ? dy / len : 0.0f;

            thumbPos = { stickC.x + ux * cl, stickC.y + uy * cl };

            const float ax = (ux * cl) / baseR;
            const float ay = (uy * cl) / baseR;
            st.sThumbLX = int16_t(std::clamp(ax * 32767.0f, -32767.0f, 32767.0f));
            st.sThumbLY = int16_t(std::clamp(-ay * 32767.0f, -32767.0f, 32767.0f));

            stickActive = true;
        }

        // ---- Camera: virtual right stick ----
        // Menus release the camera finger: a drag there would only feed a camera
        // nobody controls and swallow taps on the right half of the screen.
        const auto cameraMode = context != ETouchContext::Normal || daytimeProfile ||
            gaiaColossusProfile || superSonicProfile
            ? EAndroidTouchCameraMode::Off : Config::TouchCamera.Value;
        if (cameraMode == EAndroidTouchCameraMode::RightStick)
        {
            const ImVec2 rstickC(g_layout.x[TC_RSTICK] * vw, g_layout.y[TC_RSTICK] * vh);
            const float rBaseR  = RSTICK_BASE_R  * vh * g_layout.scale;
            const float rThumbR = RSTICK_THUMB_R * vh * g_layout.scale;
            const float rZoneR  = RSTICK_ZONE_R  * vh * g_layout.scale;

            const ImVec2* rstickPos = nullptr;
            if (g_rstickFingerId != (SDL_FingerID)-1)
            {
                for (const auto& fp : fps)
                    if (fp.id == g_rstickFingerId) { rstickPos = &fp.pos; break; }

                if (!rstickPos)
                    g_rstickFingerId = (SDL_FingerID)-1;
            }
            if (g_rstickFingerId == (SDL_FingerID)-1)
            {
                for (const auto& fp : fps)
                {
                    if (fp.id == g_stickFingerId)
                        continue;

                    const float dx = fp.pos.x - rstickC.x;
                    const float dy = fp.pos.y - rstickC.y;
                    if (dx * dx + dy * dy <= rZoneR * rZoneR)
                    {
                        g_rstickFingerId = fp.id;
                        rstickPos = &fp.pos;
                        break;
                    }
                }
            }

            ImVec2 rThumbPos = rstickC;
            bool rstickActive = false;
            if (rstickPos)
            {
                const float dx = rstickPos->x - rstickC.x;
                const float dy = rstickPos->y - rstickC.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                const float cl = std::min(len, rBaseR);
                const float ux = len > 0.0f ? dx / len : 0.0f;
                const float uy = len > 0.0f ? dy / len : 0.0f;

                rThumbPos = { rstickC.x + ux * cl, rstickC.y + uy * cl };

                const float ax = (ux * cl) / rBaseR;
                const float ay = (uy * cl) / rBaseR;
                st.sThumbRX = int16_t(std::clamp(ax * 32767.0f, -32767.0f, 32767.0f));
                st.sThumbRY = int16_t(std::clamp(-ay * 32767.0f, -32767.0f, 32767.0f));

                rstickActive = true;
            }

            dl->AddCircleFilled(rstickC, rBaseR, IM_COL32(0, 0, 0, rstickActive ? 90 : 55), 48);
            dl->AddCircle(rstickC, rBaseR, IM_COL32(255, 255, 255, 130), 48, 3.0f);
            dl->AddCircleFilled(rThumbPos, rThumbR, IM_COL32(255, 255, 255, rstickActive ? 170 : 110), 32);
        }
        else
        {
            g_rstickFingerId = (SDL_FingerID)-1;
        }

        // ---- Camera: free-area drag on the right half of the screen ----
        if (cameraMode == EAndroidTouchCameraMode::TouchArea)
        {
            const ImVec2* camPos = nullptr;
            if (g_camFingerId != (SDL_FingerID)-1)
            {
                for (const auto& fp : fps)
                    if (fp.id == g_camFingerId) { camPos = &fp.pos; break; }

                if (!camPos)
                    g_camFingerId = (SDL_FingerID)-1;
            }
            if (g_camFingerId == (SDL_FingerID)-1)
            {
                for (const auto& fp : fresh)
                {
                    if (fp.id == g_stickFingerId || fp.pos.x < vw * 0.5f)
                        continue;

                    // A finger that lands on any control belongs to that control.
                    bool onControl = false;
                    for (int i = 0; i < TC_COUNT && !onControl; ++i)
                    {
                        const ElemRect r = controlRect(i);
                        const float hw = r.hw * 1.35f;
                        const float hh = r.hh * 1.35f;
                        onControl = fp.pos.x >= r.c.x - hw && fp.pos.x <= r.c.x + hw &&
                                    fp.pos.y >= r.c.y - hh && fp.pos.y <= r.c.y + hh;
                    }
                    if (onControl)
                        continue;

                    g_camFingerId = fp.id;
                    g_camLastPos = fp.pos;
                    camPos = &fp.pos;
                    break;
                }
            }

            if (camPos)
            {
                const float dx = camPos->x - g_camLastPos.x;
                const float dy = camPos->y - g_camLastPos.y;
                g_camLastPos = *camPos;

                const float full = vh * CAM_DRAG_SENS;
                const float ax = std::clamp(dx / full, -1.0f, 1.0f);
                const float ay = std::clamp(dy / full, -1.0f, 1.0f);
                st.sThumbRX = int16_t(ax * 32767.0f);
                st.sThumbRY = int16_t(-ay * 32767.0f);

                // Subtle feedback dot so the user can tell the drag is being tracked.
                dl->AddCircleFilled(*camPos, vh * 0.012f, IM_COL32(255, 255, 255, 70), 24);
            }
        }
        else
        {
            g_camFingerId = (SDL_FingerID)-1;
        }

        // Buttons are hit-tested against every finger except the stick's and the camera's.
        std::vector<ImVec2> pts;
        pts.reserve(fps.size());
        for (const auto& fp : fps)
            if (fp.id != g_stickFingerId && fp.id != g_rstickFingerId && fp.id != g_camFingerId)
                pts.push_back(fp.pos);

        auto isOnControl = [&](ImVec2 pos)
        {
            for (int i = 0; i < TC_COUNT; ++i)
            {
                if (daytimeProfile && IsHiddenDaytimeControl(i))
                    continue;

                const ElemRect r = controlRect(i);
                const float hw = r.hw * 1.35f;
                const float hh = r.hh * 1.35f;
                if (pos.x >= r.c.x - hw && pos.x <= r.c.x + hw &&
                    pos.y >= r.c.y - hh && pos.y <= r.c.y + hh)
                    return true;
            }
            return false;
        };

        if (daytimeProfile)
        {
            constexpr float SWIPE_DISTANCE = 0.12f;
            for (const auto& fp : fps)
            {
                if (g_swipeTriggered.count(fp.id))
                    continue;

                const float dx = fp.nx - fp.startNx;
                const float dy = fp.ny - fp.startNy;
                if (std::fabs(dx) < SWIPE_DISTANCE || std::fabs(dx) < std::fabs(dy) * 1.4f)
                    continue;

                const ImVec2 startPos(fp.startNx * sw - offX, fp.startNy * sh - offY);
                if (isOnControl(startPos) || isOnControl(fp.pos))
                    continue;

                st.wButtons |= dx < 0.0f
                    ? XAMINPUT_GAMEPAD_LEFT_SHOULDER
                    : XAMINPUT_GAMEPAD_RIGHT_SHOULDER;
                g_swipeTriggered.insert(fp.id);
            }
        }

        if (useDpad)
        {
            DrawDpad(dl, fps, stickC, baseR, zoneR, st);
        }
        else
        {
            dl->AddCircleFilled(stickC, baseR, IM_COL32(0, 0, 0, stickActive ? 90 : 55), 48);
            dl->AddCircle(stickC, baseR, IM_COL32(255, 255, 255, 130), 48, 3.0f);
            dl->AddCircleFilled(thumbPos, thumbR, IM_COL32(255, 255, 255, stickActive ? 170 : 110), 32);
        }

        // A finger that starts on X keeps boost held while it is dragged to RT.
        const float faceR = FACE_BTN_R * vh * g_layout.scale *
            (daytimeProfile || werehogProfile || gaiaColossusProfile || superSonicProfile ? 1.45f : 1.0f);
        const float triggerHW = SHOULDER_HW * vh * g_layout.scale;
        const float triggerHH = SHOULDER_HH * vh * g_layout.scale;
        const float finalButtonGlyphHalfH = faceR * 0.95f;
        const ElemRect xRect = controlRect(TC_X);
        const ElemRect rtRect = controlRect(TC_RT);
        const ImVec2 werehogButtonPts = controlRect(TC_LB).c;
        const float werehogUtilityR = faceR * 1.00f;
        const float werehogShoulderR = faceR * 1.05f;
        const bool menuContext = context != ETouchContext::Normal && context != ETouchContext::Cutscene;
        const bool showMenuX = context == ETouchContext::Title || context == ETouchContext::Menu;
        GuestTexture* finalBoostTexture = gaiaColossusProfile
            ? SelectTouchAsset(g_chipBoostXbox.get(), g_chipBoostPlaystation.get())
            : superSonicProfile
                ? SelectTouchAsset(g_superSonicBoostXbox.get(), g_superSonicBoostPlaystation.get())
                : nullptr;
        GuestTexture* werehogGrab = werehogProfile
            ? SelectTouchAsset(g_werehogGrabXbox.get(), g_werehogGrabPlaystation.get()) : nullptr;
        ImVec4 werehogGrabUv = { 0.0f, 0.0f, 1.0f, 1.0f };
        if (werehogGrab && !UsePlayStationTouchAssets() && werehogGrab->width > werehogGrab->height)
        {
            // The Xbox grab asset is currently a sheet. Its red B button is
            // the second item in the second row of the source sheet.
            werehogGrabUv = { 0.23f, 0.23f, 0.50f, 0.50f };
        }
        const bool jumpPressed = AnyFingerInCircle(pts, controlRect(TC_A).c, faceR * 1.3f);
        const bool attackPressed =
            AnyFingerInCircle(pts, xRect.c, faceR * 1.3f) ||
            AnyFingerInCircle(pts, controlRect(TC_Y).c, faceR * 1.3f);
        const bool werehogStickMoving = stickActive &&
            (std::abs(st.sThumbLX) > 8192 || std::abs(st.sThumbLY) > 8192);

        if (werehogProfile)
        {
            const uint64_t now = SDL_GetTicks64();
            for (const auto& fp : fresh)
            {
                const float dx = fp.pos.x - rtRect.c.x;
                const float dy = fp.pos.y - rtRect.c.y;
                if (dx * dx + dy * dy <= (werehogShoulderR * 1.3f) * (werehogShoulderR * 1.3f))
                    g_werehogRunActive = !g_werehogRunActive;
            }

            if (jumpPressed && !g_werehogJumpWasDown && g_werehogRunActive)
                g_werehogRunJumpUntilMs = now + WEREHOG_RUN_JUMP_GRACE_MS;
            const bool jumpComboWindow = now < g_werehogRunJumpUntilMs;
            if (attackPressed && !g_werehogAttackWasDown && g_werehogRunActive &&
                !jumpComboWindow)
                g_werehogRunAttackUntilMs = now + WEREHOG_RUN_ATTACK_CANCEL_MS;
            else if (jumpComboWindow)
                g_werehogRunAttackUntilMs = 0;

            if (g_werehogRunActive && g_werehogRunAttackUntilMs != 0 &&
                now >= g_werehogRunAttackUntilMs)
            {
                g_werehogRunActive = false;
                g_werehogRunAttackUntilMs = 0;
            }

            if (g_werehogRunActive && !werehogStickMoving &&
                now >= g_werehogRunJumpUntilMs)
            {
                g_werehogRunActive = false;
                g_werehogRunJumpUntilMs = 0;
            }

            for (const auto& fp : fps)
            {
                if (fp.id == g_stickFingerId || fp.id == g_rstickFingerId || fp.id == g_camFingerId)
                    continue;

                const float dx = fp.pos.x - werehogButtonPts.x;
                const float dy = fp.pos.y - werehogButtonPts.y;
                if (dx * dx + dy * dy <= (werehogShoulderR * 1.3f) * (werehogShoulderR * 1.3f))
                    g_werehogDefenseFingerIds.insert(fp.id);
            }

            g_werehogStickWasActive = werehogStickMoving;
            g_werehogJumpWasDown = jumpPressed;
            g_werehogAttackWasDown = attackPressed;
        }
        else
        {
            g_werehogDefenseFingerIds.clear();
            g_werehogRunActive = false;
            g_werehogJumpWasDown = false;
            g_werehogAttackWasDown = false;
            g_werehogStickWasActive = false;
            g_werehogRunJumpUntilMs = 0;
            g_werehogRunAttackUntilMs = 0;
        }
        for (const auto& fp : fps)
        {
            if (!daytimeProfile && !superSonicProfile && !gaiaColossusProfile)
                break;

            if (g_boostFingerIds.count(fp.id))
                continue;

            const float xdx = fp.pos.x - xRect.c.x;
            const float xdy = fp.pos.y - xRect.c.y;
            if (xdx * xdx + xdy * xdy <= (faceR * 1.35f) * (faceR * 1.35f))
                g_boostFingerIds.insert(fp.id);
        }

        bool boostPressed = false;
        bool driftPressed = false;
        for (const auto& fp : fps)
        {
            if (!daytimeProfile && !superSonicProfile && !gaiaColossusProfile)
                break;

            if (!g_boostFingerIds.count(fp.id))
                continue;

            const float rtRadius = std::max(faceR * 1.8f, triggerHW * 1.8f);
            const float rtDx = fp.pos.x - rtRect.c.x;
            const float rtDy = fp.pos.y - rtRect.c.y;
            const bool onRt = rtDx * rtDx + rtDy * rtDy <= rtRadius * rtRadius;
            const bool onX = (fp.pos.x - xRect.c.x) * (fp.pos.x - xRect.c.x) +
                (fp.pos.y - xRect.c.y) * (fp.pos.y - xRect.c.y) <= (faceR * 1.35f) * (faceR * 1.35f);
            boostPressed = boostPressed || onX || (daytimeProfile && onRt);
            driftPressed = driftPressed || (daytimeProfile && onRt);
        }
        if (boostPressed)
            st.wButtons |= XAMINPUT_GAMEPAD_X;
        if (driftPressed)
            st.bRightTrigger = 255;

        if (daytimeProfile)
        {
            DrawTouchConnection(dl, g_sonicConnect.get(), xRect.c, rtRect.c, faceR * 0.18f, 210);
        }
        else if (werehogProfile)
        {
            const ImVec2 lbCenter = controlRect(TC_LB).c;
            DrawTouchConnection(dl, g_werehogConnect.get(), lbCenter, controlRect(TC_X).c,
                faceR * 0.18f, 210);
            DrawTouchConnection(dl, g_werehogConnect.get(), lbCenter, controlRect(TC_Y).c,
                faceR * 0.18f, 210);
            DrawTouchConnection(dl, g_werehogConnect.get(), lbCenter, controlRect(TC_A).c,
                faceR * 0.18f, 210);
        }

        // ---- Face buttons ----
        if (context != ETouchContext::Cutscene)
        {
            if (menuContext || (!gaiaColossusProfile && !superSonicProfile))
            {
                DrawFaceButton(dl, pts, controlRect(TC_A).c, faceR, EButtonIcon::A,
                    XAMINPUT_GAMEPAD_A, st,
                    daytimeProfile ? SelectTouchAsset(g_sonicJumpXbox.get(), g_sonicJumpPlaystation.get()) :
                    werehogProfile ? SelectTouchAsset(g_werehogJumpXbox.get(), g_werehogJumpPlaystation.get()) : nullptr);
                DrawFaceButton(dl, pts, controlRect(TC_B).c, faceR, EButtonIcon::B,
                    XAMINPUT_GAMEPAD_B, st,
                    daytimeProfile ? SelectTouchAsset(g_sonicSSXbox.get(), g_sonicSSPlaystation.get()) :
                    werehogGrab, werehogGrabUv);
            }
        }
        if ((!menuContext && !daytimeProfile && !werehogProfile) || showMenuX)
        {
            DrawFaceButton(dl, pts, xRect.c, faceR, EButtonIcon::X,
                XAMINPUT_GAMEPAD_X, st, showMenuX ? nullptr : finalBoostTexture);
        }
        else if (daytimeProfile || werehogProfile)
        {
            DrawFaceButton(dl, pts, xRect.c, faceR, EButtonIcon::X,
                daytimeProfile ? 0 : XAMINPUT_GAMEPAD_X, st,
                daytimeProfile ? SelectTouchAsset(g_sonicBoostXbox.get(), g_sonicBoostPlaystation.get()) :
                SelectTouchAsset(g_werehogAtk1Xbox.get(), g_werehogAtk1Playstation.get()));
        }
        if (!menuContext && !gaiaColossusProfile && !superSonicProfile)
        {
            DrawFaceButton(dl, pts, controlRect(TC_Y).c, faceR, EButtonIcon::Y,
                XAMINPUT_GAMEPAD_Y, st,
                daytimeProfile ? SelectTouchAsset(g_sonicDashXbox.get(), g_sonicDashPlaystation.get()) :
                werehogProfile ? SelectTouchAsset(g_werehogAtk2Xbox.get(), g_werehogAtk2Playstation.get()) : nullptr);
        }

        // ---- Shoulders ----
        const float shHW = SHOULDER_HW * vh * g_layout.scale;
        const float shHH = SHOULDER_HH * vh * g_layout.scale;
        if (context == ETouchContext::Settings)
        {
            if (DrawWideButton(dl, pts, controlRect(TC_LB).c, shHW, shHH,
                shHW * 0.75f, shHH * 0.85f, EButtonIcon::LB))
                st.wButtons |= XAMINPUT_GAMEPAD_LEFT_SHOULDER;
            if (DrawWideButton(dl, pts, controlRect(TC_RB).c, shHW, shHH,
                shHW * 0.75f, shHH * 0.85f, EButtonIcon::RB))
                st.wButtons |= XAMINPUT_GAMEPAD_RIGHT_SHOULDER;
        }
        else if (context == ETouchContext::Normal && !daytimeProfile && !werehogProfile && !gaiaColossusProfile && !superSonicProfile)
        {
            if (DrawWideButton(dl, pts, controlRect(TC_LB).c, shHW, shHH, shHW * 0.75f, shHH * 0.85f, EButtonIcon::LB))
                st.wButtons |= XAMINPUT_GAMEPAD_LEFT_SHOULDER;
            if (DrawWideButton(dl, pts, controlRect(TC_RB).c, shHW, shHH, shHW * 0.75f, shHH * 0.85f, EButtonIcon::RB))
                st.wButtons |= XAMINPUT_GAMEPAD_RIGHT_SHOULDER;
        }
        else if (context == ETouchContext::Normal && gaiaColossusProfile &&
            DrawWideButton(dl, pts, controlRect(TC_LB).c, shHW, shHH,
                finalButtonGlyphHalfH, finalButtonGlyphHalfH, EButtonIcon::LB,
                SelectTouchAsset(g_chipGuardXbox.get(), g_chipGuardPlaystation.get())))
            st.wButtons |= XAMINPUT_GAMEPAD_LEFT_SHOULDER;

        // ---- Triggers ----
        if (context == ETouchContext::Normal && !daytimeProfile && !werehogProfile &&
            !gaiaColossusProfile && !superSonicProfile &&
            DrawWideButton(dl, pts, controlRect(TC_LT).c, shHW, shHH, shHH * 0.95f, shHH * 0.95f, EButtonIcon::LT))
            st.bLeftTrigger = 255;
        if (werehogProfile)
        {
            DrawCircularButton(dl, pts, controlRect(TC_LB).c, werehogShoulderR, EButtonIcon::LB,
                SelectTouchAsset(g_werehogGuardXbox.get(), g_werehogGuardPlaystation.get()));
            if (!g_werehogDefenseFingerIds.empty())
                st.wButtons |= XAMINPUT_GAMEPAD_LEFT_SHOULDER;
            if (DrawCircularButton(dl, pts, controlRect(TC_RB).c, werehogUtilityR, EButtonIcon::RB,
                SelectTouchAsset(g_werehogUnleashXbox.get(), g_werehogUnleashPlaystation.get())))
                st.wButtons |= XAMINPUT_GAMEPAD_RIGHT_SHOULDER;

            DrawCircularButton(dl, pts, controlRect(TC_RT).c, werehogShoulderR, EButtonIcon::RT,
                SelectTouchAsset(g_werehogDashXbox.get(), g_werehogDashPlaystation.get()));
            if (g_werehogRunActive &&
                (werehogStickMoving || SDL_GetTicks64() < g_werehogRunJumpUntilMs))
                st.bRightTrigger = 255;
        }
        else if (daytimeProfile)
        {
            const float driftR = faceR * 1.18f;
            if (DrawCircularButton(dl, pts, controlRect(TC_RT).c, driftR, EButtonIcon::RT,
                SelectTouchAsset(g_sonicDriftXbox.get(), g_sonicDriftPlaystation.get())))
                st.bRightTrigger = 255;
        }
        else if (gaiaColossusProfile)
        {
            if (DrawWideButton(dl, pts, controlRect(TC_LT).c, shHW, shHH,
                finalButtonGlyphHalfH, finalButtonGlyphHalfH, EButtonIcon::LT,
                SelectTouchAsset(g_chipAtkLeftXbox.get(), g_chipAtkLeftPlaystation.get())))
                st.bLeftTrigger = 255;
            if (DrawWideButton(dl, pts, controlRect(TC_RT).c, shHW, shHH,
                finalButtonGlyphHalfH, finalButtonGlyphHalfH, EButtonIcon::RT,
                SelectTouchAsset(g_chipAtkRightXbox.get(), g_chipAtkRightPlaystation.get())))
                st.bRightTrigger = 255;
        }
        else if (context == ETouchContext::Normal && !superSonicProfile &&
            DrawWideButton(dl, pts, controlRect(TC_RT).c, shHW, shHH,
            shHH * 0.95f, shHH * 0.95f, EButtonIcon::RT))
            st.bRightTrigger = 255;

        // ---- Start / Back ----
        const float menuScale = context == ETouchContext::Menu ? 0.78f : 1.0f;
        const float menuHW = MENU_HW * vh * g_layout.scale * menuScale;
        const float menuHH = MENU_HH * vh * g_layout.scale * menuScale;
        if (context == ETouchContext::Normal &&
            DrawWideButton(dl, pts, controlRect(TC_START).c, menuHW, menuHH, menuHW, menuHH, EButtonIcon::Start))
            st.wButtons |= XAMINPUT_GAMEPAD_START;
        if (context == ETouchContext::Pause &&
            DrawWideButton(dl, pts, ElemRectOf(TC_BACK, vw, vh).c, menuHW, menuHH, menuHW, menuHH, EButtonIcon::Back))
            st.wButtons |= XAMINPUT_GAMEPAD_BACK;

        g_state = st;

        g_prevIds = std::move(curIds);
        return;
    }

    // -----------------------------------------------------------------------
    // Editor mode.
    // -----------------------------------------------------------------------
    g_state = {};
    g_stickFingerId = (SDL_FingerID)-1;
    g_rstickFingerId = (SDL_FingerID)-1;
    g_camFingerId = (SDL_FingerID)-1;

    // Dimmed backdrop.
    dl->AddRectFilled({ 0.0f, 0.0f }, { vw, vh }, IM_COL32(0, 0, 0, 120));

    // Action bar (top). Handle taps first so a tap on a button never starts a drag.
    const float barY   = vh * 0.055f;
    const float barHH  = vh * 0.040f;
    const float wideHW = vw * 0.085f;
    const float sizeHW = vh * 0.050f;

    const ImVec2 resetC(vw * 0.30f, barY);
    const ImVec2 minusC(vw * 0.44f, barY);
    const ImVec2 plusC (vw * 0.56f, barY);
    const ImVec2 doneC (vw * 0.70f, barY);

    bool changed = false;
    std::unordered_set<SDL_FingerID> consumed;

    // A tap that hits an action button is "consumed" so the same finger can't also
    // start dragging a control placed underneath the bar.
    auto tapConsume = [&](ImVec2 c, float hw, float hh) -> bool
    {
        bool hit = false;
        for (const auto& f : fresh)
            if (f.pos.x >= c.x - hw && f.pos.x <= c.x + hw && f.pos.y >= c.y - hh && f.pos.y <= c.y + hh)
            {
                hit = true;
                consumed.insert(f.id);
            }
        return hit;
    };

    if (tapConsume(doneC, wideHW, barHH))
    {
        SaveLayout();
        g_edit = false;
        g_dragElem = -1;
        g_prevIds = std::move(curIds);
        return;
    }
    if (tapConsume(resetC, wideHW, barHH))
    {
        g_layout = kDefault;
        changed = true;
    }
    if (tapConsume(minusC, sizeHW, barHH))
    {
        g_layout.scale = std::clamp(g_layout.scale - 0.05f, SCALE_MIN, SCALE_MAX);
        changed = true;
    }
    if (tapConsume(plusC, sizeHW, barHH))
    {
        g_layout.scale = std::clamp(g_layout.scale + 0.05f, SCALE_MIN, SCALE_MAX);
        changed = true;
    }

    // ---- Dragging ----
    if (g_dragElem >= 0)
    {
        const ImVec2* dp = nullptr;
        for (const auto& fp : fps)
            if (fp.id == g_dragFinger) { dp = &fp.pos; break; }

        if (!dp)
        {
            g_dragElem = -1;      // finger lifted -> commit
            changed = true;
        }
        else
        {
            g_layout.x[g_dragElem] = std::clamp(dp->x / vw + g_grabX, 0.02f, 0.98f);
            g_layout.y[g_dragElem] = std::clamp(dp->y / vh + g_grabY, 0.02f, 0.98f);
        }
    }

    if (g_dragElem < 0)
    {
        for (const auto& f : fresh)
        {
            if (consumed.count(f.id))
                continue;

            for (int i = 0; i < TC_COUNT; ++i)
            {
                const ElemRect r = ElemRectOf(i, vw, vh);
                bool hit;
                if (r.round)
                {
                    const float dx = f.pos.x - r.c.x;
                    const float dy = f.pos.y - r.c.y;
                    hit = dx * dx + dy * dy <= r.hw * r.hw;
                }
                else
                {
                    hit = f.pos.x >= r.c.x - r.hw && f.pos.x <= r.c.x + r.hw &&
                          f.pos.y >= r.c.y - r.hh && f.pos.y <= r.c.y + r.hh;
                }

                if (hit)
                {
                    g_dragElem = i;
                    g_dragFinger = f.id;
                    g_grabX = g_layout.x[i] - f.pos.x / vw;
                    g_grabY = g_layout.y[i] - f.pos.y / vh;
                    break;
                }
            }

            if (g_dragElem >= 0)
                break;
        }
    }

    // ---- Draw all controls with selection outlines ----
    for (int i = 0; i < TC_COUNT; ++i)
    {
        const ElemRect r = ElemRectOf(i, vw, vh);
        DrawElemVisual(dl, i, r);

        const ImU32 col = (i == g_dragElem) ? IM_COL32(255, 220, 60, 255) : IM_COL32(70, 200, 110, 220);
        if (r.round)
            dl->AddCircle(r.c, r.hw, col, 40, 3.0f);
        else
            dl->AddRect({ r.c.x - r.hw, r.c.y - r.hh }, { r.c.x + r.hw, r.c.y + r.hh }, col, 6.0f, 0, 3.0f);
    }

    // ---- Action bar on top ----
    tapBox(resetC, wideHW, barHH, "RESET", false);
    tapBox(minusC, sizeHW, barHH, "-", false);
    tapBox(plusC,  sizeHW, barHH, "+", false);
    tapBox(doneC,  wideHW, barHH, "DONE", true);

    char sizeLabel[32];
    snprintf(sizeLabel, sizeof(sizeLabel), "SIZE %d%%", int(g_layout.scale * 100.0f + 0.5f));
    const ImVec2 slSize = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, sizeLabel);
    dl->AddText(font, fontPx, { vw * 0.50f - slSize.x * 0.5f, barY + barHH * 1.4f }, IM_COL32(255, 255, 255, 230), sizeLabel);

    const char* hint = "Drag buttons to arrange";
    const ImVec2 hintSize = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, hint);
    dl->AddText(font, fontPx, { vw * 0.50f - hintSize.x * 0.5f, vh * 0.14f }, IM_COL32(255, 255, 255, 200), hint);

    if (changed)
        SaveLayout();

    g_prevIds = std::move(curIds);
}
