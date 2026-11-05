#include "kickdown.h"
#include "keyboard_binding.h"
#include "cruise_guard.h"
#include "cruise_acceleration.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
int main(int argc,char **argv) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    using namespace roadtrip;
    for(const char *id : {"ford.f150_23","ford.bronco_24","ford.victoria_06","ford.mustang_67"}) assert(ford(id));
    for(const char *id : {"vehicle.ford.f150_23","vehicle.ford.bronco_24","vehicle.ford.victoria_06","vehicle.ford.mustang_67"}) assert(ford(id));
    assert(!ford("kenworth.t680")); assert(!ford("ford.modcar")); assert(!ford(nullptr));
    for(const char *id : {"vehicle.kenworth.t680","vehicle.ford.modcar","vehicle.vehicle.ford.f150_23","vehicle.ford.f150_23.extra","prefix.ford.f150_23","vehicle.",""}) assert(!ford(id));
    Kickdown k;
    assert(!k.update(100,true,true,300));
    assert(!k.update(500,true,true,300)); // hold is not a double tap
    assert(!k.update(501,false,true,300));
    assert(!k.update(510,true,true,300));
    assert(!k.update(550,false,true,300));
    assert(k.update(610,true,true,300));
    assert(k.update(2000,true,true,300)); // kickdown lasts while held
    assert(!k.update(2001,false,true,300));
    assert(!k.update(2010,true,true,300)); // first press after kickdown release is ordinary
    assert(!k.update(2020,false,true,300));
    assert(k.update(2050,true,true,300));
    assert(!k.update(2060,true,false,300)); // pause/focus loss cancels
    assert(!k.update(2070,true,true,300)); // held during resume is blocked until release
    assert(!k.update(2080,false,true,300));
    assert(!k.update(2090,true,true,300));
    assert(!k.update(2100,false,true,300));
    assert(k.update(2150,true,true,300));
    k.cancel();
    assert(!k.update(3000,true,true,300));
    assert(!k.update(3010,false,true,300));
    assert(!k.update(3301,true,true,300)); // outside double-tap window
    auto binding=parse_keyboard_binding("mix dforward `keyboard.uarrow?0 | keyboard.a?0 | joy2.b5?0`\n");
    assert(binding.valid && binding.scans==std::vector<unsigned>({0xe048,0x1e}));
    assert(parse_keyboard_binding("mix dforward `keyboard.w?0`\r\n").valid);
    assert(!parse_keyboard_binding("mix dforward `joy.b1?0`\n").valid);
    assert(!parse_keyboard_binding("mix dforward `unbound`\n").valid);
    assert(!parse_keyboard_binding("mix dforward `keyboard.a?0 & keyboard.lshift?0`\n").valid);
    assert(!parse_keyboard_binding("mix dforward `keyboard.a?0 |`\n").valid);
    assert(!parse_keyboard_binding("mix dforward `keyboard.unknown?0`\n").valid);
    assert(!parse_keyboard_binding("mix dforward `keyboard.numenter?0`\n").valid);
    assert(!parse_keyboard_binding("mix dforward `keyboard.w?0`\nmix dforward `keyboard.a?0`\n").valid);
    assert(!parse_keyboard_binding("mix dforward missing_backticks\n").valid);
    assert(!parse_keyboard_binding("mix photofwd `keyboard.w?0`\n").valid);
    auto profile=last_profile_controls("/home/steam_profiles/AB12/ffb_lut.sii\n/home/profiles/34CD/controls.sii\n");
    assert(profile.id=="34CD" && !profile.steam);
    profile=last_profile_controls("/home/profiles/AB12/controls.sii\n/steam/profiles/34CD/quick_replies.sii\n");
    assert(profile.id=="34CD" && profile.steam);
    assert(last_profile_controls("/home/profiles/../../controls.sii").id.empty());
    assert(last_profile_controls("/home/profiles(backup)/AB12/controls.sii").id.empty());
    // game.log.txt keeps growing while driving; the active profile must survive it.
    auto follow=[](ProfileLog &l,const std::string &file,size_t chunk) {
        l.begin(file.substr(0,64),file.size());
        for(size_t at=l.offset;at<file.size();at+=chunk)l.feed(file.data()+at,std::min(chunk,file.size()-at));
    };
    std::string game="00:00:00.000 : log created on : Wednesday November 05 2026 @ 17:12:00\n";
    game+="/home/profiles/AB12/controls.sii\n";
    ProfileLog profile_log;follow(profile_log,game,5); // reads split inside the path
    assert(profile_log.profile.id=="AB12" && !profile_log.profile.steam);
    while(game.size()<1024*1024)game+="[road_trip_shift] GEAR_CHOICE source=game_logic caller=direct stock_candidate=9 plugin_return=9\n";
    assert(last_profile_controls(game.substr(game.size()-256*1024)).id.empty()); // the old tail window loses it
    follow(profile_log,game,256*1024);assert(profile_log.profile.id=="AB12");
    game+="/home/steam_profiles/CD34/controls.sii\n";follow(profile_log,game,7);
    assert(profile_log.profile.id=="CD34" && profile_log.profile.steam); // real profile switch
    game+="/home/profiles/EF";follow(profile_log,game,3);assert(profile_log.profile.id=="CD34");
    game+="56/controls.sii\n";follow(profile_log,game,3);assert(profile_log.profile.id=="EF56"); // completed later
    follow(profile_log,"00:00:01.000 : new log\n",64);assert(profile_log.profile.id.empty()); // shorter: new log
    follow(profile_log,game,4096);assert(profile_log.profile.id=="EF56");
    std::string rewritten="00:00:00.000 : log created on : Thursday November 06 2026 @ 09:00:00\n";
    while(rewritten.size()<game.size()+10)rewritten+="no profile yet\n";
    follow(profile_log,rewritten,4096);assert(profile_log.profile.id.empty()); // same name, different log
    std::puts("PASS: remapped keyboard bindings, controller alternatives, unsupported/ambiguous bindings, profile selection and traversal refusal.");
    std::puts("PASS: active profile survives a growing game log, split reads, profile switch and log restart.");
    for(float m : {0.0f,10.0f,3.0f,high_power,7.5f}) {
        assert(mode(m,true,false,high_power)==m);
        assert(mode(m,true,true,high_power)==high_power);
        assert(mode(m,false,true,high_power)==m);
        assert(mode(m,true,false,high_power)==m); // restoration is immediate, with no saved stale setting
    }
    for(float target:{15.0f,30.0f,45.0f}) {
        CruiseAcceleration acceleration;
        acceleration.update(true,true,false,target,target,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+5,target,0,0);assert(acceleration.active);
        assert(mode(economy_mode,true,false,high_power,acceleration.active)==normal_mode);
        acceleration.update(true,true,false,target+5,target+2,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+5,target+5,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+5,target+3,0,0);assert(acceleration.active); // recover a later speed deficit without any target change
        acceleration.update(true,true,false,target+10,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+10,target+5,0.1f,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+15,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+15,target+5,0,0.1f);assert(!acceleration.active);
        acceleration.update(true,true,false,target+20,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,true,target+20,target+5,1,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+25,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+24,target+5,0,0);assert(acceleration.active); // reduced target is still above actual speed
        acceleration.update(false,true,false,target+30,target+5,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+30,target+5,0,0);assert(acceleration.active); // recover after resume with fresh valid state
        acceleration.update(true,false,false,target+35,target+5,0,0);assert(!acceleration.active);
        acceleration.reset();
        acceleration.update(true,true,false,target,target-1,0,0);assert(acceleration.active); // cruise already on below target
        acceleration.update(true,true,false,target,target,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target,target+1,0,0);assert(!acceleration.active);
        // Repeated crossings and small losses must not re-enter Normal.
        for(int i=0;i<100;++i) {
            acceleration.update(true,true,false,target,target-0.0001f,0,0);assert(!acceleration.active);
            acceleration.update(true,true,false,target,target+0.0001f,0,0);assert(!acceleration.active);
        }
        const float boundary=target-CruiseAcceleration::recovery_gap_mps;
        acceleration.update(true,true,false,target,boundary,0,0);assert(!acceleration.active);
        assert(!std::strcmp(acceleration.reason,"within_recovery_band"));
        acceleration.update(true,true,false,target,std::nextafter(boundary,-INFINITY),0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target,target-0.01f,0,0);assert(acceleration.active); // hold through the band
        acceleration.update(true,true,false,target,target,0,0);assert(!acceleration.active);
        // Target changes use the same band; braking/manual/kickdown still cancel immediately.
        acceleration.update(true,true,false,target+0.2f,target,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+2,target,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+0.2f,target,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+0.2f,target,0,0.1f);assert(!acceleration.active);
        acceleration.update(true,true,false,target+0.2f,target,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+2,target,0,0);assert(acceleration.active);
        acceleration.update(true,true,true,target+2,target,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+2,target,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+2,target,0.1f,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+2,target,0,0);assert(acceleration.active);
        acceleration.update(false,true,false,target+2,target,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+2,target,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target,target-1,0.2f,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target,target-1,0,0.2f);assert(!acceleration.active);
        acceleration.update(true,true,false,target-2,target-1,0,0);assert(!acceleration.active); // lower target is now satisfied
        assert(mode(normal_mode,true,false,high_power,true)==normal_mode);
        assert(mode(high_power,true,false,high_power,true)==high_power);
        assert(mode(economy_mode,true,true,high_power,true)==high_power);
        assert(mode(economy_mode,false,false,high_power,true)==economy_mode);
    }
    std::puts("PASS: Economy recovery enters beyond 3 km/h deficit, holds through the band, restores at target; boundary chatter, ACC/manual/kickdown cancellation and target changes.");
    // A user-selected High Power setting is not evidence of plugin kickdown.
    assert(!std::strcmp(mode_source(true,false),"configured"));
    assert(!mode_override(high_power,true,false,high_power));
    assert(!std::strcmp(mode_source(true,true),"kickdown"));
    assert(!mode_override(high_power,true,true,high_power)); // request exists, value was already High Power
    assert(mode_override(10,true,true,high_power));
    assert(!mode_override(10,false,true,high_power));
    assert(!std::strcmp(mode_name(high_power),"high_power"));
    assert(!std::strcmp(mode_name(10),"economy"));
    assert(!std::strcmp(mode_name(3),"normal"));
    assert(!std::strcmp(mode_name(0),"off"));
    CruiseSample s;s.eligible=true;s.gear=9;s.speed=s.target=31;s.throttle=0.3f;
    CruiseGuard guard;
    auto step=[&](uint64_t t,int g,float v,float gas){s.gear=g;s.speed=v;s.throttle=gas;guard.update(t,s);};
    step(100,9,31,0.3f);step(300,10,31,0.3f);
    step(500,10,30.5f,0.9f);step(700,0,30.3f,0.4f);step(900,9,30,0.3f);
    assert(guard.cap==9); // neutral during a real shift does not erase the trial
    assert(guard_choice(10,guard.cap,true,false)==9);
    assert(guard_choice(7,guard.cap,true,false)==7); // downshifts stay available
    assert(guard_choice(0,guard.cap,true,false)==0);
    assert(guard_choice(-1,guard.cap,true,false)==-1);
    assert(guard_choice(10,guard.cap,true,true)==10); // explicit kickdown is unaffected
    assert(guard_choice(10,guard.cap,false,false)==10);
    assert(guard_choice(9,8,true,false,10)==9);
    assert(guard_choice(10,8,true,false,10)==10);
    assert(guard_choice(11,8,true,false,10)==10);
    assert(guard_choice(10,8,true,false,6)==8);
    for(uint64_t t=1100;t<=11900;t+=200)step(t,9,31,0.4f);
    assert(guard.cap==0); // after minimum hold plus 3 seconds at target with lower load
    for(float target:{12.0f,25.0f,40.0f}) {
        s.target=target;s.pedal=s.brake=0;s.kickdown=false;guard.reset();
        step(100,4,target,0.3f);step(300,5,target,0.3f);
        step(500,5,target-0.8f,1);step(700,4,target-1,0.3f);assert(guard.cap==4);
        s.pedal=0.5f;step(900,4,target,0.3f);assert(guard.cap==0);s.pedal=0;
    }
    s.target=31;guard.reset();step(100,9,31,0.2f);step(300,10,31,0.2f);step(500,9,30,0.2f);
    assert(guard.cap==0); // slow traffic/coasting, without high load, is not a failed power upshift
    guard.reset();step(100,10,31,1);s.brake=0.2f;step(300,9,29,0);assert(guard.cap==0);s.brake=0;
    guard.reset();step(100,10,31,1);s.kickdown=true;step(300,9,29,1);assert(guard.cap==0);s.kickdown=false;
    guard.reset();step(100,10,31,1);s.target=28;step(300,9,29,1);assert(guard.cap==0);
    // Observed Bronco chain: the stock chooser asks for 10, steps through
    // 7/8/9, then requests 6 below the target with increased cruise demand.
    // Demand stays below the old fixed heavy-throttle gate.
    s.target=46.94f;guard.reset();step(100,6,46.94f,0.26f);
    step(200,7,46.94f,0.19f);step(400,8,46.84f,0.19f);step(600,9,46.67f,0.19f);
    step(900,9,46.36f,0.62f);step(1000,6,46.36f,0.29f);
    assert(guard.cap==8);assert(!std::strcmp(guard.reason,"native_load_reversal"));
    assert(guard_choice(10,guard.cap,true,false)==8);assert(guard_choice(5,guard.cap,true,false)==5);
    assert(guard_choice(10,guard.cap,true,true)==10);
    // A new driver target or braking must discard this learning immediately.
    s.target=50;step(1100,6,46.4f,0.7f);assert(guard.cap==0);
    // Throttle noise is not increased demand: no ceiling from an ordinary downshift.
    s.target=31;guard.reset();step(100,9,31,0.20f);step(300,10,30.9f,0.21f);step(500,10,30.8f,0.24f);step(700,9,30.7f,0.22f);
    assert(guard.cap==0);
    // ACC can hold speed below the displayed target without braking. Release on
    // eased load, not on reaching that target, or the ceiling never ends.
    auto learn=[&]{guard.reset();step(100,9,31,0.3f);step(300,10,31,0.3f);step(500,10,30.5f,0.9f);step(900,9,30,0.3f);assert(guard.cap==9);};
    learn();for(uint64_t t=1100;t<=12900;t+=200)step(t,9,28,0.4f);
    assert(guard.cap==0 && !std::strcmp(guard.reason,"load_relaxed"));
    learn();for(uint64_t t=1100;t<=12900;t+=200)step(t,9,33,0); // downhill overspeed, no load
    assert(guard.cap==0);
    learn();float falling=30;for(uint64_t t=1100;t<=20900;t+=200){falling-=0.06f;step(t,9,falling,0.5f);}
    assert(guard.cap==9); // speed keeps dropping: load has not eased
    learn();for(uint64_t t=1100;t<=8800;t+=200)step(t,9,28,0.4f);
    assert(guard.cap==9); // minimum hold still applies
    std::puts("PASS: cruise failed-upshift hold, release hysteresis, speed independence, braking/manual/kickdown bypass.");
    std::puts("PASS: vehicle scope, single/double/held/released input, cancellation, timeout, mode restoration.");
    if(argc==2) {
        // Optional offline detector replay, not a simulation of changed vehicle physics.
        std::ifstream f(argv[1]);assert(f);std::string line;
        auto split=[](const std::string &l){std::vector<std::string> v;std::stringstream ss(l);std::string x;while(std::getline(ss,x,','))v.push_back(x);return v;};
        std::getline(f,line);auto fields=split(line);std::unordered_map<std::string,size_t> col;
        for(size_t i=0;i<fields.size();++i)col[fields[i]]=i;
        CruiseGuard replay;float previous_mode=-1;unsigned holds=0;
        while(std::getline(f,line)) {
            auto values=split(line);if(values.size()<fields.size())continue;
            auto n=[&](const char *key){return std::stod(values[col.at(key)]);};
            CruiseSample sample;sample.eligible=n("scope")!=0 && n("paused")==0;
            sample.kickdown=n("kickdown")!=0;sample.pedal=(float)n("input_throttle");
            sample.speed=(float)n("speed_kmh")/3.6f;sample.target=(float)n("cruise_kmh")/3.6f;
            sample.throttle=(float)n("effective_throttle");sample.gear=(int)n("gear");
            if(col.count("effective_brake"))sample.brake=(float)n("effective_brake");
            float selected=(float)n("mode_config");if(selected!=previous_mode){replay.reset("configured_mode_changed");previous_mode=selected;}
            int old=replay.cap;replay.update((uint64_t)n("tick_ms"),sample);
            if(old!=replay.cap) {std::printf("REPLAY t=%.0f cap=%d reason=%s speed=%.2f target=%.2f\n",n("tick_ms"),replay.cap,replay.reason,sample.speed*3.6f,sample.target*3.6f);if(replay.cap)++holds;}
        }
        std::printf("REPLAY: %u hold activations; braking unavailable in older traces must be checked in a new drive.\n",holds);
    }
}
