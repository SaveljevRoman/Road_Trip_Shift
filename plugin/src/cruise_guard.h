#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace roadtrip {
struct CruiseSample {
    bool eligible=false;
    bool kickdown=false;
    float pedal=0,brake=0,speed=0,target=0,throttle=0;
    int gear=0;
};

// Learn from a *completed* failed upshift: a higher gear lost speed under
// substantial throttle and the stock gearbox itself selected a lower gear.
// A temporary ceiling then prevents repeating that upshift until load eases.
// Speeds are m/s; all speed thresholds are relative errors, never set speeds.
struct CruiseGuard {
    int cap=0,trial_gear=0,last_gear=0;
    uint64_t last_time=0,trial_time=0,heavy_time=0,cap_time=0,relaxed_time=0;
    float trial_speed=0,last_target=0;
    bool initialized=false,heavy_seen=false,relaxing=false;
    const char *reason="inactive";

    void reset(const char *why="reset") {
        cap=trial_gear=last_gear=0;
        last_time=trial_time=heavy_time=cap_time=relaxed_time=0;
        trial_speed=last_target=0;
        initialized=heavy_seen=relaxing=false;
        reason=why;
    }
    void update(uint64_t now,const CruiseSample &s) {
        if(!s.eligible){reset("inactive");return;}
        if(s.kickdown){reset("kickdown");return;}
        if(s.pedal>0.05f){reset("manual_gas");return;}
        if(s.brake>0.05f){reset("braking");return;}
        if(!std::isfinite(s.speed)||!std::isfinite(s.target)||!std::isfinite(s.throttle)||
           s.target<=0||s.speed<0||s.gear<0){reset("cruise_off_or_invalid");return;}
        if(initialized && (now<last_time||now-last_time>1000))reset("sample_gap");
        if(initialized && std::fabs(s.target-last_target)>0.5f)reset("target_change");
        if(!initialized) {
            initialized=true;last_target=s.target;last_gear=s.gear;
            // An already engaged gear can also prove unsuitable after cruise resumes.
            trial_gear=s.gear;trial_time=now;trial_speed=s.speed;
        }
        last_time=now;last_target=s.target;
        if(s.throttle>=0.75f){heavy_time=now;heavy_seen=true;}
        if(s.gear>0) {
            if(last_gear>0 && s.gear>last_gear) {
                trial_gear=s.gear;trial_time=now;trial_speed=s.speed;
            } else if(trial_gear>1 && s.gear<trial_gear) {
                bool failed=now-trial_time<=12000 && trial_speed-s.speed>=0.6f &&
                    heavy_seen && now-heavy_time<=1200;
                if(failed) {
                    int learned=trial_gear-1;
                    cap=cap?std::min(cap,learned):learned;
                    cap_time=now;relaxing=false;reason="failed_upshift";
                }
                trial_gear=0;
            }
            last_gear=s.gear;
        }
        if(trial_gear && now-trial_time>12000)trial_gear=0;
        if(cap) {
            bool relaxed=now-cap_time>=8000 && std::fabs(s.target-s.speed)<=0.6f &&
                s.throttle<=0.60f && s.gear>0;
            if(!relaxed)relaxing=false;
            else if(!relaxing){relaxing=true;relaxed_time=now;}
            else if(now-relaxed_time>=3000) {
                cap=0;trial_gear=0;relaxing=false;reason="load_relaxed";
            }
        }
    }
};
inline int guard_choice(int stock,int cap,bool scoped,bool kickdown) {
    return scoped && !kickdown && cap>0 && stock>cap ? cap : stock;
}
}
