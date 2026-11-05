#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace roadtrip {
// Unpublished code is owned and freed. Published code may still be executing
// after restoration, so it stays committed until process exit, counted explicitly.
struct ThunkMemory {
    unsigned char *data=nullptr;
    size_t capacity=0,used=0,page_size=0,granularity=0;
    uint64_t allocated_bytes=0,freed_bytes=0,retained_bytes=0;
    unsigned allocations=0,frees=0,retained_blocks=0,free_failures=0;
    bool published=false;
    ThunkMemory() {SYSTEM_INFO s;GetSystemInfo(&s);page_size=s.dwPageSize;granularity=s.dwAllocationGranularity;}
    ThunkMemory(const ThunkMemory&)=delete;
    ThunkMemory& operator=(const ThunkMemory&)=delete;
    ~ThunkMemory(){reset();}
    uint64_t committed_bytes() const {return allocated_bytes-freed_bytes;}
    bool reset() {
        if(!data)return true;
        if(published){retained_bytes+=capacity;++retained_blocks;}
        else {
            if(!VirtualFree(data,0,MEM_RELEASE)){++free_failures;return false;}
            freed_bytes+=capacity;++frees;
        }
        data=nullptr;capacity=used=0;published=false;return true;
    }
    bool reserve_near(uintptr_t around) {
        if(!reset() || !page_size || !granularity)return false;
        // Relative CALL has a signed 32-bit displacement. Stay within half
        // that reach so any hook in this code region remains reachable.
        const uintptr_t reach=(uintptr_t)INT32_MAX/2;
        for(uintptr_t distance=granularity;distance<reach;distance+=granularity) {
            uintptr_t candidates[]={around>=distance?around-distance:0,
                around<=UINTPTR_MAX-distance?around+distance:0};
            for(uintptr_t address:candidates) {
                address-=address%granularity;if(!address)continue;
                auto p=(unsigned char*)VirtualAlloc((void*)address,page_size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
                if(p){data=p;capacity=page_size;allocated_bytes+=capacity;++allocations;return true;}
            }
        }
        return false;
    }
    unsigned char *take(size_t size) {
        if(!data || used>capacity || size>capacity-used)return nullptr;
        auto p=data+used;used+=size;return p;
    }
};
struct PreparedThunks {
    ThunkMemory &memory;
    bool keep=false;
    explicit PreparedThunks(ThunkMemory &m):memory(m){}
    ~PreparedThunks(){if(!keep)memory.reset();}
};
}
