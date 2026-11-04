// Road Trip Shift, experimental ATS plugin.
// The call-site trampoline approach was informed by Deppelover's MIT project
// car_automatic_transmission. This narrower implementation retains stock maps.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
#include "cruise_guard.h"
#include "rolling_log.h"
#include "native_scan.h"

static const char *VERSION="0.3.1 experimental";
static HMODULE module_handle;
static uintptr_t base;
static scs_log_t game_log;
static roadtrip::RollingLog disk_log;
static bool logging_enabled=true;
static unsigned log_quota_mb=50;
static wchar_t dir[MAX_PATH],status_path[MAX_PATH];
static char vehicle[128]="unknown";
static bool paused=true,known_ford=false,patched=false,enabled=true,analog_enabled=true;
static std::atomic<unsigned char> scope{0};
static std::atomic<unsigned char> kick{0};
static std::atomic<unsigned long long> reader_calls{0},shadow_reads{0};
static std::atomic<int> cruise_cap{0};
static std::atomic<unsigned long long> cruise_clamps{0};
static roadtrip::CruiseGuard cruise_guard;
static bool cruise_guard_enabled=true;
static_assert(sizeof(scope)==1,"Native condition stub requires a one-byte atomic flag");
static roadtrip::Kickdown keyboard;
static roadtrip::Kickdown analog_pedal;
static bool analog_down=false;
static unsigned double_ms=300,sample_ms=200;
static float pedal=0,effective=0,rpm=0,speed=0,cruise=0,brake=0,effective_brake=0,power_mode=roadtrip::high_power;
static int gear=0;
static ULONGLONG last_sample=0,last_w_seen=0;
static ULONGLONG w_press_tick=0;
static bool observed_w=false;
static unsigned sample_count=0;
static bool foreground=false;
using Reader=float(*)(void*);
using Chooser=int(*)(void*);
static Reader original_reader;
static Chooser original_chooser;
static void *adaptive_var;
static float base_mode=0;
static bool mode_seen=false;
static const char *kick_origin="none";

static void log(const char *fmt,...) {
    if(!logging_enabled)return;
    char msg[1024];va_list ap;va_start(ap,fmt);vsnprintf(msg,sizeof msg,fmt,ap);va_end(ap);
    char line[1100];snprintf(line,sizeof line,"%llu %s\n",(unsigned long long)GetTickCount64(),msg);disk_log.event(line);
    if(game_log) {char line[1100];snprintf(line,sizeof line,"[road_trip_shift] %s",msg);game_log(SCS_LOG_TYPE_message,line);}
}
static void reset_cruise_guard(const char *reason) {
    int before=cruise_cap.exchange(0);cruise_guard.reset(reason);
    if(before)log("CRUISE_GUARD CLEAR source=cruise_load old_cap=%d reason=%s",before,reason);
}
static void refresh_config_mode() {
    if(!original_reader || !adaptive_var)return;
    float value=original_reader(adaptive_var);if(!std::isfinite(value))return;
    if(!mode_seen)log("CONFIG_MODE_INITIAL source=game_setting value=%.5f name=%s",value,roadtrip::mode_name(value));
    else if(std::fabs(value-base_mode)>0.000001f) {
        log("CONFIG_MODE_CHANGED source=game_setting old=%.5f new=%.5f name=%s kickdown_request=%u",base_mode,value,roadtrip::mode_name(value),(unsigned)kick.load());
        reset_cruise_guard("configured_mode_changed");
    }
    base_mode=value;mode_seen=true;
}
static void set_kickdown(bool next,const char *reason) {
    if((kick!=0)==next)return;
    refresh_config_mode();
    float before=roadtrip::mode(base_mode,scope!=0,kick!=0,power_mode);
    if(next)kick_origin=reason;
    if(next)reset_cruise_guard("kickdown");
    kick=next;
    float after=roadtrip::mode(base_mode,scope!=0,next,power_mode);
    log("KICKDOWN %s source=plugin origin=%s reason=%s mode_config=%.5f effective_before=%.5f effective_after=%.5f value_changed=%d",
        next?"ON":"OFF",kick_origin,reason,base_mode,before,after,std::fabs(after-before)>0.0001f);
    if(!next)kick_origin="none";
}
static void init_paths() {
    GetModuleFileNameW(module_handle,dir,MAX_PATH);
    wchar_t *p=wcsrchr(dir,L'\\');if(p)*p=0;
    wcscat_s(dir,MAX_PATH,L"\\road_trip_shift");CreateDirectoryW(dir,nullptr);
    swprintf(status_path,MAX_PATH,L"%ls\\status.json",dir);
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\road_trip_shift.ini",dir);
    enabled=GetPrivateProfileIntW(L"plugin",L"enabled",1,path)!=0;
    analog_enabled=GetPrivateProfileIntW(L"input",L"analog_kickdown",1,path)!=0;
    cruise_guard_enabled=GetPrivateProfileIntW(L"cruise",L"anti_hunt",1,path)!=0;
    double_ms=std::clamp(GetPrivateProfileIntW(L"input",L"double_tap_ms",300,path),100u,700u);
    sample_ms=std::clamp(GetPrivateProfileIntW(L"logging",L"sample_ms",200,path),100u,2000u);
    logging_enabled=GetPrivateProfileIntW(L"logging",L"enabled",1,path)!=0;
    log_quota_mb=std::clamp(GetPrivateProfileIntW(L"logging",L"max_total_mb",50,path),1u,1024u);
    sample_count=0;disk_log.close();
    if(logging_enabled) {
        wchar_t logs[MAX_PATH];swprintf(logs,MAX_PATH,L"%ls\\logs",dir);
        SYSTEMTIME t;GetLocalTime(&t);wchar_t prefix[64];
        swprintf(prefix,64,L"%04u%02u%02u_%02u%02u%02u_%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
        const char *header="tick_ms,vehicle,paused,foreground,scope,kickdown,mode_config,mode_effective,w_down,input_throttle,effective_throttle,cruise_kmh,speed_kmh,rpm,gear,reader_calls,shadow_reads,mode_source,mode_override_changed,cruise_guard_cap,cruise_guard_clamps,effective_brake\n";
        disk_log.begin(logs,prefix,header,(size_t)log_quota_mb*1024*1024);
    }
}
struct Patch {uint32_t rva;unsigned n;unsigned char old[8],replacement[8];bool live=false;};
static Patch patches[32];static unsigned patch_count=0,applied=0;
static unsigned char *thunks;static size_t thunk_used=0;
static roadtrip::NativeSites native_sites;
static bool validate_sites(uintptr_t image) {
    return roadtrip::resolve_native_sites(image,native_sites);
}
static float read_mode(void *var) {
    ++reader_calls;
    bool scoped=scope!=0,kicked=kick!=0;
    if(scoped && kicked)++shadow_reads;
    return roadtrip::mode(original_reader(var),scoped,kicked,power_mode);
}
static int choose_gear(void *self) {
    int stock=original_chooser(self);
    int chosen=roadtrip::guard_choice(stock,cruise_cap.load(),scope!=0,kick!=0);
    if(chosen!=stock)++cruise_clamps;
    return chosen;
}
static unsigned char *reserve_thunks(uintptr_t around) {
    SYSTEM_INFO si;GetSystemInfo(&si);uintptr_t gran=si.dwAllocationGranularity;
    for(uintptr_t d=gran;d<0x40000000;d+=gran) {
        uintptr_t candidates[2]={(around-d)&~(gran-1),(around+d)&~(gran-1)};
        for(uintptr_t addr:candidates)if(auto p=VirtualAlloc((void*)addr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE))return (unsigned char*)p;
    }return nullptr;
}
static unsigned char *abs_jump(void *target) {
    auto p=thunks+thunk_used;thunk_used+=16;
    p[0]=0x48;p[1]=0xB8;memcpy(p+2,&target,8);p[10]=0xFF;p[11]=0xE0;return p;
}
static unsigned char *compare_stub(const unsigned char original[7]) {
    // Preserve every register. The original compare's flags survive RET.
    // For the supported player vehicle, report equality so cruise does not
    // block adaptive shifting; no cruise state or persistent setting is written.
    auto p=thunks+thunk_used;thunk_used+=48;unsigned char *q=p;
    *q++=0x50;*q++=0x48;*q++=0xB8;uintptr_t address=(uintptr_t)&scope;memcpy(q,&address,8);q+=8;
    *q++=0x80;*q++=0x38;*q++=0x00;*q++=0x58;
    *q++=0x74;*q++=0x04;
    *q++=0x48;*q++=0x39;*q++=0xC0;*q++=0xC3;
    memcpy(q,original,7);q+=7;*q++=0xC3;return p;
}
static bool add_call(uint32_t rva,unsigned n,void *target) {
    if(patch_count>=32 || thunk_used>4000)return false;
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
    base=(uintptr_t)GetModuleHandleW(nullptr);
    // Only these two gearbox code regions read a shadow High Power value.
    // UI, config serialization and the stored g_adaptive_shift remain untouched.
    if(!validate_sites(base)) {log("Gearbox instruction patterns missing or ambiguous; no hooks applied");return false;}
    original_reader=(Reader)(base+native_sites.reader);adaptive_var=(void*)(base+native_sites.variable);
    original_chooser=(Chooser)(base+native_sites.chooser);
    thunks=reserve_thunks(base+native_sites.reads[0]);if(!thunks){log("Cannot allocate nearby stubs; no hooks");return false;}
    void *reader_thunk=abs_jump((void*)&read_mode);
    for(auto r:native_sites.reads)if(!add_call(r,5,reader_thunk))return false;
    for(auto r:native_sites.compares)if(!add_call(r,7,compare_stub((unsigned char*)(base+r))))return false;
    void *chooser_thunk=abs_jump((void*)&choose_gear);
    for(auto r:native_sites.choices)if(!add_call(r,5,chooser_thunk))return false;
    DWORD old;if(!VirtualProtect(thunks,4096,PAGE_EXECUTE_READ,&old))return false;
    FlushInstructionCache(GetCurrentProcess(),thunks,4096);
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)&read_mode,&pinned))return false;
    for(unsigned i=0;i<patch_count;i++) {
        Patch &p=patches[i];applied=i+1;p.live=true; // include this entry in rollback even if restoring page protection fails
        if(!write_code((void*)(base+p.rva),p.replacement,p.n)){bool restored=restore();log("PATCH FAILED; rollback=%d",restored);return false;}
    }
    log("HOOKS READY: %u sites resolved by instruction patterns; no version or EXE hash restriction",patch_count);return true;
}
static void SCSAPIFUNC configuration(scs_event_t,const void *info,scs_context_t) {
    auto c=(const scs_telemetry_configuration_t*)info;if(!c)return;
    // Log identifiers so an SDK change is visible instead of silently treating a truck as a car.
    if(strcmp(c->id,"truck") && strcmp(c->id,"car"))return;
    vehicle[0]=0;
    for(auto a=c->attributes;a && a->name;++a)if(!strcmp(a->name,"id") && a->value.type==SCS_VALUE_TYPE_string && a->value.value_string.value) {
        snprintf(vehicle,sizeof vehicle,"%s",a->value.value_string.value);break;}
    set_kickdown(false,"vehicle_change");
    reset_cruise_guard("vehicle_change");
    known_ford=roadtrip::ford(vehicle);scope=0;keyboard.cancel();analog_pedal.cancel();analog_down=false;
    pedal=effective=cruise=brake=effective_brake=0;
    log("VEHICLE config=%s id=%s supported=%d",c->id,vehicle,known_ford);
}
static void SCSAPIFUNC channel(const scs_string_t,const scs_u32_t,const scs_value_t *v,const scs_context_t context) {
    if(!v)return;
    int id=(int)(intptr_t)context;
    if(id==6) {if(v->type==SCS_VALUE_TYPE_s32)gear=v->value_s32.value;return;}
    if(v->type!=SCS_VALUE_TYPE_float)return;
    float x=v->value_float.value;if(!std::isfinite(x))return;
    switch(id){case 0:pedal=x;break;case 1:effective=x;break;case 2:rpm=x;break;case 3:speed=x;break;case 4:cruise=x;break;case 5:brake=x;break;case 7:effective_brake=x;break;}
}
static void SCSAPIFUNC activity(scs_event_t event,const void*,scs_context_t) {
    set_kickdown(false,event==SCS_TELEMETRY_EVENT_paused?"pause":"resume");
    reset_cruise_guard("activity_changed");
    paused=event==SCS_TELEMETRY_EVENT_paused;scope=0;keyboard.cancel();analog_pedal.cancel();analog_down=false;
    log("DRIVING paused=%d",paused);
}
static void SCSAPIFUNC frame_start(scs_event_t,const void *info,scs_context_t) {
    auto t=(const scs_telemetry_frame_start_t*)info;
    if(t && (t->flags&SCS_TELEMETRY_FRAME_START_FLAG_timer_restart)){set_kickdown(false,"timer_restart");reset_cruise_guard("timer_restart");keyboard.cancel();analog_pedal.cancel();analog_down=false;}
    refresh_config_mode();
    scope=patched && enabled && known_ford && !paused;
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);foreground=pid==GetCurrentProcessId();
    ULONGLONG now=GetTickCount64();bool held=(GetAsyncKeyState('W')&0x8000)!=0;
    if(held)reset_cruise_guard("manual_gas");
    if(scope && foreground && held!=observed_w) {
        if(held)log("W DOWN gap_ms=%llu input=%.3f effective=%.3f",(unsigned long long)(w_press_tick?now-w_press_tick:0),pedal,effective);
        else log("W UP held_ms=%llu input=%.3f effective=%.3f",(unsigned long long)(w_press_tick?now-w_press_tick:0),pedal,effective);
    }
    if(held && !observed_w)w_press_tick=now;
    observed_w=held;
    if(held)last_w_seen=now;
    bool keykick=keyboard.update(now,held,scope && foreground && brake<0.1f,double_ms);
    // The same double-press rule for a physical pedal/trigger. A single full
    // press must retain the configured mode, rather than becoming kickdown.
    // W temporarily suppresses this path so its raw input is not counted twice.
    analog_down=pedal>=(analog_down?0.03f:0.15f);
    bool analog=analog_pedal.update(now,analog_down,analog_enabled && scope && foreground && brake<0.1f && !held && now-last_w_seen>250,double_ms);
    unsigned char next=scope && foreground && brake<0.1f && (keykick||analog);
    const char *reason=next?(keykick?"W_double":"pedal_double"):(!scope?"inactive":!foreground?"focus_loss":brake>=0.1f?"brake":"input_release");
    set_kickdown(next!=0,reason);
}
static void SCSAPIFUNC frame_end(scs_event_t,const void*,scs_context_t) {
    ULONGLONG now=GetTickCount64();
    roadtrip::CruiseSample load;
    load.eligible=cruise_guard_enabled && scope;load.kickdown=kick!=0;
    load.pedal=pedal;load.brake=std::max(brake,effective_brake);load.speed=speed;
    load.target=cruise;load.throttle=effective;load.gear=gear;
    int old_cap=cruise_cap.load();cruise_guard.update(now,load);cruise_cap=cruise_guard.cap;
    if(old_cap!=cruise_guard.cap)log("CRUISE_GUARD %s source=cruise_load old_cap=%d cap=%d reason=%s speed_kmh=%.2f target_kmh=%.2f throttle=%.3f",
        cruise_guard.cap?"HOLD":"CLEAR",old_cap,cruise_guard.cap,cruise_guard.reason,speed*3.6f,cruise*3.6f,effective);
    if(now-last_sample<sample_ms)return;
    last_sample=now;
    refresh_config_mode();
    if(!logging_enabled)return;
    float current=roadtrip::mode(base_mode,scope!=0,kick!=0,power_mode);
    const char *source=mode_seen?roadtrip::mode_source(scope!=0,kick!=0):"unknown";
    bool overridden=mode_seen && roadtrip::mode_override(base_mode,scope!=0,kick!=0,power_mode);
    bool held=(GetAsyncKeyState('W')&0x8000)!=0;
    char row[1024];snprintf(row,sizeof row,"%llu,%s,%d,%d,%u,%u,%.5f,%.5f,%d,%.4f,%.4f,%.3f,%.3f,%.1f,%d,%llu,%llu,%s,%d,%d,%llu,%.4f\n",(unsigned long long)now,vehicle,paused,foreground,(unsigned)scope.load(),(unsigned)kick.load(),base_mode,current,held,pedal,effective,cruise*3.6f,speed*3.6f,rpm,gear,reader_calls.load(),shadow_reads.load(),source,overridden,cruise_cap.load(),cruise_clamps.load(),effective_brake);
    disk_log.sample(row);++sample_count;if(sample_count%5==0)disk_log.flush();
    FILE *s=_wfopen(status_path,L"w");if(s){fprintf(s,"{\n\"version\":\"%s\",\"tick_ms\":%llu,\"hooks_ready\":%s,\"vehicle\":\"%s\",\"vehicle_supported\":%s,\"scope\":%s,\"paused\":%s,\"kickdown\":%s,\"mode_config\":%.5f,\"mode_config_known\":%s,\"mode_source\":\"%s\",\"mode_override_changed\":%s,\"mode_effective\":%.5f,\"speed_kmh\":%.2f,\"rpm\":%.1f,\"gear\":%d,\"input_throttle\":%.4f,\"effective_throttle\":%.4f,\"cruise_kmh\":%.2f,\"reader_calls\":%llu,\"shadow_reads\":%llu,\"cruise_guard_cap\":%d,\"cruise_guard_clamps\":%llu,\"effective_brake\":%.4f\n}\n",VERSION,(unsigned long long)now,patched?"true":"false",vehicle,known_ford?"true":"false",scope?"true":"false",paused?"true":"false",kick?"true":"false",base_mode,mode_seen?"true":"false",source,overridden?"true":"false",current,speed*3.6f,rpm,gear,pedal,effective,cruise*3.6f,reader_calls.load(),shadow_reads.load(),cruise_cap.load(),cruise_clamps.load(),effective_brake);fclose(s);}
}
extern "C" __declspec(dllexport) SCSAPI_RESULT scs_telemetry_init(scs_u32_t version,const scs_telemetry_init_params_t *params) {
    if(SCS_GET_MAJOR_VERSION(version)!=1 || !params)return SCS_RESULT_unsupported;
    auto p=(const scs_telemetry_init_params_v100_t*)params;game_log=p->common.log;
    if(strcmp(p->common.game_id,"ats"))return SCS_RESULT_unsupported;
    init_paths();log("INIT %s game=%s",VERSION,p->common.game_id);
    auto event=[&](scs_event_t e,scs_telemetry_event_callback_t fn){return p->register_for_event(e,fn,nullptr)==SCS_RESULT_ok;};
    bool ok=event(SCS_TELEMETRY_EVENT_configuration,configuration) && event(SCS_TELEMETRY_EVENT_paused,activity) && event(SCS_TELEMETRY_EVENT_started,activity) && event(SCS_TELEMETRY_EVENT_frame_start,frame_start) && event(SCS_TELEMETRY_EVENT_frame_end,frame_end);
    const char *names[]={"truck.input.throttle","truck.effective.throttle","truck.engine.rpm","truck.speed","truck.cruise_control","truck.input.brake","truck.engine.gear","truck.effective.brake"};
    for(int i=0;i<8;i++){scs_result_t r=p->register_for_channel(names[i],SCS_U32_NIL,i==6?SCS_VALUE_TYPE_s32:SCS_VALUE_TYPE_float,SCS_TELEMETRY_CHANNEL_FLAG_none,channel,(void*)(intptr_t)i);log("CHANNEL %s result=%d",names[i],r);ok=ok && r==SCS_RESULT_ok;}
    if(!ok){log("SDK REGISTRATION FAILED; no hooks applied");return SCS_RESULT_generic_error;}
    if(enabled)patched=install_hooks();else log("Disabled by INI; telemetry only");
    return SCS_RESULT_ok;
}
extern "C" __declspec(dllexport) SCSAPI_VOID scs_telemetry_shutdown() {
    set_kickdown(false,"shutdown");scope=0;keyboard.cancel();analog_pedal.cancel();bool restored=restore();log("SHUTDOWN restored=%d",restored);
    reset_cruise_guard("shutdown");
    if(restored && thunks)VirtualFree(thunks,0,MEM_RELEASE);
    thunks=nullptr;patched=false;
    disk_log.close();
}
BOOL WINAPI DllMain(HINSTANCE inst,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){module_handle=inst;DisableThreadLibraryCalls(inst);}return TRUE;
}
