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

// Evidence behind the latest learned ceiling, for CRUISE_GUARD HOLD diagnostics.
struct CruiseGuardEvidence {
    int failed_gear=0;
    float trial_speed=0,entry_throttle=0,peak_throttle=0; // trial_speed: highest speed in the upshift chain; entry_throttle: at its first upshift
    uint64_t trial_age_ms=0;
    bool observed_upshift=false;
};
// Learn from a *completed* failed upshift: a higher gear lost speed under
// substantial throttle and the stock gearbox itself selected a lower gear.
// A temporary ceiling then prevents repeating that upshift until load eases.
// Speeds are m/s; all speed thresholds are relative errors, never set speeds.
struct CruiseGuard {
    static constexpr float noise=0.05f; // pedal/brake/throttle changes below this are not driver or load input
    int cap=0,trial_gear=0,last_gear=0;
    uint64_t last_time=0,trial_time=0,heavy_time=0,cap_time=0,relaxed_time=0;
    float trial_speed=0,last_target=0,trial_throttle=0,peak_throttle=0,relaxed_speed=0;
    // false: the trial is only the gear engaged when learning (re)started.
    bool initialized=false,heavy_seen=false,relaxing=false,trial_observed=false;
    const char *reason="inactive";
    CruiseGuardEvidence evidence;

    void reset(const char *why="reset") {
        cap=trial_gear=last_gear=0;
        last_time=trial_time=heavy_time=cap_time=relaxed_time=0;
        trial_speed=last_target=trial_throttle=peak_throttle=relaxed_speed=0;
        initialized=heavy_seen=relaxing=trial_observed=false;
        reason=why;evidence={};
    }
    void update(uint64_t now,const CruiseSample &s) {
        if(!s.eligible){reset("inactive");return;}
        if(s.kickdown){reset("kickdown");return;}
        if(s.pedal>noise){reset("manual_gas");return;}
        if(s.brake>noise){reset("braking");return;}
        if(!std::isfinite(s.speed)||!std::isfinite(s.target)||!std::isfinite(s.throttle)||
           s.target<=0||s.speed<0||s.gear<0){reset("cruise_off_or_invalid");return;}
        if(initialized && (now<last_time||now-last_time>1000))reset("sample_gap");
        if(initialized && std::fabs(s.target-last_target)>0.5f)reset("target_change");
        if(!initialized) {
            initialized=true;last_target=s.target;last_gear=s.gear;
            // An already engaged gear can also prove unsuitable after cruise
            // resumes, but only under the heavy-load evidence below: no upshift
            // was observed, so a later downshift alone shows no failed upshift.
            trial_gear=s.gear;trial_time=now;trial_speed=s.speed;
            trial_throttle=peak_throttle=s.throttle;trial_observed=false;
        }
        last_time=now;last_target=s.target;
        if(s.throttle>=0.75f){heavy_time=now;heavy_seen=true;}
        if(trial_gear)peak_throttle=std::max(peak_throttle,s.throttle);
        if(s.gear>0) {
            if(last_gear>0 && s.gear>last_gear) {
                // Preserve evidence over a chain of upshifts: overwriting the
                // entry speed at each step hid the loss that led to a reversal.
                // The chain starts at its first observed upshift, not at the
                // speed/throttle when learning restarted (e.g. right after braking).
                if(!trial_gear || !trial_observed) {trial_speed=s.speed;trial_throttle=peak_throttle=s.throttle;}
                else trial_speed=std::max(trial_speed,s.speed);
                trial_gear=s.gear;trial_time=now;trial_observed=true;
            } else if(trial_gear>1 && s.gear<trial_gear) {
                bool heavy_failure=trial_speed-s.speed>=0.6f && heavy_seen && now-heavy_time<=1200;
                // The game's own downshift confirms demand after an observed
                // upshift. Re-acceleration after braking or a cruise resume also
                // raises throttle and downshifts, without any upshift to blame.
                // No fixed speed, RPM or gas percentage is needed here.
                bool native_reversal=trial_observed && s.speed<trial_speed && s.speed<s.target &&
                    peak_throttle>trial_throttle+noise;
                bool failed=now-trial_time<=12000 && (heavy_failure || native_reversal);
                if(failed) {
                    int learned=trial_gear-1;
                    cap=cap?std::min(cap,learned):learned;
                    cap_time=now;relaxing=false;reason=native_reversal?"native_load_reversal":"failed_upshift";
                    evidence={trial_gear,trial_speed,trial_throttle,peak_throttle,now-trial_time,trial_observed};
                }
                trial_gear=0;trial_observed=false;
            }
            last_gear=s.gear;
        }
        if(trial_gear && now-trial_time>12000){trial_gear=0;trial_observed=false;}
        if(cap) {
            // Release on eased load: moderate throttle while speed is not falling.
            // ACC may hold speed below the displayed target without braking, so
            // reaching that target cannot be required.
            bool relaxed=now-cap_time>=8000 && s.throttle<=0.60f && s.gear>0 &&
                (!relaxing || s.speed>=relaxed_speed-0.6f);
            if(!relaxed)relaxing=false;
            else if(!relaxing){relaxing=true;relaxed_time=now;relaxed_speed=s.speed;}
            else if(now-relaxed_time>=3000) {
                cap=0;trial_gear=0;trial_observed=false;relaxing=false;reason="load_relaxed";
            }
        }
    }
};
inline int guard_choice(int stock,int cap,bool scoped,bool kickdown,int current=0) {
    if(!scoped || kickdown || cap<=0 || stock<=cap)return stock;
    // Never create a downshift or deepen a stock downshift to enforce a ceiling.
    if(current>0) {if(stock<=current)return stock;return std::max(current,cap);}
    return cap;
}
}
