// Native Markdown renderer for werc. Build with: make -C bin/contrib md2html
// UTF-8 is preserved as bytes; no locale-dependent conversion is performed.
#include "metadata.h"
#include "callouts.h"
#include "footnotes.h"
#include "toc.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using std::string;
using Lines = std::vector<string>;
static bool space(char c) { return std::isspace(static_cast<unsigned char>(c)); }
static string trim(string s) {
    auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == string::npos ? "" : s.substr(a, b-a+1);
}
static string lower(string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Scan an entity in place, without copying or matching the remaining document.
static size_t entityEnd(const string &s, size_t start) {
    size_t i = start + 1;
    if (i == s.size()) return start;
    if (s[i] == '#') {
        ++i;
        bool hex = i < s.size() && (s[i] == 'x' || s[i] == 'X');
        if (hex) ++i;
        size_t first = i;
        while (i < s.size() && (hex ? std::isxdigit(static_cast<unsigned char>(s[i])) :
                                     std::isdigit(static_cast<unsigned char>(s[i])))) ++i;
        if (i == first) return start;
    } else {
        if (!std::isalpha(static_cast<unsigned char>(s[i]))) return start;
        while (i < s.size() && std::isalnum(static_cast<unsigned char>(s[i]))) ++i;
    }
    return i < s.size() && s[i] == ';' ? i + 1 : start;
}
static string escape(const string &s, bool entities = false) {
    string out;
    for (size_t i=0; i<s.size(); ++i) {
        if (s[i]=='&' && entities) {
            size_t end = entityEnd(s, i);
            if (end > i) { out.append(s, i, end-i); i=end-1; continue; }
        }
        switch(s[i]) {
        case '&': out+="&amp;"; break;
        case '<': out+="&lt;"; break;
        case '>': out+="&gt;"; break;
        case '"': out+="&quot;"; break;
        default: out+=s[i];
        }
    }
    return out;
}
static Lines lines(const string &s) {
    Lines out; std::istringstream in(s); string line;
    while (std::getline(in,line)) out.push_back(line);
    return out;
}
static size_t indent(const string &s) { return s.find_first_not_of(' ')==string::npos ? s.size() : s.find_first_not_of(' '); }
static string normalize(const string &s) {
    string out; size_t col=0;
    for (size_t i=0;i<s.size();++i) {
        char c=s[i];
        if(c=='\r') { if(i+1<s.size() && s[i+1]=='\n') ++i; c='\n'; }
        if(c=='\t') { size_t n=4-col%4; out.append(n,' '); col+=n; }
        else { out+=c; if(c=='\n') col=0; else ++col; }
    }
    return out;
}

// this is used so that links are displayed nicely.
struct Link { string url, title; };
class Markdown {
    std::map<string,Link> refs;
    footnotes::Document notes;
    toc::Document outline;
    string suffix=" />";
    bool mathEnabled=false;
    static constexpr unsigned maxDepth=64;
    mutable size_t workRemaining=0;
    void spend(size_t amount=1) const {
        if (amount > workRemaining) throw std::runtime_error("document exceeds parsing work limit");
        workRemaining -= amount;
    }
    size_t closing(const string &s, size_t start, char open, char close) const {
        size_t depth=1;
        for (size_t i=start; i<s.size(); ++i) {
            spend();
            if (s[i]=='\\' && i+1<s.size()) { ++i; continue; }
            if (s[i]==open) ++depth;
            if (s[i]==close && --depth==0) return i;
        }
        return string::npos;
    }
    struct MathDelimiters {
        std::vector<size_t> single, display, newline;
    };
    struct MathSpan {
        size_t content, end;
        bool display;
    };

    static bool escapedAt(const string &text, size_t pos) {
        size_t backslashes = 0;
        while (pos > 0 && text[--pos] == '\\') ++backslashes;
        return backslashes % 2 != 0;
    }

    static char32_t codepointAt(const string &text, size_t pos) {
        if (pos >= text.size()) return 0;
        unsigned char first = text[pos];
        if (first < 0x80) return first;
        size_t width = first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
        if (pos + width > text.size()) return first;
        char32_t cp = first & (0x7F >> width);
        for (size_t i = 1; i < width; ++i) {
            unsigned char next = text[pos + i];
            if ((next & 0xC0) != 0x80) return first;
            cp = (cp << 6) | (next & 0x3F);
        }
        return cp;
    }

    static bool mathSpace(const string &text, size_t pos) {
        char32_t cp = codepointAt(text, pos);
        return (cp >= 9 && cp <= 13) || cp == 0x20 || cp == 0x85 || cp == 0xA0 ||
               cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
               cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
    }

    static bool mathDigit(const string &text, size_t pos) {
        char32_t cp = codepointAt(text, pos);
        // Unicode decimal digits, grouped in consecutive sets of ten.
        static constexpr char32_t decimalStarts[] = {
            0x30, 0x660, 0x6F0, 0x7C0, 0x966, 0x9E6, 0xA66, 0xAE6,
            0xB66, 0xBE6, 0xC66, 0xCE6, 0xD66, 0xDE6, 0xE50, 0xED0,
            0xF20, 0x1040, 0x1090, 0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80,
            0x1A90, 0x1B50, 0x1BB0, 0x1C40, 0x1C50, 0xA620, 0xA8D0, 0xA900,
            0xA9D0, 0xA9F0, 0xAA50, 0xABF0, 0xFF10, 0x104A0, 0x10D30, 0x10D40,
            0x11066, 0x110F0, 0x11136, 0x111D0, 0x112F0, 0x11450, 0x114D0, 0x11650,
            0x116C0, 0x116D0, 0x116DA, 0x11730, 0x118E0, 0x11950, 0x11BF0, 0x11C50,
            0x11D50, 0x11DA0, 0x11F50, 0x16130, 0x16A60, 0x16AC0, 0x16B50, 0x16D70,
            0x1CCF0, 0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC, 0x1D7F6, 0x1E140, 0x1E2F0,
            0x1E4F0, 0x1E5F1, 0x1E950, 0x1FBF0,
        };
        for (char32_t start : decimalStarts) if (cp >= start && cp < start + 10) return true;
        return false;
    }

    static size_t previousCodepoint(const string &text, size_t pos) {
        --pos;
        while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) --pos;
        return pos;
    }

    MathDelimiters mathDelimiters(const string &text) const {
        MathDelimiters result;
        if (!mathEnabled || text.find('$') == string::npos) return result;
        size_t backslashes = 0;
        for (size_t i=0; i<text.size(); ++i) {
            if (text[i]=='\n') result.newline.push_back(i);
            if (text[i]=='$' && backslashes%2==0) {
                if (text.compare(i,2,"$$")==0) result.display.push_back(i);
                if (i>0 && !mathSpace(text,previousCodepoint(text,i)) && !mathDigit(text,i+1))
                    result.single.push_back(i);
            }
            backslashes = text[i]=='\\' ? backslashes+1 : 0;
        }
        return result;
    }

    // Recognize delimiters before interpreting any Markdown inside an equation.
    std::optional<MathSpan> mathAt(const string &text, size_t pos, const MathDelimiters *index=nullptr) const {
        if (!mathEnabled || text[pos] != '$' || escapedAt(text, pos)) return {};
        bool display = text.compare(pos, 2, "$$") == 0;
        size_t width = display ? 2 : 1;
        size_t content = pos + width;
        if (content == text.size() || (!display && mathSpace(text, content))) return {};
        if (index) {
            const auto &closers = display ? index->display : index->single;
            auto end = std::upper_bound(closers.begin(),closers.end(),content);
            if (end == closers.end()) return {};
            if (!display) {
                auto newline = std::lower_bound(index->newline.begin(),index->newline.end(),content);
                if (newline != index->newline.end() && *newline < *end) return {};
            }
            return MathSpan{content,*end+width,display};
        }
        for (size_t end = content; end < text.size(); ++end) {
            spend();
            if (!display && text[end] == '\n') break;
            if (text[end] != '$' || escapedAt(text, end)) continue;
            if (display && text.compare(end, 2, "$$") != 0) continue;
            if (end == content) continue;
            if (!display && (mathSpace(text, previousCodepoint(text, end)) || mathDigit(text, end + 1))) continue;
            return MathSpan{content, end + width, display};
        }
        return {};
    }

    static string mathHTML(const string &text, const MathSpan &math) {
        size_t width = math.display ? 2 : 1;
        string tex = escape(text.substr(math.content, math.end - width - math.content));
        return math.display ? "<div class=\"werc-math\">\\[" + tex + "\\]</div>"
                            : "<span class=\"werc-math\">\\(" + tex + "\\)</span>";
    }

    // HTML attributes, comments and code-like elements are opaque to math parsing.
    size_t htmlEnd(const string &text, size_t start) const {
        if (text.compare(start, 4, "<!--") == 0) {
            spend(text.size()-start);
            size_t end = text.find("-->", start + 4);
            return end == string::npos ? text.size() : end + 3;
        }
        char quote = 0;
        for (size_t i = start + 1; i < text.size(); ++i) {
            spend();
            if (quote) { if (text[i] == quote) quote = 0; }
            else if (text[i] == '\'' || text[i] == '"') quote = text[i];
            else if (text[i] == '>') return i + 1;
        }
        return start;
    }

    size_t rawElementEnd(const string &text, size_t start, size_t tagEnd) const {
        size_t nameEnd = start + 1;
        while (nameEnd < tagEnd && std::isalpha(static_cast<unsigned char>(text[nameEnd]))) ++nameEnd;
        string name = lower(text.substr(start + 1, nameEnd - start - 1));
        if (name != "pre" && name != "code" && name != "script" &&
            name != "style" && name != "textarea") return tagEnd;
        spend(text.size());
        size_t close = lower(text).find("</" + name, tagEnd);
        if (close == string::npos) return text.size();
        size_t end = htmlEnd(text, close);
        return end == close ? text.size() : end;
    }

    string rawMath(const string &text) const {
        string out;
        for (size_t i = 0; i < text.size();) {
            if (text[i] == '<') {
                size_t end = htmlEnd(text, i);
                if (end > i) {
                    end = rawElementEnd(text, i, end);
                    out.append(text, i, end - i);
                    i = end;
                    continue;
                }
            }
            if (auto math = mathAt(text, i)) {
                out += mathHTML(text, *math);
                i = math->end;
            } else out += text[i++];
        }
        return out;
    }

    // A standalone display block can contain blank lines and Markdown punctuation.
    std::optional<string> displayBlock(const Lines &src, size_t &line) const {
        if (!mathEnabled || indent(src[line]) > 3 || trim(src[line]).rfind("$$", 0) != 0) return {};
        string text = src[line];
        size_t start = indent(text);
        size_t endLine = line;
        size_t search = start + 2;
        while (true) {
            size_t end = text.find("$$", search);
            if (end != string::npos && !escapedAt(text, end)) {
                auto math = mathAt(text, start);
                if (!math || !trim(text.substr(math->end)).empty()) return {};
                line = endLine + 1;
                return mathHTML(text, *math);
            }
            if (end != string::npos) { search = end + 2; continue; }
            if (++endLine == src.size()) return {};
            search = text.size();
            spend(src[endLine].size()+1);
            text += '\n' + src[endLine];
        }
    }

    static string refID(string s) {
        return lower(std::regex_replace(trim(s),std::regex(R"(\s+)")," "));
    }
    static std::optional<Link> target(string s) {
        s=trim(s); if(s.empty()) return Link{};
        Link out; size_t end=0;
        if(s[0]=='<') { end=s.find('>'); if(end==string::npos) return {}; out.url=s.substr(1,end-1); ++end; }
        else { int depth=0; while(end<s.size() && (!space(s[end])||depth)) { if(s[end]=='(') ++depth; if(s[end]==')') --depth; ++end; } out.url=s.substr(0,end); }
        string title=trim(s.substr(end));
        if(!title.empty()) {
            if(title.size()<2 || !((title[0]=='"'&&title.back()=='"')||(title[0]=='\''&&title.back()=='\'')||(title[0]=='('&&title.back()==')'))) return {};
            out.title=title.substr(1,title.size()-2);
        }
        return out;
    }
    // Definition-looking lines inside a multiline code span are still code.
    // Continue scanning after its closer, since another span may start there.
    void skipMultilineCode(const Lines &src, size_t &line) const {
        if (src[line].find('`') == string::npos) return;
        size_t pos = 0;
        while (pos < src[line].size()) {
            if (src[line][pos] != '`' || escapedAt(src[line], pos)) {
                size_t next = skipInlineToken(src[line], pos);
                pos = next > pos ? next : pos + 1;
                continue;
            }
            size_t width = 1;
            while (pos + width < src[line].size() && src[line][pos + width] == '`') ++width;
            size_t endLine = line, search = pos + width, end = string::npos;
            while (endLine < src.size() && !trim(src[endLine]).empty()) {
                while ((end = src[endLine].find(string(width, '`'), search)) != string::npos) {
                    if ((end == 0 || src[endLine][end - 1] != '`') &&
                        (end + width == src[endLine].size() || src[endLine][end + width] != '`')) break;
                    search = end + 1;
                }
                if (end != string::npos) break;
                ++endLine;
                search = 0;
            }
            if (end == string::npos) pos += width;
            else { line = endLine; pos = end + width; }
        }
    }

    void definitions(Lines &src) {
        static const std::regex def(R"(^ {0,3}\[([^\]]+)\]:\s*(.*)$)");
        bool fenced=false, comment=false; char fence=0; size_t count=0; string rawTag;
        for(size_t i=0;i<src.size();++i) {
            string t=trim(src[i]);
            if(!fenced) {
                if(comment || t.rfind("<!--",0)==0) {
                    comment=t.find("-->")==string::npos; continue;
                }
                if (rawTag.empty() && t.size() >= 5 && lower(t.substr(0, 5)) == "<code" &&
                    (t.size() == 5 || t[5] == '>' || t[5] == '/' || space(t[5]))) rawTag = "code";
                if(!rawTag.empty() || rawStart(src[i],rawTag)) {
                    if(rawTag=="hr" || trim(src[i]).find("/>")!=string::npos || lower(src[i]).find("</"+rawTag+">")!=string::npos) rawTag.clear();
                    continue;
                }
            }
            if (!fenced) {
                size_t next = i;
                if (displayBlock(src, next)) { i = next - 1; continue; }
            }
            if(indent(src[i]) <= 3 && t.size()>=3 && (t[0]=='`'||t[0]=='~')) {
                size_t n=t.find_first_not_of(t[0]); if(n==string::npos) n=t.size();
                if(n>=3) {
                    if(!fenced) { fenced=true; fence=t[0]; count=n; }
                    else if(t[0]==fence && n>=count && trim(t.substr(n)).empty()) fenced=false;
                    continue;
                }
            }
            if(fenced) continue;
            if (notes.extract(src, i)) continue;
            std::smatch m;
            if(std::regex_match(src[i],m,def)) {
                string raw=m[2]; size_t consumed=0;
                if(trim(raw).empty() && i+1<src.size()) { raw=trim(src[i+1]); consumed=1; }
                auto link=target(raw);
                if(link && link->title.empty() && i+consumed+1<src.size()) {
                    string next=trim(src[i+consumed+1]);
                    if(!next.empty() && (next[0]=='"'||next[0]=='\''||next[0]=='(')) {
                        auto full=target(raw+" "+next); if(full) { link=full; ++consumed; }
                    }
                }
                if(link) { refs[refID(m[1])]=*link; for(size_t n=0;n<=consumed;++n) src[i+n].clear(); i+=consumed; }
            } else skipMultilineCode(src, i);
        }
    }
    // Link labels and emphasis delimiters must not end inside code or TeX.
    size_t skipInlineToken(const string &text, size_t pos, const MathDelimiters *index=nullptr) const {
        if (auto math = mathAt(text, pos,index)) return math->end;
        std::size_t noteEnd = notes.referenceEnd(text, pos);
        if (noteEnd > pos) return noteEnd;
        if (text[pos] == '`') {
            size_t width = 1;
            while (pos + width < text.size() && text[pos + width] == '`') ++width;
            size_t end = pos + width;
            spend(text.size()-end);
            while ((end = text.find(string(width, '`'), end)) != string::npos) {
                spend();
                if (text[end - 1] != '`' && (end + width == text.size() || text[end + width] != '`')) return end + width;
                ++end;
            }
        }
        if (text[pos] == '<' && pos + 1 < text.size() &&
            (std::isalpha(static_cast<unsigned char>(text[pos + 1])) || string("/!?").find(text[pos + 1]) != string::npos)) {
            size_t end = htmlEnd(text, pos);
            if (end > pos) return rawElementEnd(text, pos, end);
        }
        return pos;
    }

    // Pair brackets once per inline region. Failed openers never rescan a suffix.
    std::vector<size_t> labelEnds(const string &text, const MathDelimiters &index) const {
        std::vector<size_t> ends(text.size(), string::npos), stack;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) { ++i; continue; }
            // A footnote at an opening bracket still participates as a nested
            // bracket in labels; the complete reference is consumed separately.
            if (text[i] != '[') {
                size_t next = skipInlineToken(text, i,&index);
                if (next > i) { i = next - 1; continue; }
            }
            if (text[i] == '[') stack.push_back(i);
            if (text[i] == ']' && !stack.empty()) {
                ends[stack.back()] = i;
                stack.pop_back();
            }
        }
        return ends;
    }

    string inlines(const string &s, unsigned depth=0, bool links=true) {
        if(depth>maxDepth) return escape(s,true);
        string out;
        static const string punctuation="\\`*_{}[]()>#+-.!|";
        static const std::regex tag(R"(^(?:</?[A-Za-z][^>]*>|<!--[\s\S]*?-->|<\?[\s\S]*?\?>))");
        std::vector<size_t> labels;
        auto delimiters = mathDelimiters(s);
        bool noClosingAngle = false;
        size_t failedEmphasis[2][4] = {};

        for(size_t i=0;i<s.size();) {
            char c=s[i];
            if(c=='\\' && i+1<s.size() && punctuation.find(s[i+1])!=string::npos) { out+=escape(s.substr(i+1,1)); i+=2; continue; }
            if(c=='`') {
                size_t n=1; while(i+n<s.size() && s[i+n]=='`') ++n;
                size_t end=i+n;
                while((end=s.find(string(n,'`'),end))!=string::npos) {
                    if((end==0||s[end-1]!='`') && (end+n==s.size()||s[end+n]!='`')) break;
                    ++end;
                }
                if(end!=string::npos) { out+="<code>"+escape(trim(s.substr(i+n,end-i-n)))+"</code>"; i=end+n; continue; }
                out+=string(n,'`'); i+=n; continue;
            }
            if (auto math = mathAt(s, i,&delimiters)) {
                out += mathHTML(s, *math);
                i = math->end;
                continue;
            }
            if (mathEnabled && s.compare(i, 2, "$$") == 0 && !escapedAt(s, i)) {
                out += "$$";
                i += 2;
                continue;
            }
            if (links && c == '[' && (i == 0 || s[i - 1] != '!')) {
                std::size_t end = notes.referenceEnd(s, i);
                if (end > i) {
                    out += notes.reference(s, i);
                    i = end;
                    continue;
                }
            }
            if(links && (c=='[' || (c=='!'&&i+1<s.size()&&s[i+1]=='['))) {
                bool image=c=='!'; size_t start=i+(image?2:1);
                if (labels.empty()) labels = labelEnds(s,delimiters);
                size_t end=labels[start-1];
                if(end!=string::npos) {
                    size_t after=end+1;
                    // Without a target or definitions, a label is literal text.
                    // Avoid repeatedly copying nested, balanced literal labels.
                    if (refs.empty() && (after==s.size() || s[after]!='(')) {
                        out += c; ++i; continue;
                    }
                    spend(end-start);
                    string label=s.substr(start,end-start); std::optional<Link> link;
                    if(after<s.size()&&s[after]=='(') {
                        size_t close=closing(s,after+1,'(',')');
                        if(close!=string::npos) { link=target(s.substr(after+1,close-after-1)); if(link) after=close+1; }
                    } else {
                        size_t p=after; while(p<s.size()&&space(s[p])) ++p;
                        string id=label;
                        if(p<s.size()&&s[p]=='[') { size_t close=s.find(']',p+1); if(close!=string::npos) { id=s.substr(p+1,close-p-1); if(id.empty()) id=label; after=close+1; } }
                        auto it=refs.find(refID(id)); if(it!=refs.end()) link=it->second;
                    }
                    if(link) {
                        string title=link->title.empty()?"":" title=\""+escape(link->title,true)+"\"";
                        if(image) out+="<img src=\""+escape(link->url,true)+"\" alt=\""+escape(label,true)+"\""+title+suffix;
                        else out+="<a href=\""+escape(link->url,true)+"\""+title+">"+inlines(label,depth+1,false)+"</a>";
                        i=after; continue;
                    }
                }
            }
            if(c=='<' && !noClosingAngle) {
                size_t end=s.find('>',i+1);
                if (end==string::npos) noClosingAngle=true;
                if(end!=string::npos) {
                    string address=s.substr(i+1,end-i-1);
                    if(links && (address.rfind("http://",0)==0||address.rfind("https://",0)==0||address.rfind("ftp://",0)==0||address.find('@')!=string::npos) && address.find_first_of(" \t\n\"<")==string::npos) {
                        bool mail=address.find('@')!=string::npos && address.find("://")==string::npos;
                        if(address.rfind("mailto:",0)==0) { address.erase(0,7); mail=true; }
                        out+="<a href=\""+string(mail?"mailto:":"")+escape(address,true)+"\">"+escape(address,true)+"</a>"; i=end+1; continue;
                    }
                }
                std::match_results<string::const_iterator> m;
                if(!noClosingAngle && std::regex_search(s.begin()+static_cast<std::ptrdiff_t>(i),s.end(),m,tag)) {
                    size_t end = htmlEnd(s, i);
                    end = rawElementEnd(s, i, end);
                    out.append(s, i, end - i);
                    i = end;
                    continue;
                }
            }
            if(c=='*'||c=='_') {
                size_t run=1; while(i+run<s.size()&&s[i+run]==c) ++run;
                size_t n=std::min<size_t>(run,3);
                if(i+n<s.size()&&!space(s[i+n]) && i >= failedEmphasis[c=='_' ? 1 : 0][n]) {
                    size_t end=i+n;
                    while (end < s.size()) {
                        if (s[end] == '\\' && end + 1 < s.size()) { end += 2; continue; }
                        size_t next = skipInlineToken(s, end,&delimiters);
                        if (next > end) { end = next; continue; }
                        if (s.compare(end, n, string(n, c)) == 0 && end > i + n && !space(s[end - 1])) break;
                        ++end;
                    }
                    if (end == s.size()) {
                        failedEmphasis[c=='_' ? 1 : 0][n] = s.size();
                        end = string::npos;
                    }
                    if(end!=string::npos) {
                        string body=inlines(s.substr(i+n,end-i-n),depth+1,links);
                        out+=(n==3?"<strong><em>":n==2?"<strong>":"<em>")+body+(n==3?"</em></strong>":n==2?"</strong>":"</em>"); i=end+n; continue;
                    }
                }
            }
            if(c==' ' && i+1<s.size()&&s[i+1]==' ') {
                size_t end=i; while(end<s.size()&&s[end]==' ') ++end;
                if(end<s.size()&&s[end]=='\n') { out+=" <br"+suffix+"\n"; i=end+1; continue; }
            }
            if(c=='&') { size_t end=entityEnd(s,i); if(end>i) { out.append(s,i,end-i); i=end; continue; } }
            if(c=='<'||c=='&') out+=escape(s.substr(i,1)); else out+=c;
            ++i;
        }
        return out;
    }
    string paragraph(const string &source) {
        string body = inlines(source);
        if (!mathEnabled) return "<p>" + body + "</p>";
        const string marker = "<div class=\"werc-math\">";
        string out;
        size_t pos = 0;
        while (pos < body.size()) {
            size_t start = body.find(marker, pos);
            string prose = body.substr(pos, start == string::npos ? string::npos : start - pos);
            if (!trim(prose).empty()) out += "<p>" + prose + "</p>";
            if (start == string::npos) break;
            size_t close = body.find("</div>", start);
            if (close == string::npos) { out += "<p>" + body.substr(start) + "</p>"; break; }
            size_t end = close + 6;
            out += body.substr(start, end - start);
            pos = end;
        }
        return out;
    }

    Lines cells(string s) const {
        s=trim(s); Lines out(1); bool pipe=false;
        auto delimiters = mathDelimiters(s);
        for(size_t i=0;i<s.size();) {
            if (auto math = mathAt(s, i,&delimiters)) {
                out.back().append(s, i, math->end - i);
                i = math->end;
            } else if (std::size_t end = notes.referenceEnd(s, i); end > i) {
                out.back().append(s, i, end - i);
                i = end;
            } else if(s[i]=='\\'&&i+1<s.size()) { if(s[i+1]=='|') out.back()+="&#124;"; else out.back()+=s.substr(i,2); i+=2; }
            else if(s[i]=='`') {
                size_t n=1; while(i+n<s.size()&&s[i+n]=='`') ++n;
                size_t end=i+n;
                while((end=s.find(string(n,'`'),end))!=string::npos) {
                    if(s[end-1]!='`'&&(end+n==s.size()||s[end+n]!='`')) break;
                    ++end;
                }
                if(end==string::npos) { out.back()+=s.substr(i,n); i+=n; }
                else { out.back()+=s.substr(i,end+n-i); i=end+n; }
            } else if(s[i]=='|') { pipe=true; out.push_back(""); ++i; }
            else out.back()+=s[i++];
        }
        if(!pipe) return {};
        if(!out.empty()&&out.front().empty()) out.erase(out.begin());
        if(!out.empty()&&out.back().empty()) out.pop_back();
        for(auto &v:out) v=trim(v);
        return out;
    }
    Lines alignment(const string &head,const string &rule) const {
        auto h=cells(head), r=cells(rule); if(h.empty()||h.size()!=r.size()) return {};
        static const std::regex sep(R"(^:?-{3,}:?$)");
        Lines out;
        for(auto &s:r) {
            if(!std::regex_match(s,sep)) return {};
            out.push_back(s.front()==':'?(s.back()==':'?"center":"left"):(s.back()==':'?"right":""));
        }
        return out;
    }
    string row(Lines cs,const Lines &align,const string &tag) {
        cs.resize(align.size()); string out="<tr>";
        for(size_t i=0;i<align.size();++i) {
            out+="<"+tag+(tag=="th"?" scope=\"col\"":"");
            if(!align[i].empty()) out+=" style=\"text-align:"+align[i]+"\"";
            out+=">"+inlines(cs[i])+"</"+tag+">";
        }
        return out+"</tr>\n";
    }
    struct Marker { bool valid=false, ordered=false; size_t leading=0, content=0; };
    static Marker marker(const string &s) {
        static const std::regex re(R"(^( *)([*+-]|[0-9]+\.) +(.*)$)");
        std::smatch m; if(!std::regex_match(s,m,re)) return {};
        return {true,m[2].length()>1,static_cast<size_t>(m[1].length()),s.size()-static_cast<size_t>(m[3].length())};
    }
    static bool hr(const string &s) {
        string t=trim(s); if(t.empty()||string("*-_").find(t[0])==string::npos) return false;
        size_t n=0; for(char c:t) { if(c==t[0]) ++n; else if(c!=' ') return false; } return n>=3;
    }
    static bool rawStart(const string &s, string &name) {
        static const std::regex re(R"(^ {0,3}<(hr|p|div|h[1-6]|blockquote|pre|table|dl|ol|ul|script|style|noscript|form|fieldset|iframe|math|ins|del|section|details|textarea)\b)",std::regex::icase);
        std::smatch m; if(!std::regex_search(s,m,re)) return false; name=lower(m[1]); return true;
    }
    bool startBlock(const Lines &src,size_t i) const {
        const auto &s=src[i]; if(trim(s).empty()) return true;
        string t=trim(s), tag;
        if(indent(s)>=4||hr(s)||marker(s).valid||rawStart(s,tag)||t.rfind("<!--",0)==0) return true;
        if(mathEnabled && t.rfind("$$",0)==0) return true;
        if(t[0]=='>'||t[0]=='#'||t.rfind("```",0)==0||t.rfind("~~~",0)==0) return true;
        return i+1<src.size()&&!alignment(s,src[i+1]).empty();
    }
    string blocks(const Lines &src,unsigned depth=0) {
        if(depth>maxDepth) { string all; for(auto &s:src) all+=s+"\n"; return "<pre>"+escape(all)+"</pre>"; }
        std::vector<string> chunks;
        for(size_t i=0;i<src.size();) {
            if(trim(src[i]).empty()) { ++i; continue; }
            string t=trim(src[i]), html; std::smatch m;
            static const std::regex fence(R"(^ {0,3}(`{3,}|~{3,})(.*)$)"), atx(R"(^ {0,3}(#{1,6})\s*(.*?)\s*#*\s*$)"), setext(R"(^ {0,3}(=+|-+) *$)");
            if(std::regex_match(src[i],m,fence)) {
                char c=m[1].str()[0]; size_t n=m[1].length(); string info=trim(m[2]), code; ++i;
                while(i<src.size()) {
                    string close=trim(src[i]); size_t run=close.find_first_not_of(c); if(run==string::npos) run=close.size();
                    if(indent(src[i])<=3&&run>=n&&trim(close.substr(run)).empty()) { ++i; break; }
                    code+=src[i++]+"\n";
                }
                string lang; for(char ch:info) { if(space(ch)) break; if(std::isalnum(static_cast<unsigned char>(ch))||ch=='_'||ch=='-'||ch=='+') lang+=ch; }
                html="<pre><code"+(lang.empty()?"":" class=\"language-"+lang+"\"")+">"+escape(code)+"</code></pre>";
            } else if(indent(src[i])>=4) {
                string code;
                while(i<src.size()&&(trim(src[i]).empty()||indent(src[i])>=4)) { code+=(trim(src[i]).empty()?"":src[i].substr(4))+"\n"; ++i; }
                while(!code.empty()&&code.back()=='\n') code.pop_back();
                html="<pre><code>"+escape(code)+"\n</code></pre>";
            } else if (auto display = displayBlock(src, i)) {
                html = *display;
            } else if(std::regex_match(src[i],m,atx)) {
                unsigned level = static_cast<unsigned>(m[1].length());
                html = outline.heading(level, inlines(m[2]), depth == 0);
                ++i;
            } else if(i+1<src.size()&&std::regex_match(src[i+1],m,setext)) {
                unsigned level = m[1].str()[0] == '=' ? 1 : 2;
                html = outline.heading(level, inlines(t), depth == 0);
                i += 2;
            } else if(hr(src[i])) { html="<hr"+suffix; ++i;
            } else if(t[0]=='>') {
                bool calloutBlock = callouts::parse(t.substr(1)).has_value();
                Lines quote;
                while(i<src.size()) {
                    string line=src[i]; size_t p=indent(line);
                    if(p<=3&&p<line.size()&&line[p]=='>') { line.erase(0,p+1); if(!line.empty()&&line[0]==' ') line.erase(0,1); quote.push_back(line); ++i; }
                    else if(trim(line).empty()) {
                        if (calloutBlock) break;
                        if(i+1<src.size() && trim(src[i+1]).rfind(">",0)==0) {
                            string next = trim(src[i+1]).substr(1);
                            if (!next.empty() && next[0] == ' ') next.erase(0, 1);
                            if (callouts::parse(next)) break;
                            quote.push_back("");
                            ++i;
                        } else break;
                    }
                    else if(!startBlock(src,i)) { quote.push_back(line); ++i; } else break;
                }
                auto callout = quote.empty() ? std::optional<callouts::Header>{} : callouts::parse(quote.front());
                if (callout) {
                    quote.erase(quote.begin());
                    // Render in visual order so title footnotes precede body footnotes.
                    string title = inlines(callout->title);
                    string body = blocks(quote, depth+1);
                    html = callouts::render(*callout, title, body);
                } else html="<blockquote>\n"+blocks(quote,depth+1)+"\n</blockquote>";
            } else if(marker(src[i]).valid) {
                Marker first=marker(src[i]); string tag=first.ordered?"ol":"ul"; std::vector<Lines> items; std::vector<bool> looseness; bool nextLoose=false;
                while(i<src.size()) {
                    Marker mark=marker(src[i]); if(!mark.valid||mark.leading!=first.leading||mark.ordered!=first.ordered) break;
                    Lines item{src[i].substr(mark.content)}; bool loose=nextLoose; nextLoose=false; ++i;
                    while(i<src.size()) {
                        Marker next=marker(src[i]); if(next.valid&&next.leading<=first.leading) break;
                        if(trim(src[i]).empty()) {
                            size_t nextLine=i; while(nextLine<src.size()&&trim(src[nextLine]).empty()) ++nextLine;
                            if(nextLine==src.size()) { i=nextLine; break; }
                            Marker after=marker(src[nextLine]);
                            if(indent(src[nextLine])>first.leading || (after.valid&&after.leading==first.leading&&after.ordered==first.ordered)) {
                                loose=true; item.push_back(""); i=nextLine;
                                if(after.valid&&after.leading==first.leading) { nextLoose=true; break; }
                                continue;
                            }
                            break;
                        }
                        if(indent(src[i])>first.leading) { size_t cut=std::min(indent(src[i]),mark.content); item.push_back(src[i].substr(cut)); ++i; }
                        else if(!startBlock(src,i)) { item.push_back(src[i++]); } else break;
                    }
                    items.push_back(item); looseness.push_back(loose);
                }
                html="<"+tag+">\n";
                for(size_t itemIndex=0;itemIndex<items.size();++itemIndex) {
                    string body=blocks(items[itemIndex],depth+1);
                    if(!looseness[itemIndex]&&body.rfind("<p>",0)==0) { size_t end=body.find("</p>"); if(end!=string::npos) { body.erase(end,4); body.erase(0,3); } }
                    html+="<li>"+body+"</li>\n";
                }
                html+="</"+tag+">";
            } else {
                string raw;
                if(rawStart(src[i],raw)) {
                    string close="</"+raw+">";
                    do { string line=src[i++]; html+=line+"\n"; if(raw=="hr" || trim(line).find("/>")!=string::npos || lower(line).find(close)!=string::npos) break; } while(i<src.size());
                    if(!html.empty()) html.pop_back();
                    if(mathEnabled) html = rawMath(html);
                } else if(t.rfind("<!--",0)==0) {
                    do { string line=src[i++]; html+=line+"\n"; if(line.find("-->")!=string::npos) break; } while(i<src.size());
                    html.pop_back();
                } else {
                    Lines align=i+1<src.size()?alignment(src[i],src[i+1]):Lines{};
                    if(!align.empty()) {
                        html="<div class=\"markdown-table\">\n<table>\n<thead>\n"+row(cells(src[i]),align,"th")+"</thead>\n<tbody>\n"; i+=2;
                        while(i<src.size()&&indent(src[i])<4) { auto cs=cells(src[i]); if(cs.empty()) break; html+=row(cs,align,"td"); ++i; }
                        html+="</tbody>\n</table>\n</div>";
                    } else {
                        string para=src[i++];
                        while(i<src.size()&&!startBlock(src,i)) {
                            if(i+1<src.size()&&std::regex_match(src[i+1],setext)) break;
                            para+='\n'+src[i++];
                        }
                        html = paragraph(para);
                    }
                }
            }
            chunks.push_back(html);
        }
        string out; for(auto &chunk:chunks) { if(!out.empty()) out+="\n\n"; out+=chunk; } return out;
    }
public:
    explicit Markdown(bool html4=false, bool math=false) : mathEnabled(math) { if(html4) suffix=">"; }
    string render(const string &input) {
        workRemaining = 128 * input.size() + 8192;
        refs.clear();
        notes = footnotes::Document{};
        Lines src=lines(normalize(input));
        auto document = metadata::parse(src);
        outline = toc::Document(lower(document.value("toc")) != "false", lower(document.value("lang")));
        string head = document.propertiesHTML();
        src.erase(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(document.bodyLine));
        definitions(src);
        string body = blocks(src);
        string endnotes = notes.render([this](Lines content) {
            definitions(content);
            return blocks(content, 1);
        }, suffix);
        return head + outline.render() + body + endnotes + "\n";
    }
};

// This mode is also used by app.rc; no interpreter is needed to build the head.
static string mathjaxHeaders(const string &url) {
    return R"(<script>
window.MathJax = {
  tex: {inlineMath: [['\\(', '\\)']], displayMath: [['\\[', '\\]']],
        processEscapes: true, processEnvironments: false, processRefs: false},
  options: {ignoreHtmlClass: '.*', processHtmlClass: 'werc-math'}
};
</script>
)" + string("<script defer src=\"") + escape(url) + "\"></script>\n";
}

int main(int argc,char **argv) {
    try {
        bool html4=false, math=false, untrusted=false; Lines files;
        for(int i=1;i<argc;++i) {
            string arg=argv[i];
            if(arg=="--version"||arg=="--shortversion") { std::cout<<(arg=="--version"?"werc md2html ":"")<<"1.0\n"; return 0; }
            if(arg=="--help") { std::cout<<"Usage: md2html [--untrusted] [--math] [--html4tags] [--] [FILE ...]\nReads stdin when no files are given; - means stdin.\n       md2html --page-metadata [FILE]\n       md2html --mathjax-headers URL\n"; return 0; }
            if(arg=="--page-metadata") {
                if(i+1 == argc) std::cout << metadata::pageAssignments({});
                else if(i+2 == argc) {
                    std::ifstream input(argv[i+1], std::ios::binary);
                    if(!input) throw std::runtime_error("cannot open " + string(argv[i+1]));
                    std::cout << metadata::pageAssignments(metadata::read(input));
                } else throw std::runtime_error("--page-metadata accepts at most one file");
                return std::cout ? 0 : 1;
            }
            if(arg=="--mathjax-headers") {
                if(i+2 != argc) throw std::runtime_error("--mathjax-headers requires one URL");
                std::cout << mathjaxHeaders(argv[i+1]);
                return std::cout ? 0 : 1;
            }
            if(arg=="--untrusted") untrusted=true;
            else if(arg=="--math") math=true;
            else if(arg=="--html4tags") html4=true;
            else if(arg=="--") { while(++i<argc) files.push_back(argv[i]); break; }
            else if(arg.size()>1&&arg[0]=='-') throw std::runtime_error("unknown option: "+arg);
            else files.push_back(arg);
        }
        string input;
        constexpr size_t inputLimit = 4 * 1024 * 1024;
        auto appendInput = [&](std::istream &stream) {
            char buffer[8192];
            while (stream) {
                stream.read(buffer,sizeof buffer);
                size_t count = static_cast<size_t>(stream.gcount());
                if (count > inputLimit-input.size()) throw std::runtime_error("document exceeds 4 MiB input limit");
                input.append(buffer,count);
            }
        };
        if(files.empty()) files.push_back("-");
        for(auto &file:files) {
            if(file=="-") appendInput(std::cin);
            else { std::ifstream in(file,std::ios::binary); if(!in) throw std::runtime_error("cannot open "+file); appendInput(in); if(in.bad()) throw std::runtime_error("cannot read "+file); }
        }
        if(std::cin.bad()) throw std::runtime_error("cannot read stdin");
        // Restricted imports/submissions are plain escaped text. No raw HTML,
        // URL attributes, metadata evaluation, or embedded active content.
        if (untrusted) std::cout << "<pre class=\"untrusted-content\">" << escape(input) << "</pre>\n";
        else std::cout<<Markdown(html4,math).render(input);
        if(!std::cout) throw std::runtime_error("cannot write output");
    } catch(const std::exception &e) { std::cerr<<"md2html: "<<e.what()<<'\n'; return 1; }
}
