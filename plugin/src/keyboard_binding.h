#pragma once
#include <string>
#include <vector>
#include <cctype>

namespace roadtrip {
struct KeyboardBinding {
    std::vector<unsigned> scans;
    bool valid=false;
};
inline unsigned keyboard_scan(const std::string &name) {
    // Physical SCS keyboard names, translated through the active Windows layout.
    static const char *letters="qwertyuiopasdfghjklzxcvbnm";
    static const unsigned codes[]={0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,
        0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32};
    if(name.size()==1)for(unsigned i=0;letters[i];++i)if(name[0]==letters[i])return codes[i];
    if(name.size()==4 && name.compare(0,3,"key")==0 && name[3]>='0' && name[3]<='9')
        return name[3]=='0'?0x0b:unsigned(name[3]-'1')+2;
    if(name.size()>=2 && name[0]=='f') {
        unsigned n=0;for(size_t i=1;i<name.size();++i){if(!std::isdigit((unsigned char)name[i]))return 0;n=n*10+unsigned(name[i]-'0');if(n>12)return 0;}
        if(n>=1 && n<=10)return 0x3a+n;
        if(n==11)return 0x57;
        if(n==12)return 0x58;
    }
    struct Named {const char *name;unsigned scan;};
    static const Named named[]={{"space",0x39},{"tab",0x0f},{"backspace",0x0e},{"esc",1},
        {"lshift",0x2a},{"rshift",0x36},{"lctrl",0x1d},{"rctrl",0xe01d},{"lalt",0x38},{"ralt",0xe038},
        {"uarrow",0xe048},{"darrow",0xe050},{"larrow",0xe04b},{"rarrow",0xe04d},
        {"home",0xe047},{"end",0xe04f},{"pgup",0xe049},{"pgdn",0xe051},{"ins",0xe052},{"del",0xe053},
        {"grave",0x29},{"minus",0x0c},{"equal",0x0d},{"lbracket",0x1a},{"rbracket",0x1b},
        {"semicolon",0x27},{"apostrophe",0x28},{"backslash",0x2b},{"comma",0x33},{"period",0x34},{"slash",0x35}};
    for(const auto &entry:named)if(name==entry.name)return entry.scan;
    // Enter/numpad aliases are deliberately unsupported: virtual-key polling
    // cannot reliably distinguish both physical Enter keys or NumLock modes.
    return 0;
}
inline KeyboardBinding parse_keyboard_binding(const std::string &text) {
    KeyboardBinding result;size_t cursor=0;bool found=false;
    while(cursor<text.size()) {
        size_t end=text.find('\n',cursor);if(end==std::string::npos)end=text.size();
        std::string line=text.substr(cursor,end-cursor);cursor=end+1;
        auto pos=line.find("mix dforward ");if(pos==std::string::npos)continue;
        if(found)return {};
        found=true;
        auto begin=line.find('`',pos),finish=begin==std::string::npos?begin:line.find('`',begin+1);
        if(begin==std::string::npos || finish==std::string::npos)return {};
        std::string expr=line.substr(begin+1,finish-begin-1);size_t part=0;
        auto last_char=expr.find_last_not_of(" \t\r");
        if(last_char==std::string::npos || expr[last_char]=='|')return {};
        while(part<expr.size()) {
            auto last=expr.find('|',part);if(last==std::string::npos)last=expr.size();
            std::string token=expr.substr(part,last-part);part=last+1;
            auto first=token.find_first_not_of(" \t\r"),tail=token.find_last_not_of(" \t\r");
            if(first==std::string::npos)return {};
            token=token.substr(first,tail-first+1);
            if(token=="unbound")continue;
            if(token.size()>2 && token.compare(token.size()-2,2,"?0")==0)token.resize(token.size()-2);
            if(token.compare(0,9,"keyboard.")==0) {
                auto scan=keyboard_scan(token.substr(9));if(!scan || result.scans.size()>=8)return {};
                result.scans.push_back(scan);
            } else {
                // Ignore only simple OR'ed controller buttons. Reject arbitrary
                // mixes/functions/modifiers instead of guessing their meaning.
                auto dot=token.find('.');
                if(dot==std::string::npos || token.compare(0,3,"joy") || token.compare(dot,2,".b"))return {};
                for(size_t i=3;i<dot;++i)if(!std::isdigit((unsigned char)token[i]))return {};
                if(dot+2==token.size())return {};
                for(size_t i=dot+2;i<token.size();++i)if(!std::isdigit((unsigned char)token[i]))return {};
            }
        }
    }
    result.valid=found && !result.scans.empty();return result;
}
struct ProfileControls {std::string id;bool steam=false;};
inline ProfileControls last_profile_controls(const std::string &log) {
    ProfileControls result;size_t newest=0;bool found=false;
    const char *prefixes[]={"/home/profiles/","/home/steam_profiles/","/steam/profiles/"};
    for(unsigned i=0;i<3;++i) {
        size_t pos=0;
        while((pos=log.find(prefixes[i],pos))!=std::string::npos) {
            size_t begin=pos+std::char_traits<char>::length(prefixes[i]),end=log.find('/',begin);
            if(end!=std::string::npos && end>begin && end-begin<=128) {
                std::string id=log.substr(begin,end-begin);bool hex=true;
                for(char c:id)if(!std::isxdigit((unsigned char)c))hex=false;
                if(hex && (!found || pos>=newest)){result={id,i!=0};newest=pos;found=true;}
            }
            ++pos;
        }
    }
    return result;
}
}
