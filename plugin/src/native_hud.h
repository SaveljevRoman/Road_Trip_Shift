// Optional native HUD notification path. No graphics/window hooks or EXE writes.
#pragma once
#include "native_scan.h"

namespace roadtrip {
inline constexpr const char *hud_show_pattern=
    "4C 8B DC 48 83 EC 68 49 8D 43 48 41 89 53 4C 49 89 43 E8 4D 8B D0 0F B7 84 24 A8 00 00 00 4C 8D 05 ?? ?? ?? ?? 49 C7 43 E0 00 00 00 00 49 8B D2 66 89 44 24 40 8B 84 24 A0 00 00 00 89 44 24 38 8B 84 24 98 00 00 00 C7 44 24 30 01 00 00 00 89 44 24 28 48 8B 84 24 90 00 00 00 49 89 43 B8 41 C6 43 48 01 E8 ?? ?? ?? ?? 48 83 C4 68 C3";
inline constexpr const char *hud_expire_pattern=
    "4C 6B 41 20 70 48 8B 41 18 4C 03 C0 49 3B C0 74 ?? 80 78 04 00 74 ?? 39 50 08 74 ?? 48 83 C0 70 EB ?? 48 83 78 68 00 7F ?? 48 8B 0D ?? ?? ?? ?? 48 89 48 60 48 8B 0D ?? ?? ?? ?? 48 89 48 68 C3";
inline constexpr const char *hud_root_pattern=
    "48 8B 05 ?? ?? ?? ?? BA 02 00 00 00 48 8B 88 ?? ?? ?? ?? 48 8B 49 ?? 48 81 C1 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 74 24 58 48 83 C4 40";
struct HudSites {
    uint32_t show=0,expire=0,root=0,empty_ref=0;
    uint32_t gameplay_offset=0,manager_offset=0;
    unsigned hud_offset=0,fade_ms=0;
};
inline bool resolve_hud_sites(uintptr_t base,HudSites &out) {
    out={};NativeImage image;if(!image.load(base))return false;
    HudSites found;InstructionPattern p;uint32_t root_code=0,enqueue=0,expire_call=0,fade=0,fade2=0;
    if(!p.parse(hud_show_pattern) || !find_unique(image,p,found.show) ||
       !relative_target(image,found.show+30,3,7,found.empty_ref,false) ||
       !relative_target(image,found.show+100,1,5,enqueue,true))return false;
    // Verify the keyed wrapper's callee too: this layout is part of the ABI.
    if(!p.parse("44 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55 57 41 56 48 83 EC 60 80 79 38 00 4D 8B F0 4C 8B C2 49 63 E9 48 8B F9") ||
       !image.contains(enqueue,p.length,true) || !p.matches((unsigned char*)(base+enqueue)))return false;
    if(!p.parse(hud_expire_pattern) || !find_unique(image,p,found.expire) ||
       !relative_target(image,found.expire+41,3,7,fade,false) ||
       !relative_target(image,found.expire+52,3,7,fade2,false) || fade!=fade2)return false;
    if(!p.parse(hud_root_pattern) || !find_unique(image,p,root_code) ||
       !relative_target(image,root_code,3,7,found.root,false) ||
       !relative_target(image,root_code+30,1,5,expire_call,true) || expire_call!=found.expire)return false;
    std::memcpy(&found.gameplay_offset,(void*)(base+root_code+15),4);
    found.hud_offset=*(unsigned char*)(base+root_code+22);
    std::memcpy(&found.manager_offset,(void*)(base+root_code+26),4);
    int64_t fade_us=0;std::memcpy(&fade_us,(void*)(base+fade),8);
    if(fade_us<=0 || fade_us>10000000 || !found.gameplay_offset || !found.hud_offset || !found.manager_offset ||
       found.gameplay_offset>0x100000 || found.manager_offset>0x100000)return false;
    found.fade_ms=(unsigned)((fade_us+999)/1000);out=found;return true;
}
inline bool hud_read(uintptr_t address,void *out,size_t bytes) {
    if(!address || !bytes || bytes>UINTPTR_MAX-address)return false;
    auto end=address+bytes;
    for(auto at=address;at<end;) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery((void*)at,&m,sizeof m) || m.State!=MEM_COMMIT ||
           (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || !(m.Protect&0xff) || (m.Protect&0xff)==PAGE_EXECUTE)return false;
        auto next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=at)return false;at=next;
    }
    std::memcpy(out,(void*)address,bytes);return true;
}
struct NativeHud {
    // Win64 ABI, derived from the native keyed-notification wrapper. Text is
    // copied into the game's own string; no std::string crosses the boundary.
    // The wrapper also writes an 8-byte optional-key scratch slot at entry
    // RSP+0x48. Reserve that ninth argument explicitly; omitting it corrupts
    // the caller's frame even though the slot has no incoming semantic value.
    using Show=void(*)(void*,uint32_t,const char**,int,const char**,int,int,uint16_t,uint64_t);
    using Expire=void(*)(void*,uint32_t);
    static constexpr uint32_t key=0x52545348; // Plugin-owned notification group.
    uintptr_t base=0;HudSites sites{};bool ready=false;
    bool initialize(uintptr_t image){base=image;return ready=resolve_hud_sites(image,sites);}
    uintptr_t manager() const {
        uintptr_t p=0;
        if(!ready || !hud_read(base+sites.root,&p,sizeof p) || !p || p>UINTPTR_MAX-sites.gameplay_offset ||
           !hud_read(p+sites.gameplay_offset,&p,sizeof p) || !p || p>UINTPTR_MAX-sites.hud_offset ||
           !hud_read(p+sites.hud_offset,&p,sizeof p) || !p || p>UINTPTR_MAX-sites.manager_offset)return 0;
        p+=sites.manager_offset;
        // Manager must be initialized, have a live HUD handler and an intact
        // bounded array before we hand control to a game function.
        unsigned char initialized=0;uintptr_t handler=0;uint64_t count=0;
        if(p>UINTPTR_MAX-0x60 || !hud_read(p+0x38,&initialized,1) || initialized!=1 ||
           !hud_read(p+8,&handler,8) || !handler || !hud_read(p+0x20,&count,8) || count>4096)return 0;
        return p;
    }
    bool show(uintptr_t manager,const char *text) const {
        const char *empty=nullptr;
        if(!ready || !manager || !hud_read(base+sites.empty_ref,&empty,sizeof empty) || !empty)return false;
        // Custom empty icon, lowest priority, persistent until our bounded timer,
        // announcement category, no sound or callback. Reuses one keyed request.
        ((Show)(base+sites.show))((void*)manager,key,&text,11,&empty,3,2,0xffff,0);
        return true;
    }
    void expire(uintptr_t manager) const {if(ready && manager)((Expire)(base+sites.expire))((void*)manager,key);}
};
}
