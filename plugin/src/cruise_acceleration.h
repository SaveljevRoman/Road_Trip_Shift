#pragma once
#include <cmath>

namespace roadtrip {
// Economy borrows the existing Normal path whenever active cruise has speed
// to recover. No fixed road speed, gear target or percentage error is used.
struct CruiseAcceleration {
    bool initialized=false,active=false;
    float previous_target=0;
    const char *reason="inactive";
    void reset(){initialized=active=false;previous_target=0;reason="inactive";}
    void update(bool eligible,bool economy,bool kickdown,float target,float speed,float input,float brake) {
        if(!eligible || !std::isfinite(target) || !std::isfinite(speed) || !std::isfinite(input) ||
           !std::isfinite(brake) || target<=0 || speed<0) {reset();return;}
        bool increased=initialized && target>previous_target;
        initialized=true;previous_target=target;
        if(!economy){active=false;reason="selected_mode";return;}
        if(kickdown || input>0 || brake>0){active=false;reason="driver_override";return;}
        if(speed>=target){active=false;reason="target_reached";return;}
        active=true;reason=increased?"target_increased":"speed_deficit";
    }
};
}
