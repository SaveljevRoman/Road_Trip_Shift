#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace roadtrip {
inline constexpr float high_power=1.6667f;
inline bool ford(const char *id) {
    if(!id)return false;
    // SDK 1.16 reports e.g. "vehicle.ford.f150_23" in configuration.id.
    // Normalize exactly one known namespace; keep the four-model whitelist.
    if(!std::strncmp(id,"vehicle.",8))id+=8;
    return !std::strcmp(id,"ford.f150_23") || !std::strcmp(id,"ford.bronco_24") ||
           !std::strcmp(id,"ford.victoria_06") || !std::strcmp(id,"ford.mustang_67");
}
struct Kickdown {
    bool down=false, active=false, armed=false, blocked=false;
    uint64_t first_down=0;
    void cancel(bool held=false) { down=held; active=false; armed=false; blocked=held; }
    bool update(uint64_t now, bool held, bool allowed, unsigned interval) {
        if(!allowed) { cancel(held); return false; }
        if(blocked) { if(!held) { blocked=false; down=false; } return false; }
        if(held && !down) {
            if(armed && now>=first_down && now-first_down<=interval) {
                active=true; armed=false;
            } else { active=false; armed=true; first_down=now; }
        }
        if(!held && down && active) { active=false; armed=false; }
        if(armed && now-first_down>interval) armed=false;
        down=held;
        return active;
    }
};
// Require both the configured physical keyboard key and the game's accelerator
// action. Controller throttle alone never becomes a keyboard double press.
struct AcceleratorInput {
    Kickdown taps;
    bool down=false,confirmed=false;
    uint64_t press_time=0;
    const char *reason="release_required";
    AcceleratorInput() { cancel(); }
    void cancel() { taps.cancel(true); down=false;confirmed=false;reason="release_required"; }
    bool update(uint64_t now,float value,bool keyboard_held,bool allowed,unsigned interval) {
        if(!allowed || !std::isfinite(value) || value<0 || value>1) {
            cancel();return false;
        }
        // Smart Throttle ramps up over several frames. Count physical edges at
        // their actual times, then confirm each stroke with fresh SDK input.
        // A low first frame must not block the entire press until key release.
        if(keyboard_held && !taps.down) {press_time=now;confirmed=false;}
        down=keyboard_held;
        bool requested=taps.update(now,down,true,interval);
        if(taps.blocked){reason="release_required";return false;}
        if(!down) {
            // An unconfirmed tap cannot arm a later press (e.g. stale binding).
            if(!confirmed)taps.cancel();
            confirmed=false;reason="released";return false;
        }
        if(value>0)confirmed=true;
        if(!confirmed) {
            if(now<press_time || now-press_time>interval) {cancel();reason="sdk_confirmation_timeout";}
            else reason="awaiting_sdk_throttle";
            return false;
        }
        if(value<=0){cancel();reason="sdk_throttle_lost";return false;}
        reason=requested?"keyboard_accelerator_double":"single_press";
        return requested;
    }
};
inline float mode(float original, bool scoped, bool kick, float power) {
    return scoped && kick ? power : original;
}
inline const char *mode_source(bool scoped,bool kick) {
    return scoped && kick ? "kickdown" : "configured";
}
inline bool mode_override(float original,bool scoped,bool kick,float power) {
    return scoped && kick && std::fabs(original-power)>0.0001f;
}
inline const char *mode_name(float value) {
    if(std::fabs(value)<0.0001f)return "off";
    if(std::fabs(value-10.0f)<0.0001f)return "economy";
    if(std::fabs(value-3.0f)<0.0001f)return "normal";
    if(std::fabs(value-high_power)<0.0001f)return "high_power";
    return "custom";
}
}
