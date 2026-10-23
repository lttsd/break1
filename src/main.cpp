#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    constexpr int64_t MAX_MINUTES = 180; // 3 hours hard limit
    constexpr int64_t SNOOZE_MINUTES = 5;

    // Active play time, in seconds (idle time is not added).
    double s_sessionSec = 0.0;   // since the game was opened
    double s_countdownSec = 0.0; // since the last popup was dismissed
    double s_dailySec = 0.0;     // today, saved between sessions
    double s_sinceSaveSec = 0.0;

    Clock::time_point s_lastTick = Clock::now();
    bool s_popupOpen = false;
    bool s_active = true;
    bool s_dailyLoaded = false;
    int s_frameCounter = 0;
    std::string s_dayKey;

    // ---------- settings helpers ----------

    bool getBool(char const* key) {
        return Mod::get()->getSettingValue<bool>(key);
    }

    int64_t getInt(char const* key) {
        return Mod::get()->getSettingValue<int64_t>(key);
    }

    int64_t getIntervalMinutes() {
        return std::clamp<int64_t>(getInt("interval-minutes"), 1, MAX_MINUTES);
    }

    // ---------- texts ----------

    struct Strings {
        char const* title;
        char const* body;
        char const* ok;
        char const* snooze;
        char const* todayButton;
        char const* todayTitle;
        char const* todayBody;
    };

    // Spanish texts avoid accents and inverted marks on purpose, because the
    // game's big font does not draw them correctly.
    Strings const EN = {
        "Time for a break!",
        "You have been playing for <cy>{time}</c>.\nMaybe stand up, stretch and drink some water.",
        "OK",
        "Snooze 5 min",
        "Today",
        "Play time today",
        "Today you have played <cy>{today}</c>.\nSince you opened the game: <cy>{time}</c>."
    };

    Strings const ES = {
        "Hora de un descanso!",
        "Llevas <cy>{time}</c> jugando.\nLevantate, estirate y tomate un vaso de agua.",
        "OK",
        "Posponer 5 min",
        "Hoy",
        "Tiempo jugado hoy",
        "Hoy llevas <cy>{today}</c> jugando.\nDesde que abriste el juego: <cy>{time}</c>."
    };

    Strings const& getStrings() {
        auto lang = Mod::get()->getSettingValue<std::string>("language");
        return lang == "Spanish" ? ES : EN;
    }

    std::string replaceAll(std::string s, std::string const& from, std::string const& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    }

    std::string formatDuration(int64_t seconds) {
        int64_t totalMinutes = seconds / 60;
        int64_t hours = totalMinutes / 60;
        int64_t mins = totalMinutes % 60;
        if (hours > 0) {
            return fmt::format("{}h {}min", hours, mins);
        }
        return fmt::format("{} min", mins);
    }

    std::string renderText(std::string text) {
        text = replaceAll(text, "\\n", "\n");
        text = replaceAll(text, "{time}", formatDuration(static_cast<int64_t>(s_sessionSec)));
        text = replaceAll(text, "{today}", formatDuration(static_cast<int64_t>(s_dailySec)));
        return text;
    }

    // ---------- daily time ----------

    std::string todayKey() {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef GEODE_IS_WINDOWS
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[16] = {};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
        return std::string(buf);
    }

    void saveDaily() {
        s_sinceSaveSec = 0.0;
        auto mod = Mod::get();
        mod->setSavedValue<std::string>("daily-date", s_dayKey);
        mod->setSavedValue<int64_t>("daily-seconds", static_cast<int64_t>(s_dailySec));
        (void)mod->saveData();
    }

    void initDaily() {
        s_dailyLoaded = true;
        s_dayKey = todayKey();
        auto mod = Mod::get();
        auto savedDate = mod->getSavedValue<std::string>("daily-date", "");
        if (savedDate == s_dayKey) {
            s_dailySec = static_cast<double>(mod->getSavedValue<int64_t>("daily-seconds", 0));
        }
    }

    void rollDayIfNeeded() {
        auto key = todayKey();
        if (key != s_dayKey) {
            s_dayKey = key;
            s_dailySec = 0.0;
            saveDaily();
        }
    }

    // ---------- activity ----------

    bool computeActive() {
        if (!getBool("ignore-idle")) return true;
#ifdef GEODE_IS_WINDOWS
        // The game must be the foreground window.
        HWND fg = GetForegroundWindow();
        if (!fg) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return false;

        // And the user must have used the keyboard or mouse recently.
        LASTINPUTINFO lii{};
        lii.cbSize = sizeof(lii);
        if (!GetLastInputInfo(&lii)) return true;
        uint64_t idleMs = static_cast<uint64_t>(static_cast<DWORD>(GetTickCount() - lii.dwTime));
        int64_t limit = std::clamp<int64_t>(getInt("idle-minutes"), 1, 30);
        return idleMs < static_cast<uint64_t>(limit) * 60000ull;
#else
        return true;
#endif
    }

    // ---------- popup logic ----------

    bool isDue() {
        return !s_popupOpen && s_countdownSec >= static_cast<double>(getIntervalMinutes() * 60);
    }

    bool canShowNow() {
        auto pl = PlayLayer::get();
        if (!pl) return true;
        if (!getBool("wait-for-pause")) return true;

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (scene && scene->getChildByType<PauseLayer>(0)) return true;

        if (getBool("show-on-death-or-complete")) {
            if (pl->getChildByType<EndLevelLayer>(0)) return true;
            if (scene && scene->getChildByType<EndLevelLayer>(0)) return true;
        }
        return false;
    }

    void showReminder() {
        s_popupOpen = true;

        auto const& tr = getStrings();
        auto customTitle = Mod::get()->getSettingValue<std::string>("custom-title");
        auto customMessage = Mod::get()->getSettingValue<std::string>("custom-message");

        std::string title = customTitle.empty() ? std::string(tr.title) : renderText(customTitle);
        std::string message = renderText(customMessage.empty() ? std::string(tr.body) : customMessage);
        auto interval = getIntervalMinutes();

        createQuickPopup(
            title.c_str(),
            message,
            tr.ok,
            tr.snooze,
            [interval](FLAlertLayer*, bool snooze) {
                s_popupOpen = false;
                if (snooze) {
                    // Make the popup come back in SNOOZE_MINUTES.
                    int64_t remaining = (std::min)(interval, SNOOZE_MINUTES);
                    s_countdownSec = static_cast<double>((interval - remaining) * 60);
                } else {
                    s_countdownSec = 0.0;
                }
            }
        );
    }

    void tick() {
        if (!s_dailyLoaded) initDaily();

        auto now = Clock::now();
        double delta = std::chrono::duration<double>(now - s_lastTick).count();
        s_lastTick = now;
        delta = std::clamp(delta, 0.0, 1.0);

        // The (more expensive) checks run about twice per second.
        if (++s_frameCounter >= 30) {
            s_frameCounter = 0;
            s_active = computeActive();
            rollDayIfNeeded();
        }

        if (s_active) {
            s_sessionSec += delta;
            s_countdownSec += delta;
            s_dailySec += delta;
            s_sinceSaveSec += delta;
        }

        if (s_sinceSaveSec >= 60.0) saveDaily();

        if (!isDue()) return;
        if (!canShowNow()) return;
        showReminder();
    }
}

struct BreakScheduler : Modify<BreakScheduler, CCScheduler> {
    void update(float dt) {
        CCScheduler::update(dt);
        tick();
    }
};

// When the time is up while playing and the player dies, pause on respawn so
// the popup can be shown without the level running behind it.
class $modify(BRPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();

        if (!isDue()) return;
        if (!getBool("wait-for-pause") || !getBool("show-on-death-or-complete")) return;
        if (PlayLayer::get() != this || !this->getParent() || this->m_isPaused) return;

        this->pauseGame(false);
    }
};

// Button that is only added to the main menu.
class $modify(BRMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto const& tr = getStrings();

        auto label = CCLabelBMFont::create(tr.todayButton, "bigFont.fnt");
        label->setScale(0.35f);
        auto spr = CircleButtonSprite::create(label);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(BRMenuLayer::onToday));
        btn->setID("today-button"_spr);
        btn->setPosition({0.f, 0.f});

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("today-menu"_spr);
        menu->setPosition({winSize.width - 30.f, winSize.height - 30.f});
        menu->addChild(btn);
        this->addChild(menu);

        return true;
    }

    void onToday(CCObject*) {
        auto const& tr = getStrings();
        std::string title = tr.todayTitle;
        std::string body = renderText(tr.todayBody);
        FLAlertLayer::create(title.c_str(), body, "OK")->show();
    }
};
