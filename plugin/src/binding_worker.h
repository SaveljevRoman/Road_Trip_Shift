#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <utility>
#include "keyboard_binding.h"

namespace roadtrip {
// Inputs for one active-profile/controls.sii discovery. After start() the
// worker owns its copy; the game thread never touches it again.
struct BindingSource {
    std::wstring game_home,controls_override;
    ProfileLog profile_log;
};
struct BindingLoad {KeyboardBinding binding;bool file_read=false;};

// Reads game.log.txt/controls.sii on its own thread. File I/O can stall for
// milliseconds (OS cache, scanners, the game's own disk work); frame callbacks
// therefore only post a request and take the finished result, never block on disk.
// Each worker has its own heap state shared by refcount and holds a module
// reference, so a thread still finishing a slow read after stop() timed out
// cannot touch a newer worker or run after the DLL is unmapped.
class BindingWorker {
public:
    using Loader=BindingLoad(*)(BindingSource&);
    inline static std::atomic<long> live_threads{0};

    bool running() const {return shared!=nullptr;}
    bool start(BindingSource source,Loader loader) {
        if(shared || !loader)return false;
        auto s=new Shared;s->loader=loader;s->source=std::move(source);
        s->wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!s->wake || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)&run,&s->module)) {
            if(s->wake)CloseHandle(s->wake);
            delete s;return false;
        }
        ++live_threads;
        thread=CreateThread(nullptr,0,run,s,0,nullptr);
        if(!thread){--live_threads;FreeLibrary(s->module);CloseHandle(s->wake);delete s;return false;}
        shared=s;return true;
    }
    // Coalesces: requests made while a read is running produce one more read.
    void request() {
        if(!shared)return;
        AcquireSRWLockExclusive(&shared->lock);shared->pending=true;ReleaseSRWLockExclusive(&shared->lock);
        SetEvent(shared->wake);
    }
    // Only the newest finished result is kept.
    bool take(BindingLoad &out) {
        if(!shared)return false;
        AcquireSRWLockExclusive(&shared->lock);
        bool ready=shared->ready;
        if(ready){out=std::move(shared->result);shared->result={};shared->ready=false;}
        ReleaseSRWLockExclusive(&shared->lock);
        return ready;
    }
    // Returns false if the thread is still inside a read after the timeout;
    // it then finishes on its own and releases only its own state.
    bool stop(DWORD timeout_ms) {
        if(!shared)return true;
        AcquireSRWLockExclusive(&shared->lock);shared->stop=true;ReleaseSRWLockExclusive(&shared->lock);
        SetEvent(shared->wake);
        bool exited=WaitForSingleObject(thread,timeout_ms)==WAIT_OBJECT_0;
        CloseHandle(thread);thread=nullptr;
        release(shared);shared=nullptr;
        return exited;
    }
private:
    struct Shared {
        volatile LONG refs=2; // worker thread + owner
        SRWLOCK lock=SRWLOCK_INIT;
        HANDLE wake=nullptr;
        HMODULE module=nullptr;
        bool stop=false,pending=false,ready=false;
        Loader loader=nullptr;
        BindingSource source;
        BindingLoad result;
    };
    Shared *shared=nullptr;
    HANDLE thread=nullptr;
    static void release(Shared *s) {
        if(InterlockedDecrement(&s->refs))return;
        CloseHandle(s->wake);delete s;
    }
    static DWORD WINAPI run(void *context) {
        auto s=(Shared*)context;
        for(;;) {
            WaitForSingleObject(s->wake,INFINITE);
            AcquireSRWLockExclusive(&s->lock);
            bool stop=s->stop,pending=s->pending;s->pending=false;
            ReleaseSRWLockExclusive(&s->lock);
            if(stop)break;
            if(!pending)continue;
            BindingLoad next=s->loader(s->source);
            AcquireSRWLockExclusive(&s->lock);
            s->result=std::move(next);s->ready=true;
            ReleaseSRWLockExclusive(&s->lock);
        }
        HMODULE module=s->module;
        release(s);--live_threads;
        FreeLibraryAndExitThread(module,0);
        return 0;
    }
};
}
