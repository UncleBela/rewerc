#include "toc.h"

#include <cctype>
#include <utility>

namespace toc {
namespace {
std::string lower(std::string text) {
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::size_t tagEnd(const std::string &html, std::size_t start) {
    char quote = 0;
    for (std::size_t i = start + 1; i < html.size(); ++i) {
        if (quote) { if (html[i] == quote) quote = 0; }
        else if (html[i] == '\'' || html[i] == '"') quote = html[i];
        else if (html[i] == '>') return i + 1;
    }
    return start;
}
// Keep text, not interactive markup, in TOC links. In particular, never copy
// a heading's footnote anchor/ID, raw scripts, or links into another anchor.
std::string labelText(const std::string &html) {
    std::string text;
    for (std::size_t i = 0; i < html.size();) {
        if (html.compare(i, 4, "<!--") == 0) {
            auto end = html.find("-->", i + 4);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }
        if (html[i] == '<') {
            std::size_t end = tagEnd(html, i);
            if (end > i) {
                std::string tag = lower(html.substr(i, end - i));
                std::size_t nameStart = tag.size() > 1 && tag[1] == '/' ? 2 : 1;
                std::size_t nameEnd = nameStart;
                while (nameEnd < tag.size() && std::isalnum(static_cast<unsigned char>(tag[nameEnd]))) ++nameEnd;
                std::string name = tag.substr(nameStart, nameEnd - nameStart), opaque;
                if (nameStart == 1 && (name == "script" || name == "style")) opaque = name;
                else if (nameStart == 1 && name == "sup" && tag.find("footnote-ref") != std::string::npos) opaque = "sup";
                if (!opaque.empty()) {
                    std::size_t close = lower(html).find("</" + opaque, end);
                    if (close == std::string::npos) return text;
                    end = tagEnd(html, close);
                    if (end == close) return text;
                } else if (name == "br") text += ' ';
                i = end;
                continue;
            }
        }
        text += html[i++];
    }
    return text;
}
bool entity(const std::string &text, std::size_t pos, std::size_t &end) {
    end = text.find(';', pos + 1);
    if (end == std::string::npos || end == pos + 1 || end - pos > 32) return false;
    for (std::size_t i = pos + 1; i < end; ++i) {
        unsigned char c = text[i];
        if (!std::isalnum(c) && c != '#') return false;
    }
    return true;
}
std::string escaped(const std::string &text, bool preserveEntities = false) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        std::size_t end;
        if (c == '&' && preserveEntities && entity(text, i, end)) {
            out.append(text, i, end - i + 1);
            i = end;
        } else if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}
void utf8(std::string &out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
char32_t nextCodepoint(const std::string &text, std::size_t &pos) {
    unsigned char first = text[pos++];
    if (first < 0x80) return first;
    unsigned width = first >= 0xC2 && first < 0xE0 ? 2 : first < 0xF0 && first >= 0xE0 ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (!width || pos + width - 1 > text.size()) return 0xFFFD;
    char32_t cp = first & (0x7F >> width);
    for (unsigned i = 1; i < width; ++i) {
        unsigned char c = text[pos];
        if ((c & 0xC0) != 0x80) return 0xFFFD;
        ++pos;
        cp = (cp << 6) | (c & 0x3F);
    }
    if ((width == 2 && cp < 0x80) || (width == 3 && cp < 0x800) ||
        (width == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}
std::string decodeEntities(const std::string &text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::size_t end;
        if (text[i] != '&' || !entity(text, i, end)) { out += text[i]; continue; }
        std::string name = text.substr(i + 1, end - i - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name == "nbsp") out += ' ';
        else if (name[0] == '#') {
            std::size_t start = name.size() > 1 && (name[1] == 'x' || name[1] == 'X') ? 2 : 1;
            unsigned base = start == 2 ? 16 : 10;
            char32_t cp = 0;
            bool valid = start < name.size();
            for (std::size_t n = start; n < name.size(); ++n) {
                char c = name[n];
                unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : base;
                if (digit >= base || cp > (0x10FFFF - digit) / base) { valid = false; break; }
                cp = cp * base + digit;
            }
            if (valid && cp && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) utf8(out, cp);
            else out += ' ';
        } else out += name;
        i = end;
    }
    return out;
}
char latinLetter(char32_t cp) {
    if ((cp >= 0xC0 && cp <= 0xC5) || (cp >= 0xE0 && cp <= 0xE5)) return 'a';
    if (cp == 0xC7 || cp == 0xE7) return 'c';
    if ((cp >= 0xC8 && cp <= 0xCB) || (cp >= 0xE8 && cp <= 0xEB)) return 'e';
    if ((cp >= 0xCC && cp <= 0xCF) || (cp >= 0xEC && cp <= 0xEF)) return 'i';
    if (cp == 0xD1 || cp == 0xF1) return 'n';
    if ((cp >= 0xD2 && cp <= 0xD6) || (cp >= 0xF2 && cp <= 0xF6) || cp == 0x150 || cp == 0x151) return 'o';
    if ((cp >= 0xD9 && cp <= 0xDC) || (cp >= 0xF9 && cp <= 0xFC) || cp == 0x170 || cp == 0x171) return 'u';
    if (cp == 0xDD || cp == 0xFD || cp == 0xFF || cp == 0x178) return 'y';
    return 0;
}
std::string slug(const std::string &label) {
    std::string text = decodeEntities(label), out;
    bool separator = false;
    for (std::size_t i = 0; i < text.size();) {
        char32_t cp = nextCodepoint(text, i);
        if (cp >= 0x300 && cp <= 0x36F) continue; // decomposed accents
        char letter = latinLetter(cp);
        bool asciiWord = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9');
        bool unicodeWord = cp >= 0x80 && cp != 0xA0 && cp != 0x85 && cp != 0xFFFD &&
                           !(cp >= 0x2000 && cp <= 0x206F) && !(cp >= 0x3000 && cp <= 0x303F);
        if (letter || asciiWord || unicodeWord) {
            if (separator && !out.empty()) out += '-';
            separator = false;
            if (letter) out += letter;
            else if (cp < 0x80) out += static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp);
            else utf8(out, cp);
        } else separator = true;
    }
    if (out.empty()) out = "section";
    // Footnotes and werc's page layout already own these identifiers.
    if (out.rfind("fn-", 0) == 0 || out.rfind("fnref-", 0) == 0 ||
        out == "header" || out == "side-bar" || out == "main-copy" || out == "footer") out = "section-" + out;
    return out;
}
} // namespace

std::string Document::heading(unsigned level, const std::string &inlineHTML, bool include) {
    std::string label = labelText(inlineHTML);
    std::string base = slug(label), id = base;
    auto &next = suffixes[base];
    while (!ids.insert(id).second) id = base + "-" + std::to_string(++next + 1);
    if (enabled && include) {
        std::size_t index = headings.size();
        headings.push_back({level, id, escaped(label.empty() ? "Section" : label, true), {}});
        while (!parents.empty() && headings[parents.back()].level >= level) parents.pop_back();
        if (parents.empty()) roots.push_back(index);
        else headings[parents.back()].children.push_back(index);
        parents.push_back(index);
    }
    const std::string linkLabel = hungarian ? "Hivatkozás erre a szakaszra" : "Link to this section";
    const std::string permalink = " <a class=\"heading-permalink\" href=\"#" + escaped(id) +
        "\" aria-label=\"" + linkLabel + "\">#</a>";
    return "<h" + std::to_string(level) + " id=\"" + escaped(id) + "\">" + inlineHTML + permalink + "</h" + std::to_string(level) + ">";
}

std::string Document::listHTML(const std::vector<std::size_t> &items) const {
    std::string html = "<ol>\n";
    for (std::size_t index : items) {
        const Heading &item = headings[index];
        html += "<li><a href=\"#" + escaped(item.id) + "\">" + item.labelHTML + "</a>";
        if (!item.children.empty()) html += '\n' + listHTML(item.children);
        html += "</li>\n";
    }
    return html + "</ol>\n";
}

std::string Document::render() const {
    if (!enabled || roots.empty()) return "";
    const std::string title = hungarian ? "Tartalomjegyzék" : "Contents";
    const std::string label = hungarian ? "Tartalomjegyzék" : "Table of contents";
    return "<nav class=\"table-of-contents\" aria-label=\"" + label + "\">\n<div class=\"toc-title\">" + title + "</div>\n" + listHTML(roots) + "</nav>\n\n";
}
} // namespace toc
