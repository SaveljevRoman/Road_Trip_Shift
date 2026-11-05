#pragma once
#include <cmath>

namespace roadtrip {
// Observe a requested cruise-target increase, not an arbitrary road speed or
// throttle percentage. Only the selected Economy mode temporarily borrows Normal.
struct CruiseAcceleration {
    bool initialized=false,active=false;
    float previous_target=0;
    const char *reason="inactive";
    void reset(){initialized=active=false;previous_target=0;reason="inactive";}
    void update(bool eligible,bool economy,bool kickdown,float target,float speed,float input,float brake) {
        if(!eligible || !std::isfinite(target) || !std::isfinite(speed) || !std::isfinite(input) ||
           !std::isfinite(brake) || target<=0 || speed<0) {
            reset();return;
        }
        if(!initialized){initialized=true;previous_target=target;}
        bool increased=target>previous_target,decreased=target<previous_target;
        previous_target=target;
        if(!economy){active=false;reason="selected_mode";return;}
        if(kickdown || input>0 || brake>0){active=false;reason="driver_override";return;}
        if(decreased){active=false;reason="target_decreased";return;}
        if(speed>=target){active=false;reason="target_reached";return;}
        if(increased){active=true;reason="target_increased";}
    }
};
}
