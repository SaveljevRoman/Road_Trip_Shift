#pragma once
#include "kickdown.h"
#include <cstdio>

namespace roadtrip {
struct ModeNotice {
    float selected=0;
    bool seen=false,pending=false,visible=false;
    uint64_t expires=0;
    void reset(){*this={};}
    void observe(float value) {
        if(!std::isfinite(value))return;
        if(seen && std::fabs(value-selected)>0.000001f)pending=true;
        selected=value;seen=true;
    }
    bool start(uint64_t now,unsigned duration) {
        if(!pending)return false;
        pending=false;visible=true;expires=now+duration;return true;
    }
    void cancel(){pending=visible=false;expires=0;}
};
inline void notice_text(float mode,bool russian,char *out,size_t size) {
    const char *color="C0C0C0FF",*en="Custom",*ru="Другой";
    if(std::fabs(mode)<0.0001f){en="Off";ru="Выкл.";}
    else if(economy_selected(mode)){color="60D080FF";en="Eco";ru="Эко";}
    else if(std::fabs(mode-normal_mode)<0.0001f){color="60AAFFFF";en="Normal";ru="Нормальный";}
    else if(std::fabs(mode-high_power)<0.0001f){color="FF6666FF";en="High Power";ru="Мощностной";}
    std::snprintf(out,size,"<color value=%s>%s: %s</color>",color,russian?"КПП":"Transmission",russian?ru:en);
}
}
