#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace roadtrip {
// Owns only timestamp/PID-named .log/.csv files directly inside its directory.
// Reserve a whole segment before opening it, so growth never exceeds the quota.
class RollingLog {
    struct File {std::wstring path; ULONGLONG size,time;};
    std::wstring directory,prefix;
    std::string header;
    FILE *events=nullptr,*telemetry=nullptr;
    size_t quota=0,part_limit=0,part_bytes=0;
    unsigned part=0;
    bool active=false;

    static bool digit(wchar_t c){return c>=L'0' && c<=L'9';}
    bool prune() {
        WIN32_FIND_DATAW data{};
        HANDLE find=FindFirstFileW((directory+L"\\*").c_str(),&data);
        if(find==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
        std::vector<File> files;ULONGLONG total=0;
        do {
            if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))continue;
            if(!owned_name(data.cFileName))continue;
            ULONGLONG bytes=((ULONGLONG)data.nFileSizeHigh<<32)|data.nFileSizeLow;
            ULONGLONG time=((ULONGLONG)data.ftLastWriteTime.dwHighDateTime<<32)|data.ftLastWriteTime.dwLowDateTime;
            files.push_back({directory+L"\\"+data.cFileName,bytes,time});total+=bytes;
        } while(FindNextFileW(find,&data));
        DWORD error=GetLastError();FindClose(find);
        if(error!=ERROR_NO_MORE_FILES)return false;
        std::sort(files.begin(),files.end(),[](const File &a,const File &b){return a.time!=b.time?a.time<b.time:a.path<b.path;});
        const size_t available=quota-part_limit;
        for(const auto &file:files) {
            if(total<=available)break;
            if(DeleteFileW(file.path.c_str()))total-=file.size;
        }
        // Locked/unremovable files are never overwritten; stop disk logging.
        return total<=available;
    }
    bool open_part() {
        close();
        if(!prune())return false;
        std::wstring event_path,csv_path;
        for(;;) {
            if(part==~0u)return false;
            wchar_t suffix[32];swprintf(suffix,32,L"_part%06u",++part);
            auto stem=directory+L"\\"+prefix+suffix;
            event_path=stem+L".log";csv_path=stem+L".csv";
            if(GetFileAttributesW(event_path.c_str())==INVALID_FILE_ATTRIBUTES &&
               GetFileAttributesW(csv_path.c_str())==INVALID_FILE_ATTRIBUTES)break;
        }
        events=_wfopen(event_path.c_str(),L"wb");telemetry=_wfopen(csv_path.c_str(),L"wb");
        if(!events || !telemetry) {
            close();DeleteFileW(event_path.c_str());DeleteFileW(csv_path.c_str());return false;
        }
        part_bytes=std::fwrite(header.data(),1,header.size(),telemetry);
        if(part_bytes!=header.size()){close();return false;}
        active=true;flush();return true;
    }
    bool write(bool event,const char *text) {
        if(!active)return false;
        size_t size=std::strlen(text);
        if(size>part_limit-header.size())return false;
        if(part_bytes+size>part_limit && !open_part())return false;
        FILE *file=event?events:telemetry;
        size_t written=std::fwrite(text,1,size,file);part_bytes+=written;
        if(written!=size){close();return false;}
        if(event)std::fflush(events);
        return true;
    }
public:
    ~RollingLog(){close();}
    RollingLog()=default;
    RollingLog(const RollingLog&)=delete;
    RollingLog &operator=(const RollingLog&)=delete;
    static bool owned_name(const std::wstring &name) {
        if(name.size()<21)return false;
        auto extension=name.substr(name.size()-4);
        if(extension!=L".log" && extension!=L".csv")return false;
        for(size_t i=0;i<15;i++) {
            if(i==8){if(name[i]!=L'_')return false;}
            else if(!digit(name[i]))return false;
        }
        if(name[15]!=L'_')return false;
        size_t end=name.size()-4,i=16,start=i;
        while(i<end && digit(name[i]))++i;
        if(i==start)return false;
        if(i==end)return true; // legacy unsegmented session
        if(name.compare(i,5,L"_part")!=0)return false;
        i+=5;start=i;while(i<end && digit(name[i]))++i;
        return i>start && i==end;
    }
    bool begin(const std::wstring &folder,const std::wstring &session,const char *csv_header,
               size_t max_bytes,size_t segment_bytes=5u*1024*1024) {
        close();directory=folder;prefix=session;header=csv_header;quota=max_bytes;
        part=0;part_limit=std::min(segment_bytes,quota/2);
        if(part_limit<=header.size() || !owned_name(prefix+L"_part000001.csv"))return false;
        if(!CreateDirectoryW(directory.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        return open_part();
    }
    bool event(const char *text){return write(true,text);}
    bool sample(const char *text){return write(false,text);}
    void flush(){if(events)std::fflush(events);if(telemetry)std::fflush(telemetry);}
    void close() {
        if(events){std::fclose(events);events=nullptr;}
        if(telemetry){std::fclose(telemetry);telemetry=nullptr;}
        active=false;
    }
};
}
