#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace roadtrip {
inline constexpr float high_power=1.6667f;
inline constexpr float economy_mode=10.0f,normal_mode=3.0f;
inline bool economy_selected(float value){return std::fabs(value-economy_mode)<0.0001f;}
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
inline float mode(float original, bool scoped, bool kick, float power,bool cruise_normal=false) {
    // Same highest-priority High Power shadow as main, for every selected mode.
    if(scoped && kick)return power;
    return scoped && cruise_normal && economy_selected(original)?normal_mode:original;
}
inline const char *mode_source(bool scoped,bool kick,bool cruise_normal=false) {
    return scoped && kick ? "kickdown" : scoped && cruise_normal?"cruise_acceleration":"configured";
}
inline bool mode_override(float original,bool scoped,bool kick,float power,bool cruise_normal=false) {
    return std::fabs(original-mode(original,scoped,kick,power,cruise_normal))>0.0001f;
}
inline const char *mode_name(float value) {
    if(std::fabs(value)<0.0001f)return "off";
    if(std::fabs(value-10.0f)<0.0001f)return "economy";
    if(std::fabs(value-3.0f)<0.0001f)return "normal";
    if(std::fabs(value-high_power)<0.0001f)return "high_power";
    return "custom";
}
}
