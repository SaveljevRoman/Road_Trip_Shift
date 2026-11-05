#include "road_trip_shift.cpp"
#include <cassert>
#include <cstdlib>

static char logged_events[16384]={};
static void SCSAPIFUNC fake_log(scs_log_type_t,const char *message) {
    size_t used=std::strlen(logged_events);
    if(used+2<sizeof logged_events)std::snprintf(logged_events+used,sizeof logged_events-used,"%s\n",message);
}
static scs_result_t SCSAPIFUNC fake_event(scs_event_t,scs_telemetry_event_callback_t,scs_context_t) {return SCS_RESULT_ok;}
static scs_result_t SCSAPIFUNC fake_channel(const char *name,scs_u32_t,scs_value_type_t,scs_u32_t flags,scs_telemetry_channel_callback_t,scs_context_t) {
    if(!std::strcmp(name,"truck.input.throttle"))assert(flags==(SCS_TELEMETRY_CHANNEL_FLAG_each_frame|SCS_TELEMETRY_CHANNEL_FLAG_no_value));
    return SCS_RESULT_ok;
}
static float fake_reader(void *p) {return *(float*)p;}
static int fake_chooser(void *p) {return *(int*)p;}

static void test_thunk_lifetime() {
    roadtrip::ThunkMemory memory;
    for(unsigned i=0;i<64;++i) {
        assert(memory.reserve_near((uintptr_t)&test_thunk_lifetime));
        auto address=memory.data;
        {
            roadtrip::PreparedThunks preparation(memory);
            assert(memory.take(memory.capacity));assert(!memory.take(1));
            // Simulate every pre-publication return path through the same owner.
        }
        assert(!memory.data && memory.committed_bytes()==0);
        MEMORY_BASIC_INFORMATION info{};assert(VirtualQuery(address,&info,sizeof info));assert(info.State==MEM_FREE);
    }
    assert(memory.allocations==64 && memory.frees==64 && !memory.retained_blocks && !memory.free_failures);
    assert(memory.reserve_near((uintptr_t)&test_thunk_lifetime));
    auto published=memory.data;size_t bytes=memory.capacity;memory.published=true;
    assert(memory.reset());assert(!memory.data && memory.retained_blocks==1 && memory.retained_bytes==bytes);
    assert(memory.committed_bytes()==bytes);
    // Only this synthetic page is known never to have been called by any thread.
    assert(VirtualFree(published,0,MEM_RELEASE));
    std::puts("PASS: 64 unpublished preparation failures free their pages, bounds reject overflow, published pages stay tracked.");
}


static unsigned char *signature_fixture(uint32_t shift) {
    auto memory=(unsigned char*)VirtualAlloc(nullptr,0x20000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(memory);
    auto dos=(IMAGE_DOS_HEADER*)memory;dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    auto nt=(IMAGE_NT_HEADERS64*)(memory+0x100);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->FileHeader.NumberOfSections=1;
    nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfImage=0x20000;
    auto section=IMAGE_FIRST_SECTION(nt);section->VirtualAddress=0x1000+shift;
    section->Misc.VirtualSize=0x4000;section->Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
    auto put_relative=[&](uint32_t rva,unsigned offset,unsigned length,uint32_t target) {
        int32_t relative=(int32_t)target-(int32_t)(rva+length);memcpy(memory+rva+offset,&relative,4);
    };
    for(unsigned i=0;i<13;i++) {
        const auto &sig=i<11?roadtrip::read_signatures[i]:roadtrip::choice_signatures[i-11];
        roadtrip::InstructionPattern pattern;assert(pattern.parse(sig.pattern));
        uint32_t at=section->VirtualAddress+i*0x100;
        for(unsigned n=0;n<pattern.length;n++)memory[at+n]=pattern.fixed[n]?pattern.bytes[n]:(unsigned char)(17+i);
        put_relative(at+sig.call_offset,1,5,section->VirtualAddress+(i<11?0x3000+(shift?0x70:0):0x3100+(shift?0xB0:0)));
        if(i<11)put_relative(at,3,7,0x10000+(shift?0x1900:0));
    }
    return memory;
}
static void test_signature_resolution() {
    assert(!validate_sites(0));
    auto first=signature_fixture(0);auto shifted=signature_fixture(0x5000);
    roadtrip::NativeSites a,b;
    assert(roadtrip::resolve_native_sites((uintptr_t)first,a));
    assert(roadtrip::resolve_native_sites((uintptr_t)shifted,b));
    assert(a.reader+0x5070==b.reader && a.chooser+0x50B0==b.chooser && a.variable+0x1900==b.variable);
    for(unsigned i=0;i<11;i++)assert(a.reads[i]+0x5000==b.reads[i]);
    for(unsigned i=0;i<7;i++)assert(a.compares[i]+0x5000==b.compares[i]);
    for(unsigned i=0;i<2;i++)assert(a.choices[i]+0x5000==b.choices[i]);
    first[0x40]^=0x7F; // unrelated bytes do not affect compatibility
    assert(roadtrip::resolve_native_sites((uintptr_t)first,a));
    unsigned char saved=first[0x1000];first[0x1000]=0x90;
    assert(!roadtrip::resolve_native_sites((uintptr_t)first,b));first[0x1000]=saved;
    roadtrip::InstructionPattern pattern;assert(pattern.parse(roadtrip::read_signatures[0].pattern));
    memcpy(first+0x4800,first+0x1000,pattern.length);
    assert(!roadtrip::resolve_native_sites((uintptr_t)first,b)); // ambiguous match
    memset(first+0x4800,0,pattern.length);
    int32_t relative;memcpy(&relative,first+a.reads[0]+1,4);int32_t altered=relative+0x40;
    memcpy(first+a.reads[0]+1,&altered,4);assert(!roadtrip::resolve_native_sites((uintptr_t)first,b));
    memcpy(first+a.reads[0]+1,&relative,4);
    DWORD old=0;assert(VirtualProtect(first+0x1000,4096,PAGE_NOACCESS,&old));
    assert(!roadtrip::resolve_native_sites((uintptr_t)first,b));
    DWORD ignored=0;assert(VirtualProtect(first+0x1000,4096,old,&ignored));
    assert(roadtrip::resolve_native_sites((uintptr_t)first,b));
    VirtualFree(first,0,MEM_RELEASE);VirtualFree(shifted,0,MEM_RELEASE);
    std::puts("PASS: all 20 sites relocate with code/data/functions; no hash restriction; missing/ambiguous/inconsistent/unreadable patterns refuse safely.");
}

int wmain(int argc,wchar_t **argv) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    test_thunk_lifetime();
    test_signature_resolution();
    assert(argc==1 || argc==2);
    if(argc==2) {
        // Map PE sections as resources: no game code, imports or entrypoint run.
        HMODULE image=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
        assert(image);uintptr_t mapped=(uintptr_t)image&~(uintptr_t)3;
        assert(validate_sites(mapped));FreeLibrary(image);
        std::puts("PASS: all 20 native patch sites validated against the installed ATS image.");
    } else std::puts("SKIP: native addresses vs game image (no GameExe supplied).");

    module_handle=GetModuleHandleW(nullptr);
    scs_telemetry_init_params_v100_t init{};init.common.game_id="ats";init.common.log=fake_log;
    init.register_for_event=fake_event;init.register_for_channel=fake_channel;
    assert(scs_telemetry_init(SCS_TELEMETRY_VERSION_CURRENT,&init)==SCS_RESULT_ok);
    assert(!patched && applied==0); // this host has no gearbox instruction patterns
    std::puts("PASS: host without gearbox instruction patterns leaves hooks disabled.");
    patch_count=20;applied=0;assert(!install_hooks() && patch_count==0); // SDK reinit after a clean shutdown
    patch_count=5;applied=1;assert(!install_hooks() && patch_count==5 && applied==1); // never stack on live patches
    patch_count=0;applied=0;
    std::puts("PASS: reinitialization resets bookkeeping and refuses unrestored patches.");

    scs_named_value_t attrs[2]{};attrs[0].name="id";attrs[0].value.type=SCS_VALUE_TYPE_string;
    scs_telemetry_configuration_t conf{};conf.id="truck";conf.attributes=attrs;
    patched=true;paused=false; // synthetic SDK-to-hook eligibility, without patching this test host
    for(auto id:{"ford.f150_23","ford.bronco_24","ford.victoria_06","ford.mustang_67","vehicle.ford.f150_23","vehicle.ford.bronco_24","vehicle.ford.victoria_06","vehicle.ford.mustang_67"}) {
        attrs[0].value.value_string.value=id;configuration(0,&conf,nullptr);assert(known_ford);
        frame_start(0,nullptr,nullptr);assert(scope==1);
    }
    for(auto id:{"kenworth.t680","vehicle.kenworth.t680","vehicle.ford.modcar","vehicle.vehicle.ford.f150_23"}) {
        attrs[0].value.value_string.value=id;configuration(0,&conf,nullptr);assert(!known_ford);
        frame_start(0,nullptr,nullptr);assert(scope==0);
    }
    // Exercise the runtime profile/file loader using isolated test-owned files.
    const auto saved_home=game_home;
    game_home=L"keyboard_binding_fixture";controls_override.clear();selected_controls.clear();
    CreateDirectoryW(game_home.c_str(),nullptr);
    for(const wchar_t *sub:{L"\\profiles",L"\\profiles\\AB12",L"\\steam_profiles",L"\\steam_profiles\\CD34"})
        CreateDirectoryW((game_home+sub).c_str(),nullptr);
    auto write_input_file=[&](const wchar_t *relative,const char *text) {
        FILE *f=_wfopen((game_home+relative).c_str(),L"wb");assert(f);std::fputs(text,f);std::fclose(f);
    };
    write_input_file(L"\\game.log.txt","/home/profiles/AB12/controls.sii\n");
    write_input_file(L"\\profiles\\AB12\\controls.sii","mix dforward `keyboard.uarrow?0`\n");
    reload_bindings=true;refresh_keyboard_binding(1000);
    assert(keyboard_binding.valid && keyboard_binding.scans==std::vector<unsigned>({0xe048}));
    write_input_file(L"\\profiles\\AB12\\controls.sii","mix dforward `keyboard.a?0`\n");
    assert(refresh_keyboard_binding(2000));assert(keyboard_binding.scans==std::vector<unsigned>({0x1e}));
    write_input_file(L"\\game.log.txt","/home/profiles/AB12/controls.sii\n/steam/profiles/CD34/quick_replies.sii\n");
    write_input_file(L"\\steam_profiles\\CD34\\controls.sii","mix dforward `keyboard.key1?0 | joy.b3?0`\n");
    refresh_keyboard_binding(3000);assert(keyboard_binding.valid && keyboard_binding.scans==std::vector<unsigned>({2}));
    write_input_file(L"\\steam_profiles\\CD34\\controls.sii","mix dforward `joy.b3?0`\n");
    refresh_keyboard_binding(4000);assert(!keyboard_binding.valid);
    write_input_file(L"\\game.log.txt","no identifiable active profile\n");
    refresh_keyboard_binding(5000);assert(!keyboard_binding.valid); // never retain stale profile selection
    controls_override=L"profiles\\AB12\\controls.sii";
    reload_bindings=true;refresh_keyboard_binding(5001);assert(keyboard_binding.valid);
    game_home=saved_home;controls_override.clear();selected_controls.clear();reload_bindings=true;
    std::puts("PASS: active local/Steam profile files, live key rebinding, controller-only binding refusal and explicit controls override.");
    // Exercise actual SDK callback ordering, not just the tap state machine.
    scope=1;foreground=true;brake=0;keyboard_binding.valid=true;
    auto input=[&](ULONGLONG now,float value,int held=-1) {
        scs_value_t v{};v.type=SCS_VALUE_TYPE_float;v.value_float.value=value;
        channel("truck.input.throttle",SCS_U32_NIL,&v,nullptr);process_input(now,held<0?value>=0.15f:held!=0);
    };
    input(10000,0);input(10010,1);assert(!kick);input(10020,0);input(10050,1);assert(kick);
    input(10060,0);assert(!kick);input(10070,1);assert(!kick);
    input(10080,0);input(10100,1);assert(kick);
    for(int event_case=0;event_case<4;event_case++) {
        ULONGLONG start=11000+event_case*1000;
        if(event_case==0) {
            activity(SCS_TELEMETRY_EVENT_paused,nullptr,nullptr);
            activity(SCS_TELEMETRY_EVENT_started,nullptr,nullptr);
        } else if(event_case==1) {
            scs_telemetry_frame_start_t timer{};timer.flags=SCS_TELEMETRY_FRAME_START_FLAG_timer_restart;
            frame_start(0,&timer,nullptr);
        } else if(event_case==2) {
            attrs[0].value.value_string.value="vehicle.ford.f150_23";configuration(0,&conf,nullptr);
        } else {foreground=false;input(start,0);foreground=true;cancel_input();}
        scope=1;foreground=true;
        input(start+100,1);assert(!kick);input(start+120,0);input(start+150,1);assert(!kick);
        input(start+160,0);input(start+180,1);assert(kick);
    }
    // No cached throttle may arm a tap in a frame missing a fresh SDK sample.
    pedal_sample=false;process_input(20400,false);assert(!kick);
    input(20410,1);assert(!kick);input(20420,0);input(20430,1);assert(!kick);
    input(20440,0);input(20450,1);assert(kick);
    channel("truck.input.throttle",SCS_U32_NIL,nullptr,nullptr);process_input(20460,false);assert(!kick && !pedal_sample);
    input(20470,1);assert(!kick);input(20480,0);input(20490,1);assert(!kick);
    input(20500,0);input(20510,1);assert(kick);
    brake=0.5f;input(20520,1);assert(!kick);brake=0;input(20530,1);assert(!kick);
    input(20540,0);input(20550,1);assert(!kick);
    // Effective cruise throttle is independent of the accelerator action.
    cancel_input();effective=1;input(20600,0);input(20610,0);assert(!kick);
    input(20620,1,0);input(20630,0,0);input(20640,1,0);assert(!kick); // controller double press
    input(20650,1,1);assert(!kick);input(20660,1,0);input(20670,1,1);assert(kick);
    input(20680,1,0);assert(!kick); // controller gas remains held after key release
    keyboard_binding.valid=false;input(20690,1,1);assert(!kick);keyboard_binding.valid=true;
    input(20700,1,1);assert(!kick);input(20710,0,0);input(20720,1,1);assert(!kick);
    // Replay the low initial SDK values seen with in-game Smart Throttle.
    logged_events[0]=0;cancel_input();input(20800,0,0);
    input(20810,0,1);assert(!kick && !accelerator.taps.blocked);
    input(20830,0.03f,1);assert(!kick);
    input(20850,0.08f,0);assert(!kick);
    input(20865,0.06f,0);assert(!kick);input(20880,0.04f,0);assert(!kick);
    input(20900,0,1);assert(!kick);
    input(20920,0.03f,1);assert(kick);
    input(20940,0.08f,1);assert(kick);
    input(20960,0.2f,0);assert(!kick);
    assert(strstr(logged_events,"KICKDOWN ON source=plugin"));
    assert(strstr(logged_events,"INPUT_GATE reason=awaiting_sdk_throttle"));
    assert(strstr(logged_events,"confirmed=1"));
    pedal_sample=false;pedal=1;frame_start(0,nullptr,nullptr);assert(!pedal_sample);
    cancel_input();pedal=effective=0;
    std::puts("PASS: mapped keyboard accelerator, controller exclusion, full pause/resume, timer/vehicle/focus cancellation, missing samples, brake and cruise exclusion.");
    patched=false;paused=true;
    float setting=10;original_reader=fake_reader;scope=1;kick=0;
    assert(read_mode(&setting)==10);kick=1;assert(read_mode(&setting)==roadtrip::high_power);
    setting=3;kick=0;assert(read_mode(&setting)==3); // reads the current selection, never a stale cached mode
    scope=0;kick=1;assert(read_mode(&setting)==3);
    assert(reader_calls==4 && shadow_reads==1);
    std::puts("PASS: SDK configuration scope and native mode shadow/restoration.");

    logged_events[0]=0;adaptive_var=&setting;mode_seen=false;setting=10;scope=1;kick=0;
    refresh_config_mode();assert(strstr(logged_events,"CONFIG_MODE_INITIAL source=game_setting"));
    set_kickdown(true,"keyboard_accelerator_double");assert(setting==10);
    assert(strstr(logged_events,"KICKDOWN ON source=plugin"));
    assert(!strstr(logged_events,"CONFIG_MODE_CHANGED")); // a plugin request never masquerades as a user setting change
    setting=roadtrip::high_power;refresh_config_mode();
    assert(strstr(logged_events,"CONFIG_MODE_CHANGED source=game_setting"));
    assert(!roadtrip::mode_override(setting,true,true,power_mode)); // user selected the same value separately
    set_kickdown(false,"input_release");assert(setting==roadtrip::high_power);
    assert(strstr(logged_events,"KICKDOWN OFF source=plugin"));
    logged_events[0]=0;
    set_kickdown(true,"keyboard_accelerator_double");
    assert(strstr(logged_events,"value_changed=0")); // configured High Power + double accelerator press: request with no mode-value change
    assert(!strstr(logged_events,"CONFIG_MODE_CHANGED"));
    set_kickdown(false,"input_release");
    std::puts("PASS: configured High Power vs plugin kickdown have distinct event sources, including identical mode values.");

    // One continuous SDK-input-to-native-reader regression: multiple released
    // frames between taps must still override Economy without writing it.
    logged_events[0]=0;setting=10;scope=1;foreground=true;brake=0;keyboard_binding.valid=true;
    cancel_input();input(25000,0,0);input(25010,0.02f,1);input(25050,0.04f,0);
    input(25070,0.02f,0);input(25090,0,0);input(25130,0.02f,1);
    assert(kick && setting==10 && read_mode(&setting)==roadtrip::high_power);
    input(25200,0.1f,1);assert(kick && read_mode(&setting)==roadtrip::high_power);
    input(25220,0.2f,0);assert(!kick && setting==10 && read_mode(&setting)==10);
    assert(strstr(logged_events,"effective_after=1.66670 value_changed=1"));
    std::puts("PASS: multi-frame keyboard double tap overrides Economy in the native reader, holds and restores without changing the setting.");

    logged_events[0]=0;cruise_acceleration.reset();cruise_normal=0;setting=10;base_mode=10;
    scope=1;kick=0;pedal=brake=effective_brake=0;pedal_sample=true;cruise=speed=20;
    update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
    cruise=25;update_cruise_acceleration();assert(cruise_normal && setting==10 && read_mode(&setting)==3);
    assert(strstr(logged_events,"CRUISE_ACCEL ON source=plugin reason=target_increased"));
    set_kickdown(true,"keyboard_accelerator_double");assert(read_mode(&setting)==roadtrip::high_power);
    update_cruise_acceleration();assert(!cruise_normal);set_kickdown(false,"released");assert(read_mode(&setting)==10);
    cruise=30;update_cruise_acceleration();assert(cruise_normal);
    speed=30;update_cruise_acceleration();assert(!cruise_normal && setting==10 && read_mode(&setting)==10);
    assert(strstr(logged_events,"reason=target_reached"));
    speed=29;update_cruise_acceleration();assert(cruise_normal && setting==10 && read_mode(&setting)==3);
    assert(strstr(logged_events,"reason=speed_deficit"));
    // This is the current plugin Normal path, including the same cruise guard,
    // rather than a bypass of existing Normal behavior.
    auto saved_chooser=original_chooser;auto saved_clamps=cruise_clamps.load();
    original_chooser=fake_chooser;int recovery_candidate=10;cruise_cap=9;
    assert(choose_gear(&recovery_candidate)==9);
    original_chooser=saved_chooser;cruise_clamps=saved_clamps;cruise_cap=0;
    pedal=0.2f;update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
    pedal=0;update_cruise_acceleration();assert(cruise_normal && read_mode(&setting)==3);
    raw_keyboard_held=true;update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
    raw_keyboard_held=false;update_cruise_acceleration();assert(cruise_normal && read_mode(&setting)==3);
    speed=30;update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
    // ACC traffic braking has priority. Once it releases the brake, recover
    // the unchanged cruise target through the same Normal mode reader.
    scs_value_t acc_brake{};acc_brake.type=SCS_VALUE_TYPE_float;acc_brake.value_float.value=0.4f;
    channel("truck.effective.brake",SCS_U32_NIL,&acc_brake,(void*)(intptr_t)7);
    speed=25;update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
    speed=20;update_cruise_acceleration();assert(!cruise_normal && cruise==30);
    acc_brake.value_float.value=0;channel("truck.effective.brake",SCS_U32_NIL,&acc_brake,(void*)(intptr_t)7);
    update_cruise_acceleration();assert(cruise_normal && read_mode(&setting)==3);
    // Real keyboard double tap during cruise recovery: first press is Economy,
    // second/held press is High Power, release restores recovery if still needed.
    cancel_input();input(26000,0,0);update_cruise_acceleration();assert(cruise_normal);
    input(26010,0.02f,1);update_cruise_acceleration();assert(!kick && !cruise_normal && read_mode(&setting)==10);
    input(26050,0,0);update_cruise_acceleration();assert(cruise_normal);
    input(26070,0,0);input(26100,0.02f,1);update_cruise_acceleration();
    assert(kick && !cruise_normal && setting==10 && read_mode(&setting)==roadtrip::high_power);
    input(26200,0.3f,1);update_cruise_acceleration();assert(kick && read_mode(&setting)==roadtrip::high_power);
    input(26220,0.2f,0);update_cruise_acceleration();assert(!kick && read_mode(&setting)==10);
    input(26240,0,0);update_cruise_acceleration();assert(cruise_normal && read_mode(&setting)==3);
    speed=30;update_cruise_acceleration();assert(!cruise_normal && setting==10 && read_mode(&setting)==10);
    std::puts("PASS: ACC traffic braking yields to unchanged-target recovery; keyboard double tap on cruise has High Power priority and releases back to recovery/Economy.");
    setting=roadtrip::high_power;refresh_config_mode();cruise=35;update_cruise_acceleration();
    assert(!cruise_normal && read_mode(&setting)==roadtrip::high_power);
    setting=10;refresh_config_mode();cruise=40;update_cruise_acceleration();assert(cruise_normal);
    cancel_input();assert(!cruise_normal && read_mode(&setting)==10);
    cruise=speed=0;
    std::puts("PASS: target increase and later speed loss reach the current Normal reader/guard path, preserve manual Economy and never write the selection.");

    original_chooser=fake_chooser;int stock_choice=10;scope=1;kick=0;cruise_cap=9;
    assert(choose_gear(&stock_choice)==9);assert(cruise_clamps==1);
    stock_choice=7;assert(choose_gear(&stock_choice)==7);
    stock_choice=0;assert(choose_gear(&stock_choice)==0);
    stock_choice=-1;assert(choose_gear(&stock_choice)==-1);
    stock_choice=10;kick=1;assert(choose_gear(&stock_choice)==10);
    scope=0;kick=0;assert(choose_gear(&stock_choice)==10);
    scope=1;assert(read_mode(&setting)==setting); // cruise hold does not select a different transmission mode
    logged_events[0]=0;gear=4;stock_choice=9;kick=0;cruise_cap=0;
    auto count_step=chooser_calls[0].load(),count_direct=chooser_calls[1].load();
    assert(choose_step_gear(&stock_choice)==9);assert(choose_direct_gear(&stock_choice)==9);
    assert(chooser_calls[0]==count_step+1 && chooser_calls[1]==count_direct+1);
    log_gear_decisions(22000);
    assert(strstr(logged_events,"caller=adjacent_limited"));assert(strstr(logged_events,"caller=direct"));
    assert(strstr(logged_events,"stock_candidate=9 plugin_return=9 observed_gear=4"));
    assert(strstr(logged_events,"GEAR_CHANGED source=game_telemetry"));
    logging_enabled=false;logged_events[0]=0;log_gear_decisions(24000);assert(!logged_events[0]);logging_enabled=true;
    std::puts("PASS: unchanged native chooser results, separate adjacent/direct path diagnostics and observed gear changes.");
    logged_events[0]=0;sample_memory(30000,true);
    assert(memory_sample.valid && memory_sample.private_bytes>0 && memory_sample.handles_valid);
    assert(strstr(logged_events,"MEMORY source=process valid=1"));
    assert(strstr(logged_events,"plugin_thunk_committed_bytes="));
    auto memory_tick=memory_sample.tick;sample_memory(memory_tick+1);assert(memory_sample.tick==memory_tick);
    logging_enabled=false;sample_memory(memory_tick+memory_sample_ms+1,true);assert(memory_sample.tick==memory_tick);logging_enabled=true;
    std::puts("PASS: process memory/handles and separate thunk accounting, sample interval and disabled diagnostics.");
    // Full input -> native reader/chooser matrix: priority must not depend on
    // the user's adaptive mode or either cruise option, or diagnostic logging.
    auto saved_normal_option=cruise_normal_enabled,saved_guard_option=cruise_guard_enabled;
    auto saved_logging=logging_enabled;auto saved_chooser_priority=original_chooser;
    auto saved_clamps_priority=cruise_clamps.load();
    ULONGLONG priority_tick=40000;
    for(float selected:{0.0f,roadtrip::economy_mode,roadtrip::normal_mode,roadtrip::high_power,7.5f})
    for(bool normal_option:{false,true})for(bool guard_option:{false,true})for(bool diagnostics:{false,true}) {
        logged_events[0]=0;logging_enabled=diagnostics;cruise_normal_enabled=normal_option;cruise_guard_enabled=guard_option;
        setting=selected;base_mode=selected;scope=1;foreground=true;brake=effective_brake=0;
        keyboard_binding.valid=true;kick=0;cancel_input();cruise=30;speed=20;original_chooser=fake_chooser;
        input(priority_tick,0,0);update_cruise_acceleration();
        input(priority_tick+10,0.02f,1);update_cruise_acceleration();assert(!kick && read_mode(&setting)==selected);
        input(priority_tick+40,0,0);update_cruise_acceleration();input(priority_tick+60,0,0);
        input(priority_tick+100,0.02f,1);
        // Even before the regular cruise update, the lower-priority state is gone.
        assert(kick && !cruise_normal && !cruise_acceleration.active && read_mode(&setting)==roadtrip::high_power);
        set_cruise_normal(true,"priority_probe");assert(!cruise_normal && read_mode(&setting)==roadtrip::high_power);
        update_cruise_acceleration();assert(kick && !cruise_normal);
        cruise_cap=1;
        for(int native_candidate:{-1,0,3,10})assert(choose_gear(&native_candidate)==native_candidate);
        // Changing the game setting during the hold cannot end High Power.
        setting=roadtrip::economy_selected(selected)?roadtrip::normal_mode:roadtrip::economy_mode;
        refresh_config_mode();input(priority_tick+150,1,1);update_cruise_acceleration();
        assert(kick && read_mode(&setting)==roadtrip::high_power);
        float current_selection=setting;
        input(priority_tick+200,0,0);update_cruise_acceleration();
        float restored=normal_option && roadtrip::economy_selected(setting)?roadtrip::normal_mode:setting;
        assert(!kick && setting==current_selection && read_mode(&setting)==restored);
        cruise_cap=0;priority_tick+=1000;
    }
    logging_enabled=saved_logging;cruise_normal_enabled=saved_normal_option;cruise_guard_enabled=saved_guard_option;
    original_chooser=saved_chooser_priority;cruise_clamps=saved_clamps_priority;
    cruise=speed=0;cancel_input();kick=0;scope=0;
    std::puts("PASS: double keyboard gas has immediate High Power priority across Off/Economy/Normal/High Power/custom, cruise options, logging and setting changes while held; native gear candidates pass unchanged.");
    reset_cruise_guard("test_complete");original_chooser=nullptr;
    std::puts("PASS: native chooser ceiling preserves stock downshifts, neutral, reverse and kickdown, without changing mode.");

    thunks=(unsigned char*)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(thunks);thunk_used=0;thunk_memory.capacity=4096;
    unsigned char actor[0x210]={};
    const unsigned char variants[3][7]={{0x80,0xB9,4,2,0,0,0},{0x80,0xB8,4,2,0,0,0},{0x44,0x38,0xB1,4,2,0,0}};
    using Probe=int(*)(void*);
    for(unsigned v=0;v<3;v++) {
        auto target=compare_stub(variants[v]);
        auto wrapper=thunks+thunk_used;thunk_used+=64;auto q=wrapper;
        if(v==1){*q++=0x48;*q++=0x89;*q++=0xC8;} // rax = rcx
        if(v==2){*q++=0x41;*q++=0x56;*q++=0x45;*q++=0x31;*q++=0xF6;} // save r14; xor r14d,r14d
        *q++=0xE8;int32_t rel=(int32_t)(target-(q+4));memcpy(q,&rel,4);q+=4;
        *q++=0x0F;*q++=0x94;*q++=0xC0; // sete al
        if(v==2){*q++=0x41;*q++=0x5E;} // restore r14
        *q++=0x0F;*q++=0xB6;*q++=0xC0;*q++=0xC3;
        FlushInstructionCache(GetCurrentProcess(),thunks,4096);auto probe=(Probe)wrapper;
        scope=0;actor[0x204]=0;assert(probe(actor)==1);
        actor[0x204]=1;assert(probe(actor)==0);
        scope=1;assert(probe(actor)==1);
        scope=0;assert(probe(actor)==0);
    }
    VirtualFree(thunks,0,MEM_RELEASE);thunks=nullptr;thunk_memory.capacity=0;
    std::puts("PASS: executed RCX, RAX and R14B comparison stubs: stock vs Ford cruise condition.");

    auto sandbox=(unsigned char*)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(sandbox);
    base=(uintptr_t)sandbox;patch_count=2;applied=2;
    for(unsigned i=0;i<2;i++){patches[i].rva=i*16;patches[i].n=5;patches[i].live=true;memset(patches[i].old,0x90,5);memset(sandbox+i*16,0xCC,5);}
    assert(restore());assert(!memcmp(sandbox,"\x90\x90\x90\x90\x90",5));assert(!memcmp(sandbox+16,sandbox,5));
    VirtualFree(sandbox,0,MEM_RELEASE);
    // Disabling diagnostics must leave gearbox behavior alive and stop file/SDK output.
    disk_log.close();logging_enabled=false;logged_events[0]=0;
    FILE *status=_wfopen(status_path,L"wb");assert(status);std::fputs("sentinel",status);std::fclose(status);
    setting=10;adaptive_var=&setting;scope=1;kick=0;
    set_kickdown(true,"keyboard_accelerator_double");assert(read_mode(&setting)==roadtrip::high_power);
    last_sample=0;unsigned count=sample_count;frame_end(0,nullptr,nullptr);assert(sample_count==count);
    set_kickdown(false,"input_release");assert(read_mode(&setting)==10);assert(logged_events[0]==0);
    status=_wfopen(status_path,L"rb");assert(status);char snapshot[16]={};std::fread(snapshot,1,sizeof snapshot-1,status);std::fclose(status);
    assert(std::strcmp(snapshot,"sentinel")==0);logging_enabled=true;
    std::puts("PASS: disabled diagnostics stop file/status/SDK output without disabling kickdown and mode restoration.");
    original_reader=nullptr;adaptive_var=nullptr;scope=0;kick=0;frame_end(0,nullptr,nullptr);scs_telemetry_shutdown();
    std::puts("PASS: reversible patch restoration on isolated test memory; telemetry/log shutdown.");
    return 0;
}
