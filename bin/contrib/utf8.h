#ifndef WERC_UTF8_H
#define WERC_UTF8_H
#include <string>
namespace text {
using std::string;
inline bool validUtf8(const string &text) {
    for(size_t i=0;i<text.size();) {
        unsigned char c=text[i++];
        if(c<128) continue;
        unsigned remaining; unsigned value, minimum;
        if(c>=0xc2 && c<=0xdf) { remaining=1; value=c&31; minimum=0x80; }
        else if(c>=0xe0 && c<=0xef) { remaining=2; value=c&15; minimum=0x800; }
        else if(c>=0xf0 && c<=0xf4) { remaining=3; value=c&7; minimum=0x10000; }
        else return false;
        while(remaining--) {
            if(i==text.size()) return false;
            unsigned char next=text[i++];
            if((next&0xc0)!=0x80) return false;
            value=(value<<6)|(next&63);
        }
        if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
    }
    return true;
}
}
#endif
