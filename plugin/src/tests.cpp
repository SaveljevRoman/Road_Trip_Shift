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
    AcceleratorInput action;
    auto key=[&](uint64_t t,bool held,bool allowed=true){return action.update(t,held?1.0f:0.0f,held,allowed,300);};
    assert(!key(4000,true)); // startup with gas held
    assert(!key(4010,false));assert(!key(4020,true));assert(!key(4030,false));assert(key(4040,true));
    for(int i=0;i<3;i++)action.cancel(); // repeated pause/resume/timer cancellation
    assert(!key(4050,true));assert(!key(4060,false));assert(!key(4070,true));assert(!key(4080,false));assert(key(4090,true));
    assert(!key(4100,true,false));assert(!key(4110,true));assert(!key(4120,false));assert(!key(4130,true));
    assert(!action.update(4140,NAN,true,true,300));assert(!key(4150,true));
    assert(!action.update(4160,-0.1f,true,true,300));assert(!action.update(4170,1.1f,true,true,300));
    assert(!key(4180,false));assert(!key(4200,true));assert(!key(4210,false));assert(!key(4501,true));
    action.cancel();
    for(uint64_t t=5000;t<5400;t+=20)assert(!action.update(t,(t/20)%2?1.0f:0.0f,false,true,300)); // gamepad only
    assert(!action.update(5400,1,true,true,300));assert(!action.update(5410,1,false,true,300));
    assert(action.update(5420,1,true,true,300)); // keyboard presses still work while a controller holds gas
    assert(!action.update(5430,1,false,true,300)); // key release restores mode even with controller gas held
    assert(!action.update(5440,0,true,true,300)); // wait for Smart Throttle, do not invent gas
    assert(!action.update(5800,0,true,true,300)); // no confirmation: block until release
    assert(!action.update(5810,1,true,true,300));assert(!key(5820,false));assert(!key(5830,true));
    // Real Smart Throttle starts below the old 15% threshold. Both strokes
    // must count at their physical edges even while the SDK value ramps.
    action.cancel();assert(!key(6000,false));
    assert(!action.update(6010,0,true,true,300));assert(!action.taps.blocked);
    assert(!action.update(6020,0.008f,true,true,300));
    assert(!action.update(6040,0.025f,true,true,300));
    assert(!action.update(6080,0.10f,false,true,300));
    assert(!action.update(6150,0,true,true,300)); // second stroke awaits confirmation
    assert(action.update(6170,0.025f,true,true,300));
    assert(action.update(6300,0.5f,true,true,300));
    assert(!action.update(6310,0.4f,false,true,300)); // restore at the physical release
    // An unconfirmed first stroke cannot be combined with a later valid press.
    action.cancel();assert(!key(6400,false));
    assert(!action.update(6410,0,true,true,300));assert(!action.update(6430,0,false,true,300));
    assert(!action.update(6450,0.5f,true,true,300));
    // One held ramp is still one press; falling SDK input cannot leave kickdown on.
    assert(!action.update(6500,0.8f,true,true,300));assert(!key(6510,false));assert(key(6520,true));
    assert(!action.update(6530,0,true,true,300));assert(!key(6540,true));
    assert(!key(6550,false));assert(!key(6560,true));
    action.cancel();assert(!action.update(7000,0,false,true,600));
    assert(!action.update(7010,0,true,true,600));
    assert(!action.update(7410,0.000001f,true,true,600)); // positive input, no fixed percentage or 250ms timeout
    assert(!action.update(7420,0,false,true,600));
    assert(action.update(7450,0.000001f,true,true,600));
    assert(!action.update(7460,0,false,true,600));
    action.cancel();assert(!key(8000,false));assert(!key(8010,true));
    assert(!key(8030,false));assert(!key(8050,false));assert(!key(8070,false));
    assert(key(8100,true));assert(key(8200,true));assert(!key(8210,false));
    assert(!key(8230,false));assert(!key(8250,false));assert(!key(8270,true));
    action.cancel();assert(!key(8300,false));assert(!action.update(8310,0,true,true,300));
    assert(!key(8320,false));assert(!key(8340,false));assert(!key(8360,true));
    std::puts("PASS: confirmed first tap survives multiple released frames; unconfirmed taps and post-kickdown releases cannot rearm.");
    std::puts("PASS: Smart Throttle ramp/confirmation, physical double-tap timing, unconfirmed tap rejection and confirmation timeout.");
    std::puts("PASS: keyboard-only accelerator, controller exclusion, key release, repeated cancellation, invalid input and timeout.");
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
    std::puts("PASS: remapped keyboard bindings, controller alternatives, unsupported/ambiguous bindings, profile selection and traversal refusal.");
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
        acceleration.update(true,true,false,target+5,target+3,0,0);assert(!acceleration.active); // later load is not a new target request
        acceleration.update(true,true,false,target+10,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+10,target+5,0.1f,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+15,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+15,target+5,0,0.1f);assert(!acceleration.active);
        acceleration.update(true,true,false,target+20,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,true,target+20,target+5,1,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+25,target+5,0,0);assert(acceleration.active);
        acceleration.update(true,true,false,target+24,target+5,0,0);assert(!acceleration.active);
        acceleration.update(false,true,false,target+30,target+5,0,0);assert(!acceleration.active);
        acceleration.update(true,true,false,target+30,target+5,0,0);assert(!acceleration.active); // resume held target is not a new increase
        acceleration.update(true,false,false,target+35,target+5,0,0);assert(!acceleration.active);
        assert(mode(normal_mode,true,false,high_power,true)==normal_mode);
        assert(mode(high_power,true,false,high_power,true)==high_power);
        assert(mode(economy_mode,true,true,high_power,true)==high_power);
        assert(mode(economy_mode,false,false,high_power,true)==economy_mode);
    }
    std::puts("PASS: Economy cruise target increase borrows Normal, restores at target, cancels on driver/target/scope changes and preserves High Power.");
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
