// Road Trip Shift, experimental ATS plugin.
// The call-site trampoline approach was informed by Deppelover's MIT project
// car_automatic_transmission. This narrower implementation retains stock maps.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <algorithm>
#include <atomic>
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_common_configs.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "kickdown.h"
#include "keyboard_binding.h"
#include "binding_worker.h"
#include "cruise_guard.h"
#include "cruise_acceleration.h"
#include "rolling_log.h"
#include "native_scan.h"
#include "hook_memory.h"
#include "mode_notice.h"
#include "native_hud.h"
#include "performance.h"

static const char *VERSION="0.3.2 experimental";
static roadtrip::Performance performance;
static unsigned timing_sample_ms=10000;
static std::atomic<unsigned> timing_hook_every{64};
static ULONGLONG last_timing_sample=0;
static HMODULE module_handle;
static uintptr_t base;
static scs_log_t game_log;
static roadtrip::RollingLog disk_log;
static bool logging_enabled=true;
static unsigned log_quota_mb=50;
static wchar_t dir[MAX_PATH],status_path[MAX_PATH];
static char vehicle[128]="unknown";
static bool paused=true,known_ford=false,patched=false,enabled=true;
static std::atomic<unsigned char> scope{0};
static std::atomic<unsigned char> kick{0};
static std::atomic<unsigned char> cruise_normal{0};
static roadtrip::CruiseAcceleration cruise_acceleration;
static bool cruise_normal_enabled=true;
static std::atomic<unsigned long long> reader_calls{0},shadow_reads{0};
// Capture decisions without doing file I/O or reading SDK state in native hooks.
static std::atomic<unsigned long long> chooser_calls[2]{};
static std::atomic<uint64_t> chooser_decision[2]{};
static unsigned long long reported_choices[2]={};
static uint64_t reported_decision[2]={};
static ULONGLONG last_choice_log[2]={};
static std::atomic<int> cruise_cap{0};
static std::atomic<int> guard_current_gear{0};
static std::atomic<unsigned long long> cruise_clamps{0};
static roadtrip::CruiseGuard cruise_guard;
static bool cruise_guard_enabled=true;
static_assert(sizeof(scope)==1,"Native condition stub requires a one-byte atomic flag");
// Keep main's tap detector, analog thresholds and cancellation policy.
static roadtrip::Kickdown keyboard,analog_pedal;
static bool analog_enabled=true,analog_down=false;
static ULONGLONG last_keyboard_seen=0;
static bool keyboard_seen=false;
static ULONGLONG last_cruise_transition=0;
static unsigned long long cruise_transition_count=0;
static bool pedal_sample=false,observed_input=false,raw_keyboard_held=false;
static const char *input_gate="inactive",*reported_gate="";
static int reported_gear=0;
static unsigned double_ms=300,sample_ms=200,memory_sample_ms=10000;
static float pedal=0,effective=0,rpm=0,speed=0,cruise=0,brake=0,effective_brake=0,power_mode=roadtrip::high_power;
static int gear=0;
struct EngineSignal {const char *name,*field;scs_value_type_t type;};
static const EngineSignal engine_signals[]={
    {SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled,"engine_enabled",SCS_VALUE_TYPE_bool},
    {SCS_TELEMETRY_TRUCK_CHANNEL_motor_brake,"motor_brake",SCS_VALUE_TYPE_bool},
    {SCS_TELEMETRY_TRUCK_CHANNEL_parking_brake,"parking_brake",SCS_VALUE_TYPE_bool},
    {SCS_TELEMETRY_TRUCK_CHANNEL_electric_enabled,"electric_enabled",SCS_VALUE_TYPE_bool},
    {SCS_TELEMETRY_TRUCK_CHANNEL_retarder_level,"retarder_level",SCS_VALUE_TYPE_u32},
    {SCS_TELEMETRY_TRUCK_CHANNEL_input_clutch,"input_clutch",SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch,"effective_clutch",SCS_VALUE_TYPE_float},
    {SCS_TELEMETRY_TRUCK_CHANNEL_wear_engine,"engine_wear",SCS_VALUE_TYPE_float},
};
static double engine_values[8]={};
static unsigned engine_valid=0,engine_registered=0;
static double engine_value(unsigned i){return engine_valid&(1u<<i)?engine_values[i]:-1;}
static ULONGLONG last_sample=0;
static unsigned sample_count=0;
static bool foreground=false;
static roadtrip::KeyboardBinding keyboard_binding;
// Game-thread copy of the discovery inputs. The worker gets its own copy at
// start; this one is read directly only if the worker cannot start.
static roadtrip::BindingSource binding_source;
static roadtrip::BindingWorker binding_worker;
static bool reload_bindings=true,binding_reported=false;
static ULONGLONG last_binding_check=0;
using Reader=float(*)(void*);
using Chooser=int(*)(void*);
static Reader original_reader;
static Chooser original_chooser;
static void *adaptive_var;
static float base_mode=0;
static bool mode_seen=false;
static roadtrip::NativeHud native_hud;
static roadtrip::ModeNotice mode_notice;
static bool hud_enabled=true,hud_russian=false;
static unsigned hud_duration_ms=2500;
static uintptr_t hud_submitted=0;
static const char *kick_origin="none";

// Frequent diagnostics stay in the plugin's bounded log: echoing them grew
// game.log.txt by about 1 KB/s. Native tests observe everything via the SDK sink.
static bool sdk_log_all=false;
static void write_event(bool sdk,const char *fmt,va_list ap) {
    if(!logging_enabled)return;
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::LogIO);
    char msg[1024];vsnprintf(msg,sizeof msg,fmt,ap);
    char line[1200];snprintf(line,sizeof line,"%llu %s qpc_ns=%llu\n",(unsigned long long)GetTickCount64(),msg,(unsigned long long)performance.elapsed_ns());disk_log.event(line);
    if(game_log && (sdk || sdk_log_all)) {char line[1100];snprintf(line,sizeof line,"[road_trip_shift] %s",msg);game_log(SCS_LOG_TYPE_message,line);}
}
// Lifecycle and configuration: plugin log and game.log.txt.
static void log(const char *fmt,...) {va_list ap;va_start(ap,fmt);write_event(true,fmt,ap);va_end(ap);}
// Driving diagnostics: plugin log only.
static void trace(const char *fmt,...) {va_list ap;va_start(ap,fmt);write_event(false,fmt,ap);va_end(ap);}
static void sample_performance(ULONGLONG now,bool force=false) {
    if(!logging_enabled || !performance.enabled.load(std::memory_order_relaxed) ||
       (!force && now-last_timing_sample<timing_sample_ms))return;
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::Report);
    last_timing_sample=now;
    for(unsigned i=0;i<(unsigned)roadtrip::PerfKind::Count;++i) {
        auto s=performance.stats[i].snapshot();if(!s.count)continue;
        trace("PERF source=qpc section=%s unit=ns scope=process_lifetime samples=%llu total_ns=%llu avg_ns=%.1f max_ns=%llu over_100us=%llu over_1ms=%llu over_5ms=%llu hook_every=%u paused=%d",
            roadtrip::perf_names[i],(unsigned long long)s.count,(unsigned long long)performance.ns(s.ticks),
            (double)((long double)s.ticks*1000000000.0L/(long double)performance.frequency/(long double)s.count),
            (unsigned long long)performance.ns(s.max_ticks),(unsigned long long)s.over_100us,(unsigned long long)s.over_1ms,
            (unsigned long long)s.over_5ms,timing_hook_every.load(),paused);
    }
}
static void engine_signal(unsigned i,const scs_value_t *v) {
    if(i>=8)return;
    auto &spec=engine_signals[i];bool valid=v && v->type==spec.type;double value=0;
    if(valid) {
        if(spec.type==SCS_VALUE_TYPE_bool)value=v->value_bool.value?1:0;
        else if(spec.type==SCS_VALUE_TYPE_u32)value=v->value_u32.value;
        else {value=v->value_float.value;valid=std::isfinite(value) && value>=0 && value<=1;}
    }
    bool had=(engine_valid&(1u<<i))!=0;
    bool changed=had!=valid || (valid && (i<5?engine_values[i]!=value:(engine_values[i]==0)!=(value==0)));
    if(valid){engine_values[i]=value;engine_valid|=1u<<i;}else engine_valid&=~(1u<<i);
    if(changed)trace("ENGINE_STATE source=game_telemetry signal=%s available=%d value=%.5f valid_mask=%u engine_enabled=%.0f motor_brake=%.0f parking_brake=%.0f electric_enabled=%.0f retarder_level=%.0f input_clutch=%.4f effective_clutch=%.4f engine_wear=%.5f speed_kmh=%.2f rpm=%.1f gear=%d mode_config=%.5f kickdown=%u cruise_kmh=%.2f input=%.4f effective=%.4f input_brake=%.4f effective_brake=%.4f",
        spec.field,valid,valid?value:-1,engine_valid,engine_value(0),engine_value(1),engine_value(2),engine_value(3),engine_value(4),engine_value(5),engine_value(6),engine_value(7),
        speed*3.6f,rpm,gear,base_mode,(unsigned)kick.load(),cruise*3.6f,pedal,effective,brake,effective_brake);
}
static std::string read_input_file(const std::wstring &path) {
    FILE *file=_wfopen(path.c_str(),L"rb");if(!file)return {};
    constexpr long limit=256*1024;
    if(fseek(file,0,SEEK_END)){fclose(file);return {};}
    long size=ftell(file);if(size<0 || size>limit || fseek(file,0,SEEK_SET)){fclose(file);return {};}
    std::string text(size,'\0');size_t read=fread(text.data(),1,text.size(),file);fclose(file);text.resize(read);return text;
}
// Reads only bytes appended since the previous check; an unreadable log keeps the known profile.
static void follow_game_log(roadtrip::BindingSource &source) {
    FILE *file=_wfopen((source.game_home+L"\\game.log.txt").c_str(),L"rb");if(!file)return;
    std::string chunk(64*1024,'\0');size_t n=fread(chunk.data(),1,64,file);
    long long size=_fseeki64(file,0,SEEK_END)?-1:_ftelli64(file);
    auto &profile_log=source.profile_log;
    if(size>=0) {
        profile_log.begin(chunk.substr(0,n),(uint64_t)size);
        if(!_fseeki64(file,(long long)profile_log.offset,SEEK_SET))
            while(profile_log.offset<(uint64_t)size && (n=fread(chunk.data(),1,chunk.size(),file))>0)profile_log.feed(chunk.data(),n);
    }
    fclose(file);
}
static void init_input_paths(const wchar_t *ini) {
    wchar_t value[MAX_PATH]={};GetPrivateProfileStringW(L"input",L"controls_file",L"",value,MAX_PATH,ini);
    binding_source={};binding_source.controls_override=value;
    auto &game_home=binding_source.game_home;
    wchar_t documents[MAX_PATH]={};
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents)))
        game_home=std::wstring(documents)+L"\\American Truck Simulator";
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv) {
        for(int i=1;i+1<argc;++i)if(!wcscmp(argv[i],L"-homedir")) {
            // The game keeps its data in "American Truck Simulator" inside -homedir, as under Documents.
            wchar_t full[MAX_PATH]={};DWORD n=GetFullPathNameW(argv[i+1],MAX_PATH,full,nullptr);
            game_home=n && n<MAX_PATH?full:L"";
            while(!game_home.empty() && (game_home.back()==L'\\' || game_home.back()==L'/'))game_home.pop_back();
            if(!game_home.empty())game_home+=L"\\American Truck Simulator";
            break;
        }
        LocalFree(argv);
    }
    keyboard_binding={};reload_bindings=true;binding_reported=false;last_binding_check=0;
}
// Worker thread (or the synchronous fallback): file I/O and parsing only.
// No logging and no plugin state: the game thread applies and reports results.
static roadtrip::BindingLoad load_keyboard_binding(roadtrip::BindingSource &source) {
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::Bindings);
    std::wstring path;
    if(!source.controls_override.empty()) {
        bool absolute=source.controls_override.size()>1 && source.controls_override[1]==L':';
        path=absolute?source.controls_override:source.game_home+L"\\"+source.controls_override;
    } else if(!source.game_home.empty()) {
        follow_game_log(source);
        const auto &profile=source.profile_log.profile;
        if(!profile.id.empty())path=source.game_home+(profile.steam?L"\\steam_profiles\\":L"\\profiles\\")+
            std::wstring(profile.id.begin(),profile.id.end())+L"\\controls.sii";
    }
    auto text=read_input_file(path);
    roadtrip::BindingLoad result;result.binding=roadtrip::parse_keyboard_binding(text);result.file_read=!text.empty();
    return result;
}
static bool apply_keyboard_binding(const roadtrip::BindingLoad &load) {
    const auto &next=load.binding;
    bool changed=next.valid!=keyboard_binding.valid || next.scans!=keyboard_binding.scans;
    keyboard_binding=next;
    if(changed || !binding_reported) {
        std::string scans;
        for(unsigned scan:next.scans){char code[12];snprintf(code,sizeof code,"%s%04X",scans.empty()?"":",",scan);scans+=code;}
        log("KEYBOARD_BINDING source=controls_sii ready=%d keys=%u scans=%s file_read=%d",
            next.valid,(unsigned)next.scans.size(),scans.empty()?"none":scans.c_str(),load.file_read);binding_reported=true;
    }
    return changed;
}
// Game thread: posts a read when one is due and applies finished reads; it
// never waits for the disk. The 1 s cadence and reload triggers are unchanged.
static bool refresh_keyboard_binding(ULONGLONG now) {
    bool changed=false;
    if(reload_bindings || now-last_binding_check>=1000) {
        reload_bindings=false;last_binding_check=now;
        if(binding_worker.running())binding_worker.request();
        else changed=apply_keyboard_binding(load_keyboard_binding(binding_source));
    }
    roadtrip::BindingLoad load;
    if(binding_worker.take(load))changed=apply_keyboard_binding(load) || changed;
    return changed;
}
static void stop_binding_worker() {
    if(!binding_worker.running())return;
    bool exited=binding_worker.stop(2000);
    log("KEYBOARD_BINDING_WORKER STOP exited=%d",exited);
}
static void start_binding_worker() {
    stop_binding_worker();
    bool started=binding_worker.start(binding_source,load_keyboard_binding);
    log("KEYBOARD_BINDING_WORKER START started=%d mode=%s",started,started?"background":"synchronous_fallback");
}
static bool keyboard_accelerator_held() {
    if(!foreground || !keyboard_binding.valid)return false;
    DWORD pid=0;DWORD thread=GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if(pid!=GetCurrentProcessId())return false;
    HKL layout=GetKeyboardLayout(thread);
    for(unsigned scan:keyboard_binding.scans) {
        UINT key=MapVirtualKeyExW(scan,MAPVK_VSC_TO_VK_EX,layout);
        if(key && (GetAsyncKeyState(key)&0x8000))return true;
    }
    return false;
}
static void reset_cruise_guard(const char *reason) {
    int before=cruise_cap.exchange(0);cruise_guard.reset(reason);
    if(before)trace("CRUISE_GUARD CLEAR source=cruise_load old_cap=%d reason=%s",before,reason);
}
static void set_cruise_normal(bool next,const char *reason) {
    // A pending cruise update must never re-enable Normal during kickdown.
    next=next && kick==0;
    bool previous=cruise_normal!=0;if(previous==next)return;
    float before=roadtrip::mode(base_mode,scope!=0,kick!=0,power_mode,previous);
    cruise_normal=next;
    float after=roadtrip::mode(base_mode,scope!=0,kick!=0,power_mode,next);
    ULONGLONG now=GetTickCount64();
    trace("CRUISE_ACCEL %s source=plugin reason=%s mode_config=%.5f effective_before=%.5f effective_after=%.5f target_kmh=%.6f speed_kmh=%.6f error_kmh=%.6f transition=%llu since_transition_ms=%llu input=%.6f effective=%.6f input_brake=%.6f effective_brake=%.6f sdk_valid=%d keyboard_held=%d kickdown=%u scope=%u foreground=%d normal_enabled=%d guard_cap=%d gear=%d rpm=%.1f recovery_gap_kmh=%.3f",
        next?"ON":"OFF",reason,base_mode,before,after,cruise*3.6f,speed*3.6f,(cruise-speed)*3.6f,
        ++cruise_transition_count,(unsigned long long)(last_cruise_transition?now-last_cruise_transition:0),
        pedal,effective,brake,effective_brake,pedal_sample,raw_keyboard_held,(unsigned)kick.load(),(unsigned)scope.load(),foreground,cruise_normal_enabled,cruise_cap.load(),gear,rpm,roadtrip::CruiseAcceleration::recovery_gap_kmh);
    last_cruise_transition=now;
}
static void update_cruise_acceleration() {
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::Cruise);
    cruise_acceleration.update(cruise_normal_enabled && scope && foreground && pedal_sample && !raw_keyboard_held,
        roadtrip::economy_selected(base_mode),kick!=0,cruise,speed,pedal,std::max(brake,effective_brake));
    set_cruise_normal(cruise_acceleration.active,cruise_acceleration.reason);
}
static void refresh_config_mode() {
    if(!original_reader || !adaptive_var)return;
    float value=original_reader(adaptive_var);if(!std::isfinite(value))return;
    if(!mode_seen)log("CONFIG_MODE_INITIAL source=game_setting value=%.5f name=%s",value,roadtrip::mode_name(value));
    else if(std::fabs(value-base_mode)>0.000001f) {
        log("CONFIG_MODE_CHANGED source=game_setting old=%.5f new=%.5f name=%s kickdown_request=%u",base_mode,value,roadtrip::mode_name(value),(unsigned)kick.load());
        reset_cruise_guard("configured_mode_changed");
    }
    mode_notice.observe(value);
    base_mode=value;mode_seen=true;
}
static void clear_mode_notice(const char *reason,bool cancel_pending=true) {
    if(hud_submitted) {
        if(native_hud.manager()==hud_submitted)native_hud.expire(hud_submitted);
        trace("MODE_NOTICE EXPIRE_REQUEST source=game_hud reason=%s",reason);
    }
    hud_submitted=0;mode_notice.visible=false;
    if(cancel_pending)mode_notice.cancel();
}
static void update_mode_notice(ULONGLONG now) {
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::Hud);
    if(!hud_enabled || !enabled || !known_ford || !native_hud.ready) {clear_mode_notice("inactive");return;}
    if(paused || !foreground) {clear_mode_notice("pause_or_focus",false);return;}
    auto manager=native_hud.manager();
    if(hud_submitted && hud_submitted!=manager) {
        // The old world's HUD may already have been destroyed. Never reuse it.
        hud_submitted=0;mode_notice.visible=false;
    }
    if(!manager)return;
    if(mode_notice.start(now,hud_duration_ms)) {
        char text[256];roadtrip::notice_text(mode_notice.selected,hud_russian,text,sizeof text);
        if(native_hud.show(manager,text)) {
            hud_submitted=manager;
            trace("MODE_NOTICE REQUEST source=game_hud mode_config=%.5f name=%s duration_ms=%u language=%s",
                mode_notice.selected,roadtrip::mode_name(mode_notice.selected),hud_duration_ms,hud_russian?"ru":"en");
        } else mode_notice.visible=false;
    }
    unsigned fade=native_hud.sites.fade_ms;
    if(mode_notice.visible && now>=mode_notice.expires-std::min(fade,hud_duration_ms))clear_mode_notice("timeout");
}
static void set_kickdown(bool next,const char *reason) {
    if((kick!=0)==next)return;
    refresh_config_mode();
    float before=roadtrip::mode(base_mode,scope!=0,kick!=0,power_mode,cruise_normal!=0);
    if(next)kick_origin=reason;
    if(next)reset_cruise_guard("kickdown");
    kick=next;
    float after=roadtrip::mode(base_mode,scope!=0,next,power_mode,cruise_normal!=0);
    trace("KICKDOWN %s source=plugin origin=%s reason=%s mode_config=%.5f effective_before=%.5f effective_after=%.5f value_changed=%d",
        next?"ON":"OFF",kick_origin,reason,base_mode,before,after,std::fabs(after-before)>0.0001f);
    if(next) {
        set_cruise_normal(false,"kickdown_priority");
        cruise_acceleration.reset();
    }
    if(!next)kick_origin="none";
}
static void init_paths() {
    GetModuleFileNameW(module_handle,dir,MAX_PATH);
    wchar_t *p=wcsrchr(dir,L'\\');if(p)*p=0;
    wcscat_s(dir,MAX_PATH,L"\\road_trip_shift");CreateDirectoryW(dir,nullptr);
    swprintf(status_path,MAX_PATH,L"%ls\\status.json",dir);
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\road_trip_shift.ini",dir);
    init_input_paths(path);
    analog_enabled=GetPrivateProfileIntW(L"input",L"analog_kickdown",1,path)!=0;
    enabled=GetPrivateProfileIntW(L"plugin",L"enabled",1,path)!=0;
    cruise_normal_enabled=GetPrivateProfileIntW(L"cruise",L"normal_acceleration",1,path)!=0;
    cruise_guard_enabled=GetPrivateProfileIntW(L"cruise",L"anti_hunt",1,path)!=0;
    hud_enabled=GetPrivateProfileIntW(L"hud",L"enabled",1,path)!=0;
    hud_duration_ms=std::clamp(GetPrivateProfileIntW(L"hud",L"duration_ms",2500,path),1000u,10000u);
    wchar_t hud_language[16]={};GetPrivateProfileStringW(L"hud",L"language",L"auto",hud_language,16,path);
    hud_russian=!_wcsicmp(hud_language,L"ru") || (!_wcsicmp(hud_language,L"auto") && PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_RUSSIAN);
    double_ms=std::clamp(GetPrivateProfileIntW(L"input",L"double_tap_ms",300,path),100u,700u);
    memory_sample_ms=std::clamp(GetPrivateProfileIntW(L"logging",L"memory_sample_ms",10000,path),1000u,60000u);
    timing_sample_ms=std::clamp(GetPrivateProfileIntW(L"logging",L"timing_sample_ms",10000,path),1000u,60000u);
    timing_hook_every=std::clamp(GetPrivateProfileIntW(L"logging",L"timing_hook_every",64,path),1u,4096u);
    sample_ms=std::clamp(GetPrivateProfileIntW(L"logging",L"sample_ms",200,path),100u,2000u);
    logging_enabled=GetPrivateProfileIntW(L"logging",L"enabled",1,path)!=0;
    performance.initialize(logging_enabled && GetPrivateProfileIntW(L"logging",L"timing",1,path)!=0);
    log_quota_mb=std::clamp(GetPrivateProfileIntW(L"logging",L"max_total_mb",50,path),1u,1024u);
    sample_count=0;disk_log.close();
    if(logging_enabled) {
        wchar_t logs[MAX_PATH];swprintf(logs,MAX_PATH,L"%ls\\logs",dir);
        SYSTEMTIME t;GetLocalTime(&t);wchar_t prefix[64];
        swprintf(prefix,64,L"%04u%02u%02u_%02u%02u%02u_%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
        const char *header="tick_ms,vehicle,paused,foreground,scope,kickdown,mode_config,mode_effective,input_down,input_throttle,effective_throttle,cruise_kmh,speed_kmh,rpm,gear,reader_calls,shadow_reads,mode_source,mode_override_changed,cruise_guard_cap,cruise_guard_clamps,effective_brake,keyboard_held,input_sample_valid,input_brake,tap_armed,tap_blocked,analog_down,input_gate,memory_sample_tick,memory_valid,process_private_bytes,process_working_set_bytes,process_handles,plugin_thunk_committed_bytes,plugin_thunk_retained_bytes,cruise_acceleration_normal\n";
        std::string csv_header=header;csv_header.pop_back();
        csv_header+=",engine_enabled,motor_brake,parking_brake,electric_enabled,retarder_level,input_clutch,effective_clutch,engine_wear,engine_signals_valid,qpc_ns\n";
        disk_log.begin(logs,prefix,csv_header.c_str(),(size_t)log_quota_mb*1024*1024);
    }
}
struct Patch {uint32_t rva;unsigned n;unsigned char old[8],replacement[8];bool live=false;};
static Patch patches[32];static unsigned patch_count=0,applied=0;
static roadtrip::ThunkMemory thunk_memory;
static unsigned char *&thunks=thunk_memory.data;
static size_t &thunk_used=thunk_memory.used;
struct MemorySample {
    ULONGLONG tick=0;
    size_t private_bytes=0,working_set_bytes=0,peak_working_set_bytes=0;
    DWORD handles=0;
    bool valid=false,handles_valid=false;
};
static MemorySample memory_sample;
static uint64_t memory_baseline=0;
static bool memory_baseline_seen=false;
static void sample_memory(ULONGLONG now,bool force=false) {
    if(!logging_enabled || (!force && memory_sample.tick && now-memory_sample.tick<memory_sample_ms))return;
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::Memory);
    memory_sample={};memory_sample.tick=now;
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof counters;
    memory_sample.valid=GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&counters,sizeof counters)!=0;
    if(memory_sample.valid) {
        memory_sample.private_bytes=counters.PrivateUsage;memory_sample.working_set_bytes=counters.WorkingSetSize;
        memory_sample.peak_working_set_bytes=counters.PeakWorkingSetSize;
        if(!memory_baseline_seen){memory_baseline=counters.PrivateUsage;memory_baseline_seen=true;}
    }
    memory_sample.handles_valid=GetProcessHandleCount(GetCurrentProcess(),&memory_sample.handles)!=0;
    trace("MEMORY source=process valid=%d private_bytes=%llu working_set_bytes=%llu peak_working_set_bytes=%llu private_delta_bytes=%lld handles_valid=%d handles=%lu plugin_thunk_committed_bytes=%llu plugin_thunk_active_bytes=%llu plugin_thunk_retained_bytes=%llu plugin_thunk_allocations=%u plugin_thunk_frees=%u plugin_thunk_retained_blocks=%u plugin_thunk_free_failures=%u",
        memory_sample.valid,(unsigned long long)memory_sample.private_bytes,(unsigned long long)memory_sample.working_set_bytes,
        (unsigned long long)memory_sample.peak_working_set_bytes,memory_sample.valid?(long long)memory_sample.private_bytes-(long long)memory_baseline:0,
        memory_sample.handles_valid,(unsigned long)memory_sample.handles,(unsigned long long)thunk_memory.committed_bytes(),
        (unsigned long long)thunk_memory.capacity,(unsigned long long)thunk_memory.retained_bytes,
        thunk_memory.allocations,thunk_memory.frees,thunk_memory.retained_blocks,thunk_memory.free_failures);
}
static roadtrip::NativeSites native_sites;
static bool validate_sites(uintptr_t image) {
    return roadtrip::resolve_native_sites(image,native_sites);
}
static float read_mode(void *var) {
    auto sequence=++reader_calls;
    bool measured=sequence%timing_hook_every==0;
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::ModeTotal,measured);
    bool scoped=scope!=0,kicked=kick!=0;
    float selected;
    {roadtrip::PerfScope native(performance,roadtrip::PerfKind::ModeStock,measured);selected=original_reader(var);}
    bool accel=cruise_normal!=0;
    if(scoped && (kicked || (accel && roadtrip::economy_selected(selected))))++shadow_reads;
    return roadtrip::mode(selected,scoped,kicked,power_mode,accel);
}
static int choose_gear(void *self,unsigned path=0) {
    bool measured=(chooser_calls[path].load()+1)%timing_hook_every==0;
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::ChoiceTotal,measured);
    int stock;
    {roadtrip::PerfScope native(performance,roadtrip::PerfKind::ChoiceStock,measured);stock=original_chooser(self);}
    int chosen=roadtrip::guard_choice(stock,cruise_cap.load(),scope!=0,kick!=0,guard_current_gear.load());
    if(chosen!=stock)++cruise_clamps;
    chooser_decision[path]=((uint64_t)(uint32_t)stock<<32)|(uint32_t)chosen;
    ++chooser_calls[path];
    return chosen;
}
static int choose_step_gear(void *self){return choose_gear(self,0);}
static int choose_direct_gear(void *self){return choose_gear(self,1);}
static unsigned char *abs_jump(void *target) {
    auto p=thunk_memory.take(16);if(!p)return nullptr;
    p[0]=0x48;p[1]=0xB8;memcpy(p+2,&target,8);p[10]=0xFF;p[11]=0xE0;return p;
}
static unsigned char *compare_stub(const unsigned char original[7]) {
    // Preserve every register. The original compare's flags survive RET.
    // For the supported player vehicle, report equality so cruise does not
    // block adaptive shifting; no cruise state or persistent setting is written.
    auto p=thunk_memory.take(48);if(!p)return nullptr;unsigned char *q=p;
    *q++=0x50;*q++=0x48;*q++=0xB8;uintptr_t address=(uintptr_t)&scope;memcpy(q,&address,8);q+=8;
    *q++=0x80;*q++=0x38;*q++=0x00;*q++=0x58;
    *q++=0x74;*q++=0x04;
    *q++=0x48;*q++=0x39;*q++=0xC0;*q++=0xC3;
    memcpy(q,original,7);q+=7;*q++=0xC3;return p;
}
static bool add_call(uint32_t rva,unsigned n,void *target) {
    if(!target || patch_count>=sizeof(patches)/sizeof(*patches) || n>sizeof(patches[0].old) || n<5)return false;
    Patch &p=patches[patch_count++];p.rva=rva;p.n=n;memcpy(p.old,(void*)(base+rva),n);
    memset(p.replacement,0x90,n);p.replacement[0]=0xE8;
    int64_t diff=(int64_t)(uintptr_t)target-(int64_t)(base+rva+5);
    if(diff<INT32_MIN||diff>INT32_MAX)return false;
    int32_t rel=(int32_t)diff;memcpy(p.replacement+1,&rel,4);return true;
}
static bool write_code(void *where,const void *bytes,size_t n) {
    DWORD old=0;if(!VirtualProtect(where,n,PAGE_EXECUTE_READWRITE,&old))return false;
    memcpy(where,bytes,n);FlushInstructionCache(GetCurrentProcess(),where,n);
    DWORD ignored=0;return VirtualProtect(where,n,old,&ignored)!=0;
}
static bool restore() {
    bool ok=true;
    for(unsigned i=patch_count;i>0;--i) {
        Patch &p=patches[i-1];if(!p.live)continue;
        if(write_code((void*)(base+p.rva),p.old,p.n))p.live=false;else ok=false;
    }
    if(ok)applied=0;
    return ok;
}
static bool install_hooks() {
    // Code still pointing at earlier stubs must not receive a second installation.
    if(applied){log("PREVIOUS HOOKS NOT RESTORED; no hooks");return false;}
    if(!thunk_memory.reset()){log("Cannot release unpublished stubs; no hooks");return false;}
    patch_count=0; // SDK reinit: never overwrite an owned allocation
    base=(uintptr_t)GetModuleHandleW(nullptr);
    // Only these two gearbox code regions read a shadow High Power value.
    // UI, config serialization and the stored g_adaptive_shift remain untouched.
    if(!validate_sites(base)) {log("Gearbox instruction patterns missing or ambiguous; no hooks applied");return false;}
    original_reader=(Reader)(base+native_sites.reader);adaptive_var=(void*)(base+native_sites.variable);
    original_chooser=(Chooser)(base+native_sites.chooser);
    if(!thunk_memory.reserve_near(base+native_sites.reads[0])){log("Cannot allocate nearby stubs; no hooks");return false;}
    roadtrip::PreparedThunks prepared(thunk_memory);
    void *reader_thunk=abs_jump((void*)&read_mode);
    for(auto r:native_sites.reads)if(!add_call(r,5,reader_thunk))return false;
    for(auto r:native_sites.compares)if(!add_call(r,7,compare_stub((unsigned char*)(base+r))))return false;
    if(!add_call(native_sites.choices[0],5,abs_jump((void*)&choose_step_gear)))return false;
    if(!add_call(native_sites.choices[1],5,abs_jump((void*)&choose_direct_gear)))return false;
    DWORD old;if(!VirtualProtect(thunks,thunk_memory.capacity,PAGE_EXECUTE_READ,&old))return false;
    FlushInstructionCache(GetCurrentProcess(),thunks,thunk_memory.capacity);
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)&read_mode,&pinned))return false;
    thunk_memory.published=true; // even a failed write/rollback can expose a stub to another thread
    for(unsigned i=0;i<patch_count;i++) {
        Patch &p=patches[i];applied=i+1;p.live=true; // include this entry in rollback even if restoring page protection fails
        if(!write_code((void*)(base+p.rva),p.replacement,p.n)){bool restored=restore();log("PATCH FAILED; rollback=%d",restored);return false;}
    }
    prepared.keep=true;
    log("HOOKS READY: %u sites resolved by instruction patterns; no version or EXE hash restriction",patch_count);return true;
}
// Preserve main cancellation: reset taps, without the PR release-required latch.
static void cancel_input() {keyboard.cancel();analog_pedal.cancel();analog_down=false;pedal_sample=false;observed_input=false;reported_gate="";set_cruise_normal(false,"input_cancelled");cruise_acceleration.reset();}
static void SCSAPIFUNC configuration(scs_event_t,const void *info,scs_context_t) {
    auto c=(const scs_telemetry_configuration_t*)info;if(!c)return;
    // Log identifiers so an SDK change is visible instead of silently treating a truck as a car.
    if(!strcmp(c->id,"controls")) {
        set_kickdown(false,"controls_change");cancel_input();reload_bindings=true;return;
    }
    if(strcmp(c->id,"truck") && strcmp(c->id,"car"))return;
    clear_mode_notice("vehicle_change");mode_notice.reset();
    engine_valid=0;std::fill(std::begin(engine_values),std::end(engine_values),0);
    vehicle[0]=0;
    for(auto a=c->attributes;a && a->name;++a)if(!strcmp(a->name,"id") && a->value.type==SCS_VALUE_TYPE_string && a->value.value_string.value) {
        snprintf(vehicle,sizeof vehicle,"%s",a->value.value_string.value);break;}
    set_kickdown(false,"vehicle_change");
    reset_cruise_guard("vehicle_change");
    known_ford=roadtrip::ford(vehicle);scope=0;cancel_input();reload_bindings=true;
    guard_current_gear=0;
    pedal=effective=cruise=brake=effective_brake=0;
    log("VEHICLE config=%s id=%s supported=%d",c->id,vehicle,known_ford);
}
static void SCSAPIFUNC channel(const scs_string_t,const scs_u32_t,const scs_value_t *v,const scs_context_t context) {
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::Channel);
    int id=(int)(intptr_t)context;
    if(id>=8){engine_signal((unsigned)(id-8),v);return;}
    if(id==0) {
        pedal_sample=v && v->type==SCS_VALUE_TYPE_float && std::isfinite(v->value_float.value) &&
            v->value_float.value>=0 && v->value_float.value<=1;
        pedal=pedal_sample?v->value_float.value:0;
        return;
    }
    if(!v)return;
    if(id==6) {if(v->type==SCS_VALUE_TYPE_s32){gear=v->value_s32.value;guard_current_gear=gear;}return;}
    if(v->type!=SCS_VALUE_TYPE_float)return;
    float x=v->value_float.value;if(!std::isfinite(x))return;
    switch(id){case 1:effective=x;break;case 2:rpm=x;break;case 3:speed=x;break;case 4:cruise=x;break;case 5:brake=x;break;case 7:effective_brake=x;break;}
}
static void SCSAPIFUNC activity(scs_event_t event,const void*,scs_context_t) {
    set_kickdown(false,event==SCS_TELEMETRY_EVENT_paused?"pause":"resume");
    reset_cruise_guard("activity_changed");
    paused=event==SCS_TELEMETRY_EVENT_paused;scope=0;cancel_input();reload_bindings=true;
    log("DRIVING paused=%d",paused);
}
static void refresh_scope_and_focus() {
    scope=patched && enabled && known_ford && !paused;
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);bool next=pid==GetCurrentProcessId();if(next && !foreground)reload_bindings=true;foreground=next;
}
static void SCSAPIFUNC frame_start(scs_event_t,const void *info,scs_context_t) {
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::FrameStart);
    auto t=(const scs_telemetry_frame_start_t*)info;
    if(t && (t->flags&SCS_TELEMETRY_FRAME_START_FLAG_timer_restart)) {
        set_kickdown(false,"timer_restart");reset_cruise_guard("timer_restart");cancel_input();
    }
    // Channels follow FRAME_START. Consume only this frame's input at FRAME_END.
    pedal_sample=false;
    refresh_config_mode();refresh_scope_and_focus();
    if(!scope || !foreground) {
        set_kickdown(false,!scope?"inactive":"focus_loss");cancel_input();
    }
}
static void process_input(ULONGLONG now,bool keyboard_held) {
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::Input);
    raw_keyboard_held=keyboard_held;
    bool allowed=scope && foreground && brake<0.1f;
    bool held=keyboard_held && keyboard_binding.valid;
    bool keykick=keyboard.update(now,held,allowed && keyboard_binding.valid,double_ms);
    if(held){last_keyboard_seen=now;keyboard_seen=true;reset_cruise_guard("manual_gas");}
    // Main analog path: 15% pressed / below 3% released. Do not count
    // keyboard Smart Throttle twice. SDK freshness is diagnostic for keyboard.
    analog_down=pedal>=(analog_down?0.03f:0.15f);
    bool analog=analog_pedal.update(now,analog_down,analog_enabled && allowed && !held &&
        (!keyboard_seen || now-last_keyboard_seen>250),double_ms);
    bool next=allowed && (keykick||analog);
    input_gate=!scope?"inactive":!foreground?"focus_loss":brake>=0.1f?"brake":
        keykick?"keyboard_double":analog?"pedal_double":
        !keyboard_binding.valid?"keyboard_binding_unavailable":held?"single_press":"released";
    if(scope && (held || observed_input || next) && strcmp(reported_gate,input_gate))
        trace("INPUT_GATE reason=%s key_held=%d sdk_valid=%d input=%.4f brake=%.4f armed=%d blocked=%d analog_down=%d analog_armed=%d",
            input_gate,held,pedal_sample,pedal,brake,keyboard.armed,keyboard.blocked,analog_down,analog_pedal.armed);
    reported_gate=input_gate;
    if(held!=observed_input)trace("THROTTLE %s source=game_input input=%.4f effective=%.4f sdk_valid=%d",held?"DOWN":"UP",pedal,effective,pedal_sample);
    observed_input=held;
    set_kickdown(next,next?(keykick?"keyboard_accelerator_double":"pedal_double"):input_gate);
}
static void log_gear_decisions(ULONGLONG now) {
    if(!logging_enabled || !scope)return;
    if(gear!=reported_gear) {
        trace("GEAR_CHANGED source=game_telemetry old=%d new=%d speed_kmh=%.2f rpm=%.1f mode_config=%.5f kickdown=%u cruise_kmh=%.2f input=%.4f effective=%.4f brake=%.4f guard_cap=%d",
            reported_gear,gear,speed*3.6f,rpm,base_mode,(unsigned)kick.load(),cruise*3.6f,pedal,effective,std::max(brake,effective_brake),cruise_cap.load());
        reported_gear=gear;
    }
    for(unsigned path=0;path<2;++path) {
        auto calls=chooser_calls[path].load();auto decision=chooser_decision[path].load();
        if(calls==reported_choices[path])continue;
        if(decision==reported_decision[path] && now-last_choice_log[path]<1500)continue;
        trace("GEAR_CHOICE source=game_logic caller=%s calls=%llu stock_candidate=%d plugin_return=%d observed_gear=%d speed_kmh=%.2f rpm=%.1f mode_config=%.5f kickdown=%u cruise_kmh=%.2f input=%.4f effective=%.4f guard_cap=%d",
            path?"direct":"adjacent_limited",calls,(int32_t)(decision>>32),(int32_t)decision,gear,speed*3.6f,rpm,base_mode,(unsigned)kick.load(),cruise*3.6f,pedal,effective,cruise_cap.load());
        reported_choices[path]=calls;reported_decision[path]=decision;last_choice_log[path]=now;
    }
}
static void SCSAPIFUNC frame_end(scs_event_t,const void*,scs_context_t) {
    roadtrip::PerfScope timing(performance,roadtrip::PerfKind::FrameEnd);
    ULONGLONG now=GetTickCount64();
    sample_performance(now);
    sample_memory(now);
    refresh_scope_and_focus();
    if(scope && foreground && refresh_keyboard_binding(now))cancel_input();
    update_mode_notice(now);
    process_input(now,keyboard_accelerator_held());
    update_cruise_acceleration();
    log_gear_decisions(now);
    roadtrip::CruiseSample load;
    load.eligible=cruise_guard_enabled && scope;load.kickdown=kick!=0;
    load.pedal=foreground && pedal_sample?pedal:0;load.brake=std::max(brake,effective_brake);load.speed=speed;
    load.target=cruise;load.throttle=effective;load.gear=gear;
    int old_cap=cruise_cap.load();
    {roadtrip::PerfScope guard_timing(performance,roadtrip::PerfKind::Guard);cruise_guard.update(now,load);cruise_cap=cruise_guard.cap;}
    if(old_cap!=cruise_guard.cap && !cruise_guard.cap)trace("CRUISE_GUARD CLEAR source=cruise_load old_cap=%d cap=0 reason=%s speed_kmh=%.2f target_kmh=%.2f throttle=%.3f",
        old_cap,cruise_guard.reason,speed*3.6f,cruise*3.6f,effective);
    if(old_cap!=cruise_guard.cap && cruise_guard.cap) {
        const auto &e=cruise_guard.evidence;
        trace("CRUISE_GUARD HOLD source=cruise_load old_cap=%d cap=%d reason=%s speed_kmh=%.2f target_kmh=%.2f throttle=%.3f failed_gear=%d trial=%s trial_speed_kmh=%.2f entry_throttle=%.3f peak_throttle=%.3f trial_age_ms=%llu",
            old_cap,cruise_guard.cap,cruise_guard.reason,speed*3.6f,cruise*3.6f,effective,e.failed_gear,e.observed_upshift?"observed_upshift":"engaged_gear",
            e.trial_speed*3.6f,e.entry_throttle,e.peak_throttle,(unsigned long long)e.trial_age_ms);
    }
    if(now-last_sample<sample_ms)return;
    last_sample=now;
    refresh_config_mode();
    if(!logging_enabled)return;
    roadtrip::PerfScope io_timing(performance,roadtrip::PerfKind::TelemetryIO);
    float current=roadtrip::mode(base_mode,scope!=0,kick!=0,power_mode,cruise_normal!=0);
    const char *source=mode_seen?roadtrip::mode_source(scope!=0,kick!=0,cruise_normal!=0):"unknown";
    bool overridden=mode_seen && roadtrip::mode_override(base_mode,scope!=0,kick!=0,power_mode,cruise_normal!=0);
    bool held=raw_keyboard_held && keyboard_binding.valid;
    char row[1024];snprintf(row,sizeof row,"%llu,%s,%d,%d,%u,%u,%.5f,%.5f,%d,%.4f,%.4f,%.6f,%.6f,%.1f,%d,%llu,%llu,%s,%d,%d,%llu,%.4f,%d,%d,%.4f,%d,%d,%d,%s,%llu,%d,%llu,%llu,%lu,%llu,%llu,%d\n",(unsigned long long)now,vehicle,paused,foreground,(unsigned)scope.load(),(unsigned)kick.load(),base_mode,current,held,pedal,effective,cruise*3.6f,speed*3.6f,rpm,gear,reader_calls.load(),shadow_reads.load(),source,overridden,cruise_cap.load(),cruise_clamps.load(),effective_brake,raw_keyboard_held,pedal_sample,brake,keyboard.armed,keyboard.blocked,analog_down,input_gate,(unsigned long long)memory_sample.tick,memory_sample.valid,(unsigned long long)memory_sample.private_bytes,(unsigned long long)memory_sample.working_set_bytes,(unsigned long)memory_sample.handles,(unsigned long long)thunk_memory.committed_bytes(),(unsigned long long)thunk_memory.retained_bytes,(unsigned)cruise_normal.load());
    size_t row_used=std::strlen(row);if(row_used && row[row_used-1]=='\n')row[--row_used]=0;
    std::snprintf(row+row_used,sizeof row-row_used,",%.0f,%.0f,%.0f,%.0f,%.0f,%.4f,%.4f,%.5f,%u,%llu\n",
        engine_value(0),engine_value(1),engine_value(2),engine_value(3),engine_value(4),engine_value(5),engine_value(6),engine_value(7),engine_valid,(unsigned long long)performance.elapsed_ns());
    disk_log.sample(row);++sample_count;if(sample_count%5==0)disk_log.flush();
    FILE *s=_wfopen(status_path,L"w");if(s){fprintf(s,"{\n\"version\":\"%s\",\"tick_ms\":%llu,\"hooks_ready\":%s,\"vehicle\":\"%s\",\"vehicle_supported\":%s,\"scope\":%s,\"paused\":%s,\"kickdown\":%s,\"mode_config\":%.5f,\"mode_config_known\":%s,\"mode_source\":\"%s\",\"mode_override_changed\":%s,\"mode_effective\":%.5f,\"speed_kmh\":%.2f,\"rpm\":%.1f,\"gear\":%d,\"keyboard_binding_ready\":%s,\n\"input_sample_valid\":%s,\n\"input_throttle\":%.4f,\"effective_throttle\":%.4f,\"cruise_kmh\":%.2f,\"reader_calls\":%llu,\"shadow_reads\":%llu,\"cruise_guard_cap\":%d,\"cruise_guard_clamps\":%llu,\"effective_brake\":%.4f,\n\"keyboard_held\":%s,\"input_brake\":%.4f,\"input_gate\":\"%s\",\"tap_armed\":%s,\"tap_blocked\":%s,\"analog_down\":%s,\n\"memory_sample_tick\":%llu,\"memory_valid\":%s,\"process_private_bytes\":%llu,\"process_working_set_bytes\":%llu,\"process_handles\":%lu,\"plugin_thunk_committed_bytes\":%llu,\"plugin_thunk_retained_bytes\":%llu,\"cruise_acceleration_normal\":%s\n}\n",VERSION,(unsigned long long)now,patched?"true":"false",vehicle,known_ford?"true":"false",scope?"true":"false",paused?"true":"false",kick?"true":"false",base_mode,mode_seen?"true":"false",source,overridden?"true":"false",current,speed*3.6f,rpm,gear,keyboard_binding.valid?"true":"false",pedal_sample?"true":"false",pedal,effective,cruise*3.6f,reader_calls.load(),shadow_reads.load(),cruise_cap.load(),cruise_clamps.load(),effective_brake,raw_keyboard_held?"true":"false",brake,input_gate,keyboard.armed?"true":"false",keyboard.blocked?"true":"false",analog_down?"true":"false",(unsigned long long)memory_sample.tick,memory_sample.valid?"true":"false",(unsigned long long)memory_sample.private_bytes,(unsigned long long)memory_sample.working_set_bytes,(unsigned long)memory_sample.handles,(unsigned long long)thunk_memory.committed_bytes(),(unsigned long long)thunk_memory.retained_bytes,cruise_normal?"true":"false");fclose(s);}
}
extern "C" __declspec(dllexport) SCSAPI_RESULT scs_telemetry_init(scs_u32_t version,const scs_telemetry_init_params_t *params) {
    if(SCS_GET_MAJOR_VERSION(version)!=1 || !params)return SCS_RESULT_unsupported;
    auto p=(const scs_telemetry_init_params_v100_t*)params;game_log=p->common.log;
    if(strcmp(p->common.game_id,"ats"))return SCS_RESULT_unsupported;
    init_paths();roadtrip::PerfScope init_timing(performance,roadtrip::PerfKind::Init);
    last_timing_sample=GetTickCount64();
    cancel_input();paused=true;scope=0;cruise_normal=0;cruise_acceleration.reset();
    native_hud={};mode_notice.reset();hud_submitted=0;
    engine_valid=engine_registered=0;std::fill(std::begin(engine_values),std::end(engine_values),0);
    log("INIT %s game=%s",VERSION,p->common.game_id);
    if(performance.enabled.load())log("PERF_CLOCK source=qpc unit=ns frequency_hz=%llu nominal_tick_ns=%.3f hook_every=%u report_ms=%u",
        (unsigned long long)performance.frequency,1000000000.0/(double)performance.frequency,timing_hook_every.load(),timing_sample_ms);
    memory_sample={};memory_baseline_seen=false;
    auto event=[&](scs_event_t e,scs_telemetry_event_callback_t fn){return p->register_for_event(e,fn,nullptr)==SCS_RESULT_ok;};
    bool ok=event(SCS_TELEMETRY_EVENT_configuration,configuration) && event(SCS_TELEMETRY_EVENT_paused,activity) && event(SCS_TELEMETRY_EVENT_started,activity) && event(SCS_TELEMETRY_EVENT_frame_start,frame_start) && event(SCS_TELEMETRY_EVENT_frame_end,frame_end);
    const char *names[]={"truck.input.throttle","truck.effective.throttle","truck.engine.rpm","truck.speed","truck.cruise_control","truck.input.brake","truck.engine.gear","truck.effective.brake"};
    for(int i=0;i<8;i++){scs_result_t r=p->register_for_channel(names[i],SCS_U32_NIL,i==6?SCS_VALUE_TYPE_s32:SCS_VALUE_TYPE_float,i==0?(SCS_TELEMETRY_CHANNEL_FLAG_each_frame|SCS_TELEMETRY_CHANNEL_FLAG_no_value):SCS_TELEMETRY_CHANNEL_FLAG_none,channel,(void*)(intptr_t)i);log("CHANNEL %s result=%d",names[i],r);ok=ok && r==SCS_RESULT_ok;}
    if(!ok){log("SDK REGISTRATION FAILED; no hooks applied");return SCS_RESULT_generic_error;}
    // Optional diagnostics must never prevent the transmission from loading.
    for(unsigned i=0;i<8;++i) {
        const auto &s=engine_signals[i];auto r=p->register_for_channel(s.name,SCS_U32_NIL,s.type,
            SCS_TELEMETRY_CHANNEL_FLAG_each_frame|SCS_TELEMETRY_CHANNEL_FLAG_no_value,channel,(void*)(intptr_t)(i+8));
        if(r==SCS_RESULT_ok)engine_registered|=1u<<i;
        log("CHANNEL %s result=%d optional=1",s.name,r);
    }
    if(enabled)start_binding_worker();
    if(enabled)patched=install_hooks();else log("Disabled by INI; telemetry only");
    if(enabled && hud_enabled) {
        bool ready=native_hud.initialize((uintptr_t)GetModuleHandleW(nullptr));
        log("MODE_NOTICE %s source=game_hud reason=%s",ready?"READY":"DISABLED",ready?"native_signatures":"missing_or_ambiguous_signature");
    }
    return SCS_RESULT_ok;
}
extern "C" __declspec(dllexport) SCSAPI_VOID scs_telemetry_shutdown() {
    clear_mode_notice("shutdown");native_hud={};mode_notice.reset();
    set_kickdown(false,"shutdown");scope=0;cancel_input();bool restored=restore();log("SHUTDOWN restored=%d",restored);
    reset_cruise_guard("shutdown");
    stop_binding_worker();
    // Stubs stay allocated: another game thread may still be returning through one.
    thunk_memory.reset();patched=false;
    sample_memory(GetTickCount64(),true);
    sample_performance(GetTickCount64(),true);
    disk_log.close();game_log=nullptr;
}
BOOL WINAPI DllMain(HINSTANCE inst,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){module_handle=inst;DisableThreadLibraryCalls(inst);}return TRUE;
}
