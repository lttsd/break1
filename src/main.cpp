#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <algorithm>
#include <chrono>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    // When the game was opened (used for the "you've been playing for..." text).
    Clock::time_point const s_gameStart = Clock::now();
    // When the current countdown started (reset when the popup is dismissed).
    Clock::time_point s_countdownStart = Clock::now();
    bool s_popupOpen = false;
    int s_frameCounter = 0;

    constexpr int64_t MAX_MINUTES = 180; // 3 hours hard limit
    constexpr int64_t SNOOZE_MINUTES = 5;

    int64_t getIntervalMinutes() {
        auto value = Mod::get()->getSettingValue<int64_t>("interval-minutes");
        return std::clamp<int64_t>(value, 1, MAX_MINUTES);
    }

    std::string formatDuration(std::chrono::minutes total) {
        auto hours = total.count() / 60;
        auto mins = total.count() % 60;
        if (hours > 0) {
            return fmt::format("{}h {}min", hours, mins);
        }
        return fmt::format("{} min", mins);
    }

    bool canShowNow() {
        // Outside of levels it is always fine to show it.
        if (!PlayLayer::get()) return true;
        if (!Mod::get()->getSettingValue<bool>("wait-for-pause")) return true;

        // Inside a level: only when the pause menu is open.
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return false;
        return scene->getChildByType<PauseLayer>(0) != nullptr;
    }

    void showReminder() {
        s_popupOpen = true;

        auto played = std::chrono::duration_cast<std::chrono::minutes>(Clock::now() - s_gameStart);
        auto interval = getIntervalMinutes();

        createQuickPopup(
            "Time for a break!",
            fmt::format(
                "You have been playing for <cy>{}</c>.\n"
                "Maybe stand up, stretch and drink some water.",
                formatDuration(played)
            ),
            "OK",
            "Snooze 5 min",
            [interval](FLAlertLayer*, bool snooze) {
                s_popupOpen = false;
                if (snooze) {
                    // Make the popup come back in SNOOZE_MINUTES.
                    auto offset = std::max<int64_t>(0, interval - SNOOZE_MINUTES);
                    s_countdownStart = Clock::now() - std::chrono::minutes(offset);
                } else {
                    s_countdownStart = Clock::now();
                }
            }
        );
    }
}

struct BreakScheduler : Modify<BreakScheduler, CCScheduler> {
    void update(float dt) {
        CCScheduler::update(dt);

        // Only check about twice per second, no need to do it every frame.
        if (++s_frameCounter < 30) return;
        s_frameCounter = 0;

        if (s_popupOpen) return;

        auto elapsed = Clock::now() - s_countdownStart;
        if (elapsed < std::chrono::minutes(getIntervalMinutes())) return;
        if (!canShowNow()) return;

        showReminder();
    }
};
