// Resolve gearbox hooks from instruction signatures, independently of game version.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include "native_signatures.h"

namespace roadtrip {
struct NativeSites {
    uint32_t reads[11]{},compares[7]{},choices[2]{};
    uint32_t reader=0,chooser=0,variable=0;
};
inline bool image_readable(uintptr_t image,size_t offset,size_t length) {
    if(!image || !length || offset>UINTPTR_MAX-image || length>UINTPTR_MAX-(image+offset))return false;
    uintptr_t at=image+offset,end=at+length;
    while(at<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery((void*)at,&m,sizeof m) || (uintptr_t)m.AllocationBase!=image || m.State!=MEM_COMMIT ||
           (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || !(m.Protect&0xFF) || (m.Protect&0xFF)==PAGE_EXECUTE)return false;
        uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;
        if(next<=at)return false;
        at=next<end?next:end;
    }
    return true;
}
struct CodeRange { uint32_t rva=0,size=0; };
struct NativeImage {
    uintptr_t base=0;uint32_t size=0;CodeRange code[32]{};unsigned count=0;
    bool contains(uint32_t rva,size_t n,bool executable=false) const {
        if(!rva || rva>=size || n>size-rva || !image_readable(base,rva,n))return false;
        if(!executable)return true;
        for(unsigned i=0;i<count;i++)if(rva>=code[i].rva && rva-code[i].rva<code[i].size && n<=code[i].size-(rva-code[i].rva))return true;
        return false;
    }
    bool load(uintptr_t image) {
        *this={};base=image;
        if(!image_readable(base,0,sizeof(IMAGE_DOS_HEADER)))return false;
        auto dos=(const IMAGE_DOS_HEADER*)base;
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || !image_readable(base,dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64)))return false;
        auto nt=(const IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
           nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64) ||
           nt->FileHeader.NumberOfSections==0 || nt->FileHeader.NumberOfSections>96)return false;
        size=nt->OptionalHeader.SizeOfImage;
        size_t table=(size_t)dos->e_lfanew+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt->FileHeader.SizeOfOptionalHeader;
        size_t bytes=nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER);
        if(table>=size || bytes>size-table || !image_readable(base,table,bytes))return false;
        auto sections=(const IMAGE_SECTION_HEADER*)(base+table);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
            const auto &s=sections[i];if(!(s.Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
            if(count==32 || !s.Misc.VirtualSize || s.VirtualAddress>=size || s.Misc.VirtualSize>size-s.VirtualAddress ||
               !image_readable(base,s.VirtualAddress,s.Misc.VirtualSize))return false;
            code[count++]={s.VirtualAddress,s.Misc.VirtualSize};
        }
        return count!=0;
    }
};
struct InstructionPattern {
    unsigned char bytes[128]{};bool fixed[128]{};unsigned length=0,anchor=0,anchor_length=0;
    static int hex(char c) {return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:-1;}
    bool parse(const char *text) {
        *this={};
        while(*text) {
            if(length==128 || !text[1])return false;
            if(text[0]=='?' && text[1]=='?')fixed[length]=false;
            else {int a=hex(text[0]),b=hex(text[1]);if(a<0||b<0)return false;bytes[length]=(unsigned char)((a<<4)|b);fixed[length]=true;}
            ++length;text+=2;if(*text==' ')++text;else if(*text)return false;
        }
        for(unsigned i=0;i<length;) {
            if(!fixed[i]){++i;continue;}
            unsigned start=i;while(i<length && fixed[i])++i;
            if(i-start>anchor_length){anchor=start;anchor_length=i-start;}
        }
        return anchor_length!=0;
    }
    bool matches(const unsigned char *at) const {
        for(unsigned i=0;i<length;i++)if(fixed[i] && bytes[i]!=at[i])return false;
        return true;
    }
};
inline bool find_unique(const NativeImage &image,const InstructionPattern &pattern,uint32_t &rva) {
    bool found=false;
    for(unsigned s=0;s<image.count;s++) {
        const auto &range=image.code[s];if(range.size<pattern.length)continue;
        const auto begin=(const unsigned char*)(image.base+range.rva);
        const auto end=begin+range.size-pattern.length+1;
        auto at=begin;
        while(at<end) {
            auto hit=(const unsigned char*)std::memchr(at+pattern.anchor,pattern.bytes[pattern.anchor],(size_t)(end-at));
            if(!hit)break;
            at=hit-pattern.anchor;
            if(!std::memcmp(at+pattern.anchor,pattern.bytes+pattern.anchor,pattern.anchor_length) && pattern.matches(at)) {
                if(found)return false;
                rva=range.rva+(uint32_t)(at-begin);found=true;
            }
            ++at;
        }
    }
    return found;
}
inline bool relative_target(const NativeImage &image,uint32_t instruction,unsigned displacement,unsigned length,uint32_t &target,bool executable) {
    if(!image.contains(instruction,length,true) || displacement+4>length)return false;
    int32_t relative;std::memcpy(&relative,(void*)(image.base+instruction+displacement),4);
    int64_t result=(int64_t)instruction+length+relative;
    if(result<=0 || result>UINT32_MAX || !image.contains((uint32_t)result,32,executable))return false;
    target=(uint32_t)result;return true;
}
inline bool resolve_native_sites(uintptr_t base,NativeSites &result) {
    result={};NativeImage image;if(!image.load(base))return false;
    NativeSites found;unsigned ncompare=0;
    for(unsigned i=0;i<11;i++) {
        const auto &signature=read_signatures[i];InstructionPattern pattern;uint32_t start=0,reader=0,variable=0;
        if(!pattern.parse(signature.pattern) || !find_unique(image,pattern,start))return false;
        if(signature.call_offset+5>pattern.length || start>UINT32_MAX-signature.call_offset)return false;
        uint32_t call=start+signature.call_offset;
        if(*(unsigned char*)(base+call)!=0xE8 || !relative_target(image,call,1,5,reader,true) || !relative_target(image,start,3,7,variable,false))return false;
        if(i && (reader!=found.reader || variable!=found.variable))return false;
        found.reader=reader;found.variable=variable;found.reads[i]=call;
        if(signature.compare_offset>=0) {
            if(ncompare==7 || (unsigned)signature.compare_offset+7>pattern.length)return false;
            found.compares[ncompare++]=start+(unsigned)signature.compare_offset;
        }
    }
    if(ncompare!=7)return false;
    for(unsigned i=0;i<2;i++) {
        InstructionPattern pattern;uint32_t start=0,chooser=0;
        if(!pattern.parse(choice_signatures[i].pattern) || !find_unique(image,pattern,start) || !relative_target(image,start,1,5,chooser,true))return false;
        if(i && chooser!=found.chooser)return false;
        found.chooser=chooser;found.choices[i]=start;
    }
    // Each native site must be distinct before any process memory is changed.
    uint32_t all[20];unsigned used=0;
    for(auto r:found.reads)all[used++]=r;
    for(auto r:found.compares)all[used++]=r;
    for(auto r:found.choices)all[used++]=r;
    for(unsigned i=0;i<used;i++)for(unsigned j=0;j<i;j++) {
        unsigned ni=(i>=11 && i<18)?7:5,nj=(j>=11 && j<18)?7:5;
        if((uint64_t)all[i]<(uint64_t)all[j]+nj && (uint64_t)all[j]<(uint64_t)all[i]+ni)return false;
    }
    result=found;return true;
}
}
