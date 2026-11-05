#pragma once
#include <cmath>

namespace roadtrip {
// A 3 km/h target-relative hysteresis prevents repeated mode changes near
// the setpoint. Recovery ends at the target, without waiting for overspeed.
struct CruiseAcceleration {
    static constexpr float recovery_gap_kmh=3.0f;
    static constexpr float recovery_gap_mps=recovery_gap_kmh/3.6f;
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
        if(!active && speed>=target-recovery_gap_mps){reason="within_recovery_band";return;}
        active=true;reason=increased?"target_increased":"speed_deficit";
    }
};
}
