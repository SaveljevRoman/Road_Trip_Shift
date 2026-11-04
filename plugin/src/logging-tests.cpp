#include "rolling_log.h"
#include <cassert>
#include <fstream>

static void touch(const std::wstring &path,const std::string &text) {
    FILE *f=_wfopen(path.c_str(),L"wb");assert(f);
    assert(std::fwrite(text.data(),1,text.size(),f)==text.size());std::fclose(f);
}
static unsigned long long used(const std::wstring &directory) {
    WIN32_FIND_DATAW d{};HANDLE f=FindFirstFileW((directory+L"\\*").c_str(),&d);
    assert(f!=INVALID_HANDLE_VALUE);unsigned long long total=0;
    do {
        if(!roadtrip::RollingLog::owned_name(d.cFileName))continue;
        HANDLE file=CreateFileW((directory+L"\\"+d.cFileName).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        assert(file!=INVALID_HANDLE_VALUE);LARGE_INTEGER size{};assert(GetFileSizeEx(file,&size));CloseHandle(file);total+=size.QuadPart;
    }while(FindNextFileW(f,&d));
    FindClose(f);return total;
}
int wmain() {
    assert(roadtrip::RollingLog::owned_name(L"20261104_120000_123.csv"));
    assert(roadtrip::RollingLog::owned_name(L"20261104_120000_123_part000002.log"));
    for(auto bad:{L"notes.log",L"20261104_120000_123.txt",L"20261104_120000_123_part.log",L"20261104_120000_.csv",L"20261104_120000_123.csv.bak",L"20261104_120000_123_part1x.log"})assert(!roadtrip::RollingLog::owned_name(bad));
    wchar_t current[MAX_PATH];assert(GetCurrentDirectoryW(MAX_PATH,current));
    auto folder=std::wstring(current)+L"\\rolling-log-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    assert(CreateDirectoryW(folder.c_str(),nullptr));
    auto legacy=folder+L"\\20000101_000000_1.csv";
    touch(legacy,std::string(20000,'x'));
    auto unrelated=folder+L"\\notes.log";touch(unrelated,"keep me");
    constexpr size_t quota=16384;
    const char *header="tick,value\n";
    roadtrip::RollingLog log;
    assert(log.begin(folder,L"20261104_120000_123",header,quota,4096));
    assert(GetFileAttributesW(legacy.c_str())==INVALID_FILE_ATTRIBUTES);
    for(unsigned i=0;i<400;i++) {
        auto row=std::to_string(i)+","+std::string(250,'x')+"\n";
        assert(log.sample(row.c_str()));
        assert(log.event("event\n"));log.flush();
        assert(used(folder)<=quota);
    }
    log.close();
    assert(GetFileAttributesW(unrelated.c_str())!=INVALID_FILE_ATTRIBUTES);
    WIN32_FIND_DATAW d{};HANDLE find=FindFirstFileW((folder+L"\\*.csv").c_str(),&d);assert(find!=INVALID_HANDLE_VALUE);
    bool newest=false;
    do {
        FILE *f=_wfopen((folder+L"\\"+d.cFileName).c_str(),L"rb");assert(f);
        char line[512];assert(std::fgets(line,sizeof line,f));assert(std::strcmp(line,header)==0);
        while(std::fgets(line,sizeof line,f))if(std::strncmp(line,"399,",4)==0)newest=true;
        std::fclose(f);
    }while(FindNextFileW(find,&d));FindClose(find);assert(newest);
    // Repeated init in the same process/second must not truncate an earlier part.
    auto first=folder+L"\\20261104_130000_123_part000001.csv";
    assert(log.begin(folder,L"20261104_130000_123",header,quota,4096));
    assert(log.sample("sentinel,1\n"));log.close();
    auto before=used(folder);
    assert(log.begin(folder,L"20261104_130000_123",header,quota,4096));log.close();
    assert(used(folder)>=before);
    FILE *f=_wfopen(first.c_str(),L"rb");assert(f);char contents[128]={};std::fread(contents,1,sizeof contents-1,f);std::fclose(f);assert(std::strstr(contents,"sentinel,1"));
    assert(log.begin(folder,L"20261104_140000_123",header,quota,4096));
    for(unsigned i=0;i<100;i++){auto event=std::to_string(i)+std::string(800,'e')+"\n";assert(log.event(event.c_str()));assert(used(folder)<=quota);}
    log.close();
    // An undeletable oversized file must stop logging instead of exceeding quota.
    auto lockedFolder=folder+L"\\locked";assert(CreateDirectoryW(lockedFolder.c_str(),nullptr));
    auto locked=lockedFolder+L"\\20000101_000000_1.csv";touch(locked,std::string(20000,'x'));
    HANDLE lock=CreateFileW(locked.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(lock!=INVALID_HANDLE_VALUE);
    assert(!log.begin(lockedFolder,L"20261104_120000_123",header,quota,4096));
    assert(!log.sample("ignored\n"));assert(used(lockedFolder)==20000);CloseHandle(lock);
    assert(log.begin(lockedFolder,L"20261104_120000_123",header,quota,4096));log.close();assert(used(lockedFolder)<=quota);
    std::puts("PASS: bounded event/CSV rotation, legacy cleanup, fresh headers, newest samples, collision avoidance, unrelated-file preservation and locked-file failure.");
    return 0;
}
