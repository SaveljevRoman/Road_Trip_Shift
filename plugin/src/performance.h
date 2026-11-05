#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <limits>

namespace roadtrip {
enum class PerfKind:unsigned {FrameStart,FrameEnd,Channel,Input,Bindings,Cruise,Guard,Hud,Memory,LogIO,TelemetryIO,ModeTotal,ModeStock,ChoiceTotal,ChoiceStock,Init,Report,Count};
inline constexpr const char *perf_names[]={"frame_start","frame_end","sdk_channel","input","keyboard_binding_io","cruise_recovery","cruise_guard","hud","process_memory","event_log_io","csv_status_io","mode_hook_total","mode_stock_reader","chooser_hook_total","stock_chooser","initialization","timing_report"};
static_assert(sizeof(perf_names)/sizeof(perf_names[0])==(unsigned)PerfKind::Count,"Every timing section needs a name");
struct PerfSnapshot {uint64_t count=0,ticks=0,max_ticks=0,over_100us=0,over_1ms=0,over_5ms=0;};
struct PerfStats {
    std::atomic<uint64_t> count{0},ticks{0},max_ticks{0},over_100us{0},over_1ms{0},over_5ms{0};
    void record(uint64_t duration,uint64_t frequency) {
        ticks.fetch_add(duration,std::memory_order_relaxed);
        auto old=max_ticks.load(std::memory_order_relaxed);
        while(old<duration && !max_ticks.compare_exchange_weak(old,duration,std::memory_order_relaxed)){}
        if(duration>=frequency/10000+(frequency%10000!=0))over_100us.fetch_add(1,std::memory_order_relaxed);
        if(duration>=frequency/1000+(frequency%1000!=0))over_1ms.fetch_add(1,std::memory_order_relaxed);
        if(duration>=frequency/200+(frequency%200!=0))over_5ms.fetch_add(1,std::memory_order_relaxed);
        count.fetch_add(1,std::memory_order_relaxed);
    }
    PerfSnapshot snapshot() const {
        return {count.load(std::memory_order_relaxed),ticks.load(std::memory_order_relaxed),max_ticks.load(std::memory_order_relaxed),
            over_100us.load(std::memory_order_relaxed),over_1ms.load(std::memory_order_relaxed),over_5ms.load(std::memory_order_relaxed)};
    }
};
struct Performance {
    std::atomic<bool> enabled{false};
    uint64_t frequency=0,epoch=0;
    PerfStats stats[(unsigned)PerfKind::Count];
    // Clock origin/frequency and cumulative counters survive SDK reinit. No
    // reset races with a thread still returning through a published hook.
    bool initialize(bool requested) {
        if(!frequency) {
            LARGE_INTEGER f{},t{};
            if(!QueryPerformanceFrequency(&f) || f.QuadPart<=0 || !QueryPerformanceCounter(&t) || t.QuadPart<0){enabled=false;return false;}
            frequency=(uint64_t)f.QuadPart;epoch=(uint64_t)t.QuadPart;
        }
        enabled=requested;return requested;
    }
    uint64_t now() const {
        if(!enabled.load(std::memory_order_relaxed))return 0;
        LARGE_INTEGER t{};return QueryPerformanceCounter(&t) && t.QuadPart>=0?(uint64_t)t.QuadPart:0;
    }
    uint64_t ns(uint64_t ticks) const {
        if(!frequency)return 0;
        long double result=(long double)ticks*1000000000.0L/(long double)frequency;
        return result>=(long double)UINT64_MAX?UINT64_MAX:(uint64_t)result;
    }
    uint64_t elapsed_ns() const {auto t=now();return t>=epoch?ns(t-epoch):0;}
    void finish(PerfKind kind,uint64_t begin) {
        if(!begin)return;
        auto end=now();if(end>=begin)stats[(unsigned)kind].record(end-begin,frequency);
    }
};
struct PerfScope {
    Performance &perf;PerfKind kind;uint64_t begin;
    PerfScope(Performance &p,PerfKind k,bool sample=true):perf(p),kind(k),begin(sample?p.now():0){}
    ~PerfScope(){perf.finish(kind,begin);}
    PerfScope(const PerfScope&)=delete;PerfScope &operator=(const PerfScope&)=delete;
};
}
