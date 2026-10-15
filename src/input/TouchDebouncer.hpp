#pragma once
#include "config/Config.hpp"

class TouchDebouncer
{
    enum class Phase
    {
        AwaitRelease,
        ReleasePending,
        Ready,
        PressPending
    };
    Phase phase_{Phase::AwaitRelease};
    uint32_t since_{};
    bool decreases_;

public:
    explicit TouchDebouncer(bool decreases = config::kTouchDecreases) : decreases_(decreases)
    {
    }

    bool isPressed(uint32_t raw, uint32_t baseline) const
    {
        if (!raw || !baseline) return false;
        const auto delta = uint64_t(baseline) * config::kTouchThresholdPercent / 100;
        return decreases_ ? raw <= baseline - delta : raw >= baseline + delta;
    }

    bool isReleased(uint32_t raw, uint32_t baseline) const
    {
        if (!raw || !baseline) return false;
        // Die halbe Schaltschwelle bildet die Hysterese zwischen Drücken und Loslassen.
        const auto delta = uint64_t(baseline) * config::kTouchThresholdPercent / 200;
        return decreases_ ? raw >= baseline - delta : raw <= baseline + delta;
    }

    const char *phaseName() const
    {
        switch (phase_)
        {
        case Phase::AwaitRelease:
            return "held";
        case Phase::ReleasePending:
            return "release-debounce";
        case Phase::Ready:
            return "ready";
        case Phase::PressPending:
            return "press-debounce";
        }
        return "unknown";
    }

    void reset(bool alreadyCountedPress)
    {
        phase_ = alreadyCountedPress ? Phase::AwaitRelease : Phase::Ready;
        since_ = 0;
    }

    bool update(uint32_t raw, uint32_t baseline, uint32_t nowMs)
    {
        if (!baseline || !raw) return false;

        const bool pressed = isPressed(raw, baseline);
        const bool released = isReleased(raw, baseline);

        switch (phase_)
        {
        case Phase::AwaitRelease:
            if (released)
            {
                since_ = nowMs;
                phase_ = Phase::ReleasePending;
            }
            break;

        case Phase::ReleasePending:
            if (!released)
                phase_ = Phase::AwaitRelease;
            else if (uint32_t(nowMs - since_) >= config::kTouchReleaseDebounceMs)
                phase_ = Phase::Ready;
            break;

        case Phase::Ready:
            if (pressed)
            {
                since_ = nowMs;
                phase_ = Phase::PressPending;
            }
            break;

        case Phase::PressPending:
            if (!pressed)
                phase_ = Phase::Ready;
            else if (uint32_t(nowMs - since_) >= config::kTouchPressDebounceMs)
            {
                phase_ = Phase::AwaitRelease;
                return true;
            }
            break;
        }
        return false;
    }
};
