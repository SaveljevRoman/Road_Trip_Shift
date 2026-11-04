#include "road_trip_shift.cpp"
#include <cassert>

static char logged_events[16384]={};
static void SCSAPIFUNC fake_log(scs_log_type_t,const char *message) {
    size_t used=std::strlen(logged_events);
    if(used+2<sizeof logged_events)std::snprintf(logged_events+used,sizeof logged_events-used,"%s\n",message);
}
static scs_result_t SCSAPIFUNC fake_event(scs_event_t,scs_telemetry_event_callback_t,scs_context_t) {return SCS_RESULT_ok;}
static scs_result_t SCSAPIFUNC fake_channel(const char*,scs_u32_t,scs_value_type_t,scs_u32_t,scs_telemetry_channel_callback_t,scs_context_t) {return SCS_RESULT_ok;}
static float fake_reader(void *p) {return *(float*)p;}
static int fake_chooser(void *p) {return *(int*)p;}


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
    patched=false;paused=true;
    float setting=10;original_reader=fake_reader;scope=1;kick=0;
    assert(read_mode(&setting)==10);kick=1;assert(read_mode(&setting)==roadtrip::high_power);
    setting=3;kick=0;assert(read_mode(&setting)==3); // reads the current selection, never a stale cached mode
    scope=0;kick=1;assert(read_mode(&setting)==3);
    assert(reader_calls==4 && shadow_reads==1);
    std::puts("PASS: SDK configuration scope and native mode shadow/restoration.");

    logged_events[0]=0;adaptive_var=&setting;mode_seen=false;setting=10;scope=1;kick=0;
    refresh_config_mode();assert(strstr(logged_events,"CONFIG_MODE_INITIAL source=game_setting"));
    set_kickdown(true,"W_double");assert(setting==10);
    assert(strstr(logged_events,"KICKDOWN ON source=plugin"));
    assert(!strstr(logged_events,"CONFIG_MODE_CHANGED")); // a plugin request never masquerades as a user setting change
    setting=roadtrip::high_power;refresh_config_mode();
    assert(strstr(logged_events,"CONFIG_MODE_CHANGED source=game_setting"));
    assert(!roadtrip::mode_override(setting,true,true,power_mode)); // user selected the same value separately
    set_kickdown(false,"input_release");assert(setting==roadtrip::high_power);
    assert(strstr(logged_events,"KICKDOWN OFF source=plugin"));
    logged_events[0]=0;
    set_kickdown(true,"W_double");
    assert(strstr(logged_events,"value_changed=0")); // configured High Power + double W: request with no mode-value change
    assert(!strstr(logged_events,"CONFIG_MODE_CHANGED"));
    set_kickdown(false,"input_release");
    std::puts("PASS: configured High Power vs plugin kickdown have distinct event sources, including identical mode values.");

    original_chooser=fake_chooser;int stock_choice=10;scope=1;kick=0;cruise_cap=9;
    assert(choose_gear(&stock_choice)==9);assert(cruise_clamps==1);
    stock_choice=7;assert(choose_gear(&stock_choice)==7);
    stock_choice=0;assert(choose_gear(&stock_choice)==0);
    stock_choice=-1;assert(choose_gear(&stock_choice)==-1);
    stock_choice=10;kick=1;assert(choose_gear(&stock_choice)==10);
    scope=0;kick=0;assert(choose_gear(&stock_choice)==10);
    scope=1;assert(read_mode(&setting)==setting); // cruise hold does not select a different transmission mode
    reset_cruise_guard("test_complete");original_chooser=nullptr;
    std::puts("PASS: native chooser ceiling preserves stock downshifts, neutral, reverse and kickdown, without changing mode.");

    thunks=(unsigned char*)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(thunks);thunk_used=0;
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
    VirtualFree(thunks,0,MEM_RELEASE);thunks=nullptr;
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
    set_kickdown(true,"W_double");assert(read_mode(&setting)==roadtrip::high_power);
    last_sample=0;unsigned count=sample_count;frame_end(0,nullptr,nullptr);assert(sample_count==count);
    set_kickdown(false,"input_release");assert(read_mode(&setting)==10);assert(logged_events[0]==0);
    status=_wfopen(status_path,L"rb");assert(status);char snapshot[16]={};std::fread(snapshot,1,sizeof snapshot-1,status);std::fclose(status);
    assert(std::strcmp(snapshot,"sentinel")==0);logging_enabled=true;
    std::puts("PASS: disabled diagnostics stop file/status/SDK output without disabling kickdown and mode restoration.");
    original_reader=nullptr;adaptive_var=nullptr;scope=0;kick=0;frame_end(0,nullptr,nullptr);scs_telemetry_shutdown();
    std::puts("PASS: reversible patch restoration on isolated test memory; telemetry/log shutdown.");
    return 0;
}
