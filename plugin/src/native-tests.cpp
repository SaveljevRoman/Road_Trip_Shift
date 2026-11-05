#include "road_trip_shift.cpp"
#include <cassert>
#include <cstdlib>

static char logged_events[16384]={};
static bool reject_optional_engine=false;
static void SCSAPIFUNC fake_log(scs_log_type_t,const char *message) {
    size_t used=std::strlen(logged_events);
    if(used+2<sizeof logged_events)std::snprintf(logged_events+used,sizeof logged_events-used,"%s\n",message);
}
static scs_result_t SCSAPIFUNC fake_event(scs_event_t,scs_telemetry_event_callback_t,scs_context_t) {return SCS_RESULT_ok;}
static scs_result_t SCSAPIFUNC fake_channel(const char *name,scs_u32_t,scs_value_type_t,scs_u32_t flags,scs_telemetry_channel_callback_t,scs_context_t) {
    if(!std::strcmp(name,"truck.input.throttle"))assert(flags==(SCS_TELEMETRY_CHANNEL_FLAG_each_frame|SCS_TELEMETRY_CHANNEL_FLAG_no_value));
    if(reject_optional_engine && !std::strcmp(name,"truck.engine.enabled"))return SCS_RESULT_not_found;
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

static DWORD WINAPI perf_test_worker(void *context) {
    auto stats=(roadtrip::PerfStats*)context;
    for(unsigned i=0;i<10000;++i)stats->record(1000,10000000);
    return 0;
}
static void test_performance() {
    roadtrip::Performance p;assert(p.initialize(true));assert(p.frequency>0 && p.now()>=p.epoch);
    auto frequency=p.frequency;p.frequency=10000000;
    assert(p.ns(1)==100 && p.ns(1234567)==123456700);p.frequency=frequency;
    auto &s=p.stats[(unsigned)roadtrip::PerfKind::Input];
    for(uint64_t ticks:{999u,1000u,10000u,50000u})s.record(ticks,10000000);
    auto v=s.snapshot();assert(v.count==4 && v.ticks==61999 && v.max_ticks==50000 && v.over_100us==3 && v.over_1ms==2 && v.over_5ms==1);
    auto &parallel=p.stats[(unsigned)roadtrip::PerfKind::ModeTotal];HANDLE threads[4];
    for(auto &thread:threads){thread=CreateThread(nullptr,0,perf_test_worker,&parallel,0,nullptr);assert(thread);}
    assert(WaitForMultipleObjects(4,threads,TRUE,INFINITE)==WAIT_OBJECT_0);
    for(auto thread:threads)assert(CloseHandle(thread));
    v=parallel.snapshot();assert(v.count==40000 && v.ticks==40000000 && v.max_ticks==1000 && v.over_100us==40000 && !v.over_1ms);
    {roadtrip::PerfScope scope(p,roadtrip::PerfKind::FrameEnd);}
    assert(p.stats[(unsigned)roadtrip::PerfKind::FrameEnd].snapshot().count==1);
    p.enabled=false;assert(p.now()==0);
    {roadtrip::PerfScope scope(p,roadtrip::PerfKind::FrameEnd);}
    assert(p.stats[(unsigned)roadtrip::PerfKind::FrameEnd].snapshot().count==1);
    std::puts("PASS: QPC nanosecond conversion, long-call buckets, 40000 concurrent aggregate updates, actual timed scope and disabled profiling.");
}
static unsigned hud_test_calls=0;
static void hud_test_enqueue(void *manager,const char **text,const char **secondary,int icon,
    const char **custom,int priority,int persistence,int category,uint16_t sound,void *callback,const unsigned char *group) {
    assert(manager && text && *text && std::strstr(*text,"<color value="));
    assert(secondary && !**secondary && custom && !**custom);
    assert(icon==11 && priority==3 && persistence==1 && category==2 && sound==0xffff && !callback);
    uint32_t key=0;std::memcpy(&key,group+4,4);assert(group[0]==1 && key==roadtrip::NativeHud::key);
    ++hud_test_calls;
}
static void test_native_hud() {
    for(uint32_t shift:{0u,0x5000u}) {
        auto memory=signature_fixture(shift);uintptr_t image=(uintptr_t)memory;
        auto put=[&](uint32_t at,const char *pattern) {
            roadtrip::InstructionPattern p;assert(p.parse(pattern));
            for(unsigned i=0;i<p.length;++i)memory[at+i]=p.fixed[i]?p.bytes[i]:0;
        };
        auto relative=[&](uint32_t at,unsigned displacement,unsigned length,uint32_t target) {
            int32_t value=(int32_t)target-(int32_t)(at+length);std::memcpy(memory+at+displacement,&value,4);
        };
        uint32_t show=0x2500+shift,expire=0x2600+shift,root=0x2700+shift,enqueue=0x2800+shift;
        uint32_t global=0x14000+shift,empty=global+0x20,fade=global+0x40;
        put(show,roadtrip::hud_show_pattern);relative(show+30,3,7,empty);relative(show+100,1,5,enqueue);
        put(enqueue,"44 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55 57 41 56 48 83 EC 60 80 79 38 00 4D 8B F0 4C 8B C2 49 63 E9 48 8B F9");
        put(expire,roadtrip::hud_expire_pattern);relative(expire+41,3,7,fade);relative(expire+52,3,7,fade);
        // Preserve the original short branches in the expiry function.
        memory[expire+16]=0x2e;memory[expire+22]=5;memory[expire+27]=6;memory[expire+33]=0xea;memory[expire+40]=0x16;
        put(root,roadtrip::hud_root_pattern);relative(root,3,7,global);relative(root+30,1,5,expire);
        uint32_t offset=0x90;std::memcpy(memory+root+15,&offset,4);memory[root+22]=0x38;
        offset=0x180;std::memcpy(memory+root+26,&offset,4);
        int64_t fade_us=700000;std::memcpy(memory+fade,&fade_us,8);
        const char *blank="";std::memcpy(memory+empty,&blank,8);
        roadtrip::HudSites sites;assert(roadtrip::resolve_hud_sites(image,sites));
        assert(sites.show==show && sites.expire==expire && sites.root==global && sites.empty_ref==empty);
        assert(sites.fade_ms==700 && sites.gameplay_offset==0x90 && sites.hud_offset==0x38 && sites.manager_offset==0x180);
        auto saved=memory[show];memory[show]=0xcc;assert(!roadtrip::resolve_hud_sites(image,sites));memory[show]=saved;
        std::memcpy(memory+0x3000+shift,memory+show,110);assert(!roadtrip::resolve_hud_sites(image,sites));
        std::memset(memory+0x3000+shift,0,110);assert(roadtrip::resolve_hud_sites(image,sites));
        relative(root+30,1,5,expire+1);assert(!roadtrip::resolve_hud_sites(image,sites));relative(root+30,1,5,expire);
        assert(roadtrip::resolve_hud_sites(image,sites));
        if(!shift) {
            roadtrip::NativeHud hud;assert(hud.initialize(image));assert(!hud.manager());
            unsigned char world[0x100]={},gameplay[0x80]={},manager[0x80]={},request[0x70]={};
            uintptr_t a=(uintptr_t)world,b=(uintptr_t)gameplay,c=0;
            // The manager is embedded in its owner; use a sufficiently large owner.
            unsigned char actual_owner[0x200+0x80]={};c=(uintptr_t)actual_owner;
            std::memcpy(memory+global,&a,8);std::memcpy(world+0x90,&b,8);std::memcpy(gameplay+0x38,&c,8);
            auto m=actual_owner+0x180;uintptr_t handler=(uintptr_t)manager,entries=(uintptr_t)request;
            std::memcpy(m+8,&handler,8);m[0x38]=1;std::memcpy(m+0x18,&entries,8);
            uint64_t one=1;std::memcpy(m+0x20,&one,8);assert(hud.manager()==(uintptr_t)m);
            // Execute the real keyed wrapper against a recording callee, never game code.
            memory[enqueue]=0x48;memory[enqueue+1]=0xb8;auto target=(uintptr_t)&hud_test_enqueue;
            std::memcpy(memory+enqueue+2,&target,8);memory[enqueue+10]=0xff;memory[enqueue+11]=0xe0;
            DWORD old=0;assert(VirtualProtect(memory,0x20000,PAGE_EXECUTE_READWRITE,&old));
            FlushInstructionCache(GetCurrentProcess(),memory,0x20000);
            for(unsigned i=0;i<1000;++i) {
                char text[256];roadtrip::notice_text(i%2?10:3,i%3==0,text,sizeof text);assert(hud.show((uintptr_t)m,text));
            }
            assert(hud_test_calls==1000);
            request[4]=1;auto key=roadtrip::NativeHud::key;std::memcpy(request+8,&key,4);
            hud.expire((uintptr_t)m);int64_t time=0;std::memcpy(&time,request+0x68,8);assert(time==fade_us);
            key=1;std::memcpy(request+8,&key,4);time=0;std::memcpy(request+0x68,&time,8);
            hud.expire((uintptr_t)m);std::memcpy(&time,request+0x68,8);assert(time==0); // unrelated request untouched
            // Drive the real runtime: baseline, rapid selection changes, hidden
            // pause, timeout including native fade, logging off and UI disabled.
            native_hud=hud;mode_notice.reset();hud_submitted=0;
            enabled=known_ford=foreground=true;paused=false;hud_enabled=true;hud_duration_ms=2500;
            mode_notice.observe(10);update_mode_notice(1000);assert(hud_test_calls==1000);
            mode_notice.observe(3);mode_notice.observe(roadtrip::high_power);update_mode_notice(1100);
            assert(hud_test_calls==1001 && hud_submitted==(uintptr_t)m && mode_notice.expires==3600);
            mode_notice.observe(0);update_mode_notice(1200);assert(hud_test_calls==1002 && mode_notice.expires==3700);
            update_mode_notice(2999);assert(mode_notice.visible);update_mode_notice(3000);assert(!mode_notice.visible && !hud_submitted);
            paused=true;mode_notice.observe(10);update_mode_notice(4000);assert(mode_notice.pending && hud_test_calls==1002);
            paused=false;logging_enabled=false;update_mode_notice(5000);assert(hud_test_calls==1003 && mode_notice.expires==7500);
            hud_enabled=false;update_mode_notice(5100);assert(!mode_notice.visible && !hud_submitted);
            mode_notice.observe(3);update_mode_notice(5200);assert(hud_test_calls==1003 && !mode_notice.pending);
            logging_enabled=true;hud_enabled=true;known_ford=false;foreground=false;paused=true;
            native_hud={};mode_notice.reset();
            a=0;std::memcpy(memory+global,&a,8);assert(!hud.manager());
        }
        VirtualFree(memory,0,MEM_RELEASE);
    }
    roadtrip::ModeNotice n;n.observe(10);assert(!n.pending);n.observe(10);assert(!n.pending);
    n.observe(3);n.observe(roadtrip::high_power);assert(n.pending && n.start(100,2500) && n.expires==2600);
    n.observe(0);assert(n.start(200,2500) && n.expires==2700);n.cancel();assert(!n.pending && !n.visible);
    char text[256];roadtrip::notice_text(42,false,text,sizeof text);assert(std::strstr(text,"Custom") && !std::strstr(text,"Off"));
    std::puts("PASS: optional HUD signatures relocate/refuse missing, ambiguous and inconsistent sites; executed native Win64 wrapper for 1000 keyed notices and key-only expiry; latest mode/timer and unknown label.");
}
int wmain(int argc,wchar_t **argv) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif
    std::setvbuf(stdout,nullptr,_IONBF,0);
    sdk_log_all=true; // assertions below observe diagnostic events through fake_log
    test_thunk_lifetime();
    test_signature_resolution();
    test_performance();
    test_native_hud();
    assert(argc==1 || argc==2);
    if(argc==2) {
        // Map PE sections as resources: no game code, imports or entrypoint run.
        HMODULE image=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
        assert(image);uintptr_t mapped=(uintptr_t)image&~(uintptr_t)3;
        assert(validate_sites(mapped));
        roadtrip::HudSites hud;assert(roadtrip::resolve_hud_sites(mapped,hud));
        std::puts("PASS: native HUD keyed show/expiry and live root signatures validated against the installed ATS image (not executed).");
        FreeLibrary(image);
        std::puts("PASS: all 20 native patch sites validated against the installed ATS image.");
    } else std::puts("SKIP: native addresses vs game image (no GameExe supplied).");

    module_handle=GetModuleHandleW(nullptr);
    scs_telemetry_init_params_v100_t init{};init.common.game_id="ats";init.common.log=fake_log;
    init.register_for_event=fake_event;init.register_for_channel=fake_channel;
    reject_optional_engine=true;
    assert(scs_telemetry_init(SCS_TELEMETRY_VERSION_CURRENT,&init)==SCS_RESULT_ok);
    assert(engine_registered==254 && !engine_valid && engine_value(0)==-1);reject_optional_engine=false;
    logged_events[0]=0;scs_value_t engine_test{};engine_test.type=SCS_VALUE_TYPE_bool;engine_test.value_bool.value=1;
    channel("truck.engine.enabled",SCS_U32_NIL,&engine_test,(void*)8);assert(engine_value(0)==1);
    engine_test.value_bool.value=0;channel("truck.engine.enabled",SCS_U32_NIL,&engine_test,(void*)8);
    assert(engine_value(0)==0 && std::strstr(logged_events,"signal=engine_enabled available=1 value=0.00000"));
    engine_test.value_bool.value=1;channel("truck.brake.motor",SCS_U32_NIL,&engine_test,(void*)9);assert(engine_value(1)==1);
    channel("truck.brake.motor",SCS_U32_NIL,nullptr,(void*)9);assert(engine_value(1)==-1);
    engine_test.type=SCS_VALUE_TYPE_u32;engine_test.value_u32.value=3;channel("truck.brake.retarder",SCS_U32_NIL,&engine_test,(void*)12);assert(engine_value(4)==3);
    engine_test.type=SCS_VALUE_TYPE_float;engine_test.value_float.value=0.7f;channel("truck.effective.clutch",SCS_U32_NIL,&engine_test,(void*)14);assert(engine_value(6)>0.69);
    engine_test.value_float.value=NAN;channel("truck.effective.clutch",SCS_U32_NIL,&engine_test,(void*)14);assert(engine_value(6)==-1);
    std::puts("PASS: optional engine-channel refusal leaves plugin alive; short engine/motor-brake edges logged immediately; retarder/clutch and unavailable/invalid signals remain distinct from zero.");
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
    game_home=L"keyboard_binding_fixture";controls_override.clear();selected_controls.clear();profile_log={};
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
    {   // Later output grows the log far past the former 256 KiB tail window.
        FILE *f=_wfopen((game_home+L"\\game.log.txt").c_str(),L"ab");assert(f);
        for(int i=0;i<8192;++i)std::fputs("[road_trip_shift] GEAR_CHOICE source=game_logic caller=direct stock_candidate=9\n",f);
        std::fclose(f);
    }
    reload_bindings=true;assert(!refresh_keyboard_binding(2000)); // keeps the next 1 s poll at 3000
    assert(keyboard_binding.valid && keyboard_binding.scans==std::vector<unsigned>({0x1e}));
    write_input_file(L"\\game.log.txt","/home/profiles/AB12/controls.sii\n/steam/profiles/CD34/quick_replies.sii\n");
    write_input_file(L"\\steam_profiles\\CD34\\controls.sii","mix dforward `keyboard.key1?0 | joy.b3?0`\n");
    refresh_keyboard_binding(3000);assert(keyboard_binding.valid && keyboard_binding.scans==std::vector<unsigned>({2}));
    write_input_file(L"\\steam_profiles\\CD34\\controls.sii","mix dforward `joy.b3?0`\n");
    refresh_keyboard_binding(4000);assert(!keyboard_binding.valid);
    write_input_file(L"\\game.log.txt","no identifiable active profile\n");
    refresh_keyboard_binding(5000);assert(!keyboard_binding.valid); // never retain stale profile selection
    controls_override=L"profiles\\AB12\\controls.sii";
    reload_bindings=true;refresh_keyboard_binding(5001);assert(keyboard_binding.valid);
    game_home=saved_home;controls_override.clear();selected_controls.clear();profile_log={};reload_bindings=true;
    std::puts("PASS: active local/Steam profile files, growing game log, live key rebinding, controller-only binding refusal and explicit controls override.");
    // Only lifecycle messages reach game.log.txt; driving diagnostics stay in the plugin log.
    sdk_log_all=false;logged_events[0]=0;
    trace("GEAR_CHOICE source=game_logic test=1");log("VEHICLE config=test");
    assert(!std::strstr(logged_events,"GEAR_CHOICE") && std::strstr(logged_events,"VEHICLE config=test"));
    sdk_log_all=true;
    std::puts("PASS: frequent diagnostics stay out of the game log; lifecycle messages still reach it.");
    // Main tap semantics with PR binding discovery; keyboard SDK confirmation is deferred.
    scope=1;foreground=true;brake=0;keyboard_binding.valid=true;
    auto input=[&](ULONGLONG now,float value,int held=-1) {
        scs_value_t v{};v.type=SCS_VALUE_TYPE_float;v.value_float.value=value;
        channel("truck.input.throttle",SCS_U32_NIL,&v,nullptr);process_input(now,held<0?value>=0.15f:held!=0);
    };
    cancel_input();input(10000,0);input(10010,0,1);assert(!kick);
    input(10020,0);input(10050,0,1);assert(kick); // no SDK ramp gating
    input(10060,0);assert(!kick);input(10070,1);assert(!kick);
    input(10080,0);input(10100,1);assert(kick);
    cancel_input();set_kickdown(false,"test_reset");
    pedal_sample=false;process_input(10200,true);assert(!kick);
    process_input(10220,false);process_input(10250,true);assert(kick && !pedal_sample);
    process_input(10260,false);assert(!kick); // deferred SDK confirmation must not gate main keyboard behavior
    activity(SCS_TELEMETRY_EVENT_paused,nullptr,nullptr);
    activity(SCS_TELEMETRY_EVENT_started,nullptr,nullptr);
    scope=1;foreground=true;
    input(11000,1);assert(!kick);input(11020,0);input(11050,1);assert(kick);
    brake=0.5f;input(11060,1);assert(!kick);brake=0;
    input(11070,1);assert(!kick);input(11080,0);input(11100,1);assert(!kick);
    input(11120,0);input(11140,1);assert(kick);
    foreground=false;input(11150,1);assert(!kick);foreground=true;
    input(11160,1);assert(!kick);input(11170,0);input(11180,1);assert(!kick);
    // Analog double press remains available independently of keyboard bindings.
    cancel_input();keyboard_binding.valid=false;input(20000,0,0);input(20010,0.8f,0);assert(!kick);
    input(20030,0,0);input(20060,0.8f,0);assert(kick && !strcmp(kick_origin,"pedal_double"));
    input(20080,0,0);assert(!kick);
    analog_enabled=false;input(20400,0.8f,0);input(20420,0,0);input(20450,0.8f,0);assert(!kick);analog_enabled=true;
    // Cruise-generated effective gas is never a physical analog tap.
    cancel_input();effective=1;input(20600,0,0);input(20620,0,0);assert(!kick);
    keyboard_binding.valid=true;pedal=effective=0;raw_keyboard_held=false;cancel_input();
    std::puts("PASS: mapped keyboard uses main double/hold/release and pause/brake/focus cancellation; analog double press retained; effective cruise throttle excluded.");
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
    assert(strstr(logged_events,"target_kmh=90.000000 speed_kmh=72.000000 error_kmh=18.000000"));
    assert(strstr(logged_events,"since_transition_ms=") && strstr(logged_events,"effective_brake="));
    assert(strstr(logged_events,"sdk_valid=1 keyboard_held=0") && strstr(logged_events,"guard_cap="));
    set_kickdown(true,"keyboard_accelerator_double");assert(read_mode(&setting)==roadtrip::high_power);
    update_cruise_acceleration();assert(!cruise_normal);set_kickdown(false,"released");assert(read_mode(&setting)==10);
    cruise=30;update_cruise_acceleration();assert(cruise_normal);
    speed=30;update_cruise_acceleration();assert(!cruise_normal && setting==10 && read_mode(&setting)==10);
    assert(strstr(logged_events,"reason=target_reached"));
    for(int i=0;i<100;++i) {
        speed=29.9999f;update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
        speed=30.0001f;update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
    }
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
    speed=29.5f;update_cruise_acceleration();assert(!cruise_normal && read_mode(&setting)==10);
    speed=20;
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
    logged_events[0]=0;sample_performance(GetTickCount64(),true);
    assert(std::strstr(logged_events,"PERF source=qpc") && std::strstr(logged_events,"unit=ns") && std::strstr(logged_events,"max_ns="));
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
