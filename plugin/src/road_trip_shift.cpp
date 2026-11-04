// Road Trip Shift, experimental ATS plugin.
// The call-site trampoline approach was informed by Deppelover's MIT project
// car_automatic_transmission. This narrower implementation retains stock maps.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
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
#include "cruise_guard.h"
#include "rolling_log.h"

static const char *VERSION="0.3.1 experimental";
static const char *EXE_HASH="28CA69A7ECA99519371ADCC3E78B978946AD28CE57332780E9FE4175BE92F6A7";
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
static std::atomic<unsigned long long> reader_calls{0},shadow_reads{0};
static std::atomic<int> cruise_cap{0};
static std::atomic<unsigned long long> cruise_clamps{0};
static roadtrip::CruiseGuard cruise_guard;
static bool cruise_guard_enabled=true;
static_assert(sizeof(scope)==1,"Native condition stub requires a one-byte atomic flag");
static roadtrip::AcceleratorInput accelerator;
static bool pedal_sample=false,observed_input=false;
static unsigned double_ms=300,sample_ms=200;
static float pedal=0,effective=0,rpm=0,speed=0,cruise=0,brake=0,effective_brake=0,power_mode=roadtrip::high_power;
static int gear=0;
static ULONGLONG last_sample=0;
static unsigned sample_count=0;
static bool foreground=false;
static roadtrip::KeyboardBinding keyboard_binding;
static std::wstring game_home,controls_override,selected_controls;
static bool reload_bindings=true,binding_reported=false;
static ULONGLONG last_binding_check=0;
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
static std::string read_input_file(const std::wstring &path,bool tail=false) {
    FILE *file=_wfopen(path.c_str(),L"rb");if(!file)return {};
    constexpr long limit=256*1024;
    if(fseek(file,0,SEEK_END)){fclose(file);return {};}
    long size=ftell(file);if(size<0 || (!tail && size>limit)){fclose(file);return {};}
    long begin=tail && size>limit?size-limit:0;
    if(fseek(file,begin,SEEK_SET)){fclose(file);return {};}
    std::string text(size-begin,'\0');size_t read=fread(text.data(),1,text.size(),file);fclose(file);text.resize(read);return text;
}
static void init_input_paths(const wchar_t *ini) {
    wchar_t value[MAX_PATH]={};GetPrivateProfileStringW(L"input",L"controls_file",L"",value,MAX_PATH,ini);
    controls_override=value;selected_controls.clear();game_home.clear();
    wchar_t documents[MAX_PATH]={};
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents)))
        game_home=std::wstring(documents)+L"\\American Truck Simulator";
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv) {
        for(int i=1;i+1<argc;++i)if(!wcscmp(argv[i],L"-homedir")) {
            wchar_t full[MAX_PATH]={};DWORD n=GetFullPathNameW(argv[i+1],MAX_PATH,full,nullptr);
            game_home=n && n<MAX_PATH?full:L"";break;
        }
        LocalFree(argv);
    }
    keyboard_binding={};reload_bindings=true;binding_reported=false;last_binding_check=0;
}
static bool refresh_keyboard_binding(ULONGLONG now) {
    if(!reload_bindings && now-last_binding_check<1000)return false;
    reload_bindings=false;last_binding_check=now;
    std::wstring path;
    if(!controls_override.empty()) {
        bool absolute=controls_override.size()>1 && controls_override[1]==L':';
        path=absolute?controls_override:game_home+L"\\"+controls_override;
    } else if(!game_home.empty()) {
        selected_controls.clear();
        auto profile=roadtrip::last_profile_controls(read_input_file(game_home+L"\\game.log.txt",true));
        if(!profile.id.empty())selected_controls=game_home+(profile.steam?L"\\steam_profiles\\":L"\\profiles\\")+
            std::wstring(profile.id.begin(),profile.id.end())+L"\\controls.sii";
        path=selected_controls;
    }
    auto next=roadtrip::parse_keyboard_binding(read_input_file(path));
    bool changed=next.valid!=keyboard_binding.valid || next.scans!=keyboard_binding.scans;
    keyboard_binding=next;
    if(changed || !binding_reported) {
        log("KEYBOARD_BINDING source=controls_sii ready=%d keys=%u",next.valid,(unsigned)next.scans.size());binding_reported=true;
    }
    return changed;
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
    init_input_paths(path);
    enabled=GetPrivateProfileIntW(L"plugin",L"enabled",1,path)!=0;
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
        const char *header="tick_ms,vehicle,paused,foreground,scope,kickdown,mode_config,mode_effective,input_down,input_throttle,effective_throttle,cruise_kmh,speed_kmh,rpm,gear,reader_calls,shadow_reads,mode_source,mode_override_changed,cruise_guard_cap,cruise_guard_clamps,effective_brake\n";
        disk_log.begin(logs,prefix,header,(size_t)log_quota_mb*1024*1024);
    }
}
static bool hash_exe(char out[65]) {
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;bool ok=false;
    unsigned char buf[65536],digest[32];DWORD got=0;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 &&
       BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0) {
        ok=true;
        for(;;) {if(!ReadFile(file,buf,sizeof buf,&got,nullptr)){ok=false;break;}if(!got)break;
            if(BCryptHashData(hash,buf,got,0)<0){ok=false;break;}}
        if(ok && BCryptFinishHash(hash,digest,32,0)<0)ok=false;
        if(ok)for(int i=0;i<32;i++)snprintf(out+2*i,3,"%02X",digest[i]);
    }
    if(hash)BCryptDestroyHash(hash);
    if(alg)BCryptCloseAlgorithmProvider(alg,0);
    CloseHandle(file);return ok;
}

struct Patch {uint32_t rva;unsigned n;unsigned char old[8],replacement[8];bool live=false;};
static Patch patches[32];static unsigned patch_count=0,applied=0;
static unsigned char *thunks;static size_t thunk_used=0;
static const uint32_t reads[]={0x623D76,0x623DBC,0x6245DE,0x624619,0x62463B,0x624AA3,0x624AD6,0x62501B,0x95669E,0x956989,0x95756E};
static const uint32_t compares[]={0x623D91,0x6245F7,0x624654,0x624ABC,0x625036,0x9566B7,0x957587};
static const uint32_t choices[]={0x9569A9,0x9569DB};
static bool validate_sites(uintptr_t image) {
    for(auto r:reads) {auto s=(unsigned char*)(image+r);int32_t rel;memcpy(&rel,s+1,4);
        if(s[0]!=0xE8 || (uintptr_t)(s+5)+rel!=image+0x1E2430){log("CONFLICT at cvar read RVA %X; no hooks",r);return false;}}
    for(auto r:compares) {auto s=(unsigned char*)(image+r);
        bool regular=s[0]==0x80 && (s[1]==0xB9||s[1]==0xB8) && !memcmp(s+2,"\x04\x02\0\0\0",5);
        bool r14=r==0x957587 && !memcmp(s,"\x44\x38\xB1\x04\x02\0\0",7);
        if(!regular&&!r14){log("CONFLICT at cruise compare RVA %X; no hooks",r);return false;}}
    for(auto r:choices) {auto s=(unsigned char*)(image+r);int32_t rel;memcpy(&rel,s+1,4);
        if(s[0]!=0xE8 || (uintptr_t)(s+5)+rel!=image+0x956FA0){log("CONFLICT at automatic chooser RVA %X; no hooks",r);return false;}}
    return true;
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
    // Code still pointing at earlier stubs must not receive a second installation.
    if(applied){log("PREVIOUS HOOKS NOT RESTORED; no hooks");return false;}
    patch_count=0;thunk_used=0; // SDK reinit: start with fresh bookkeeping and stubs
    char hash[65]={};if(!hash_exe(hash) || strcmp(hash,EXE_HASH)) {log("UNSUPPORTED EXE: sha256=%s; no hooks applied",hash);return false;}
    base=(uintptr_t)GetModuleHandleW(nullptr);original_reader=(Reader)(base+0x1E2430);adaptive_var=(void*)(base+0x2DB1600);
    original_chooser=(Chooser)(base+0x956FA0);
    // Only these two gearbox code regions read a shadow High Power value.
    // UI, config serialization and the stored g_adaptive_shift remain untouched.
    if(!validate_sites(base))return false;
    thunks=reserve_thunks(base+0x624000);if(!thunks){log("Cannot allocate nearby stubs; no hooks");return false;}
    void *reader_thunk=abs_jump((void*)&read_mode);
    for(auto r:reads)if(!add_call(r,5,reader_thunk))return false;
    for(auto r:compares)if(!add_call(r,7,compare_stub((unsigned char*)(base+r))))return false;
    void *chooser_thunk=abs_jump((void*)&choose_gear);
    for(auto r:choices)if(!add_call(r,5,chooser_thunk))return false;
    DWORD old;if(!VirtualProtect(thunks,4096,PAGE_EXECUTE_READ,&old))return false;
    FlushInstructionCache(GetCurrentProcess(),thunks,4096);
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)&read_mode,&pinned))return false;
    for(unsigned i=0;i<patch_count;i++) {
        Patch &p=patches[i];applied=i+1;p.live=true; // include this entry in rollback even if restoring page protection fails
        if(!write_code((void*)(base+p.rva),p.replacement,p.n)){bool restored=restore();log("PATCH FAILED; rollback=%d",restored);return false;}
    }
    log("HOOKS READY: %u guarded sites; ATS 1.62.1.1 exact executable",patch_count);return true;
}
// Input held across a cancellation must be released before it counts as a press.
static void cancel_input() {accelerator.cancel();pedal_sample=false;observed_input=false;}
static void SCSAPIFUNC configuration(scs_event_t,const void *info,scs_context_t) {
    auto c=(const scs_telemetry_configuration_t*)info;if(!c)return;
    // Log identifiers so an SDK change is visible instead of silently treating a truck as a car.
    if(!strcmp(c->id,"controls")) {
        set_kickdown(false,"controls_change");cancel_input();reload_bindings=true;return;
    }
    if(strcmp(c->id,"truck") && strcmp(c->id,"car"))return;
    vehicle[0]=0;
    for(auto a=c->attributes;a && a->name;++a)if(!strcmp(a->name,"id") && a->value.type==SCS_VALUE_TYPE_string && a->value.value_string.value) {
        snprintf(vehicle,sizeof vehicle,"%s",a->value.value_string.value);break;}
    set_kickdown(false,"vehicle_change");
    reset_cruise_guard("vehicle_change");
    known_ford=roadtrip::ford(vehicle);scope=0;cancel_input();reload_bindings=true;
    pedal=effective=cruise=brake=effective_brake=0;
    log("VEHICLE config=%s id=%s supported=%d",c->id,vehicle,known_ford);
}
static void SCSAPIFUNC channel(const scs_string_t,const scs_u32_t,const scs_value_t *v,const scs_context_t context) {
    int id=(int)(intptr_t)context;
    if(id==0) {
        pedal_sample=v && v->type==SCS_VALUE_TYPE_float && std::isfinite(v->value_float.value) &&
            v->value_float.value>=0 && v->value_float.value<=1;
        pedal=pedal_sample?v->value_float.value:0;
        return;
    }
    if(!v)return;
    if(id==6) {if(v->type==SCS_VALUE_TYPE_s32)gear=v->value_s32.value;return;}
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
    bool allowed=scope && foreground && brake<0.1f && pedal_sample && keyboard_binding.valid;
    bool next=accelerator.update(now,pedal,keyboard_held,allowed,double_ms);
    bool down=allowed && accelerator.down;
    if(down!=observed_input)log("THROTTLE %s source=game_input input=%.3f effective=%.3f",down?"DOWN":"UP",pedal,effective);
    observed_input=down;
    if(down)reset_cruise_guard("manual_gas");
    const char *reason=next?"keyboard_accelerator_double":!scope?"inactive":!foreground?"focus_loss":
        brake>=0.1f?"brake":!pedal_sample?"input_unavailable":!keyboard_binding.valid?"keyboard_binding_unavailable":"input_release";
    set_kickdown(next,reason);
}
static void SCSAPIFUNC frame_end(scs_event_t,const void*,scs_context_t) {
    ULONGLONG now=GetTickCount64();
    refresh_scope_and_focus();
    if(scope && foreground && refresh_keyboard_binding(now))cancel_input();
    process_input(now,keyboard_accelerator_held());
    roadtrip::CruiseSample load;
    load.eligible=cruise_guard_enabled && scope;load.kickdown=kick!=0;
    load.pedal=foreground && pedal_sample?pedal:0;load.brake=std::max(brake,effective_brake);load.speed=speed;
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
    bool held=pedal_sample && accelerator.down;
    char row[1024];snprintf(row,sizeof row,"%llu,%s,%d,%d,%u,%u,%.5f,%.5f,%d,%.4f,%.4f,%.3f,%.3f,%.1f,%d,%llu,%llu,%s,%d,%d,%llu,%.4f\n",(unsigned long long)now,vehicle,paused,foreground,(unsigned)scope.load(),(unsigned)kick.load(),base_mode,current,held,pedal,effective,cruise*3.6f,speed*3.6f,rpm,gear,reader_calls.load(),shadow_reads.load(),source,overridden,cruise_cap.load(),cruise_clamps.load(),effective_brake);
    disk_log.sample(row);++sample_count;if(sample_count%5==0)disk_log.flush();
    FILE *s=_wfopen(status_path,L"w");if(s){fprintf(s,"{\n\"version\":\"%s\",\"tick_ms\":%llu,\"hooks_ready\":%s,\"vehicle\":\"%s\",\"vehicle_supported\":%s,\"scope\":%s,\"paused\":%s,\"kickdown\":%s,\"mode_config\":%.5f,\"mode_config_known\":%s,\"mode_source\":\"%s\",\"mode_override_changed\":%s,\"mode_effective\":%.5f,\"speed_kmh\":%.2f,\"rpm\":%.1f,\"gear\":%d,\"keyboard_binding_ready\":%s,\n\"input_sample_valid\":%s,\n\"input_throttle\":%.4f,\"effective_throttle\":%.4f,\"cruise_kmh\":%.2f,\"reader_calls\":%llu,\"shadow_reads\":%llu,\"cruise_guard_cap\":%d,\"cruise_guard_clamps\":%llu,\"effective_brake\":%.4f\n}\n",VERSION,(unsigned long long)now,patched?"true":"false",vehicle,known_ford?"true":"false",scope?"true":"false",paused?"true":"false",kick?"true":"false",base_mode,mode_seen?"true":"false",source,overridden?"true":"false",current,speed*3.6f,rpm,gear,keyboard_binding.valid?"true":"false",pedal_sample?"true":"false",pedal,effective,cruise*3.6f,reader_calls.load(),shadow_reads.load(),cruise_cap.load(),cruise_clamps.load(),effective_brake);fclose(s);}
}
extern "C" __declspec(dllexport) SCSAPI_RESULT scs_telemetry_init(scs_u32_t version,const scs_telemetry_init_params_t *params) {
    if(SCS_GET_MAJOR_VERSION(version)!=1 || !params)return SCS_RESULT_unsupported;
    auto p=(const scs_telemetry_init_params_v100_t*)params;game_log=p->common.log;
    if(strcmp(p->common.game_id,"ats"))return SCS_RESULT_unsupported;
    init_paths();cancel_input();paused=true;scope=0;log("INIT %s game=%s",VERSION,p->common.game_id);
    auto event=[&](scs_event_t e,scs_telemetry_event_callback_t fn){return p->register_for_event(e,fn,nullptr)==SCS_RESULT_ok;};
    bool ok=event(SCS_TELEMETRY_EVENT_configuration,configuration) && event(SCS_TELEMETRY_EVENT_paused,activity) && event(SCS_TELEMETRY_EVENT_started,activity) && event(SCS_TELEMETRY_EVENT_frame_start,frame_start) && event(SCS_TELEMETRY_EVENT_frame_end,frame_end);
    const char *names[]={"truck.input.throttle","truck.effective.throttle","truck.engine.rpm","truck.speed","truck.cruise_control","truck.input.brake","truck.engine.gear","truck.effective.brake"};
    for(int i=0;i<8;i++){scs_result_t r=p->register_for_channel(names[i],SCS_U32_NIL,i==6?SCS_VALUE_TYPE_s32:SCS_VALUE_TYPE_float,i==0?(SCS_TELEMETRY_CHANNEL_FLAG_each_frame|SCS_TELEMETRY_CHANNEL_FLAG_no_value):SCS_TELEMETRY_CHANNEL_FLAG_none,channel,(void*)(intptr_t)i);log("CHANNEL %s result=%d",names[i],r);ok=ok && r==SCS_RESULT_ok;}
    if(!ok){log("SDK REGISTRATION FAILED; no hooks applied");return SCS_RESULT_generic_error;}
    if(enabled)patched=install_hooks();else log("Disabled by INI; telemetry only");
    return SCS_RESULT_ok;
}
extern "C" __declspec(dllexport) SCSAPI_VOID scs_telemetry_shutdown() {
    set_kickdown(false,"shutdown");scope=0;cancel_input();bool restored=restore();log("SHUTDOWN restored=%d",restored);
    reset_cruise_guard("shutdown");
    // Stubs stay allocated: another game thread may still be returning through one.
    thunks=nullptr;patched=false;
    disk_log.close();game_log=nullptr;
}
BOOL WINAPI DllMain(HINSTANCE inst,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){module_handle=inst;DisableThreadLibraryCalls(inst);}return TRUE;
}
