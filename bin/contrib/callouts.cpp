#include "callouts.h"

#include <cctype>
#include <utility>

namespace callouts {
namespace {
std::string trim(const std::string &text) {
    auto first = text.find_first_not_of(" \t\r\n");
    auto last = text.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? "" : text.substr(first, last - first + 1);
}
std::string canonicalType(const std::string &type) {
    static const std::pair<const char *, const char *> aliases[] = {
        {"summary", "abstract"}, {"tldr", "abstract"},
        {"hint", "tip"}, {"important", "tip"},
        {"check", "success"}, {"done", "success"},
        {"help", "question"}, {"faq", "question"},
        {"caution", "warning"}, {"attention", "warning"},
        {"fail", "failure"}, {"missing", "failure"},
        {"error", "danger"}, {"cite", "quote"}
    };
    for (const auto &alias : aliases) if (type == alias.first) return alias.second;
    for (const char *known : {"note", "abstract", "info", "todo", "tip", "success",
                             "question", "warning", "failure", "danger", "bug", "example", "quote"})
        if (type == known) return type;
    return "note";
}
} // namespace

std::optional<Header> parse(const std::string &line) {
    std::size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos || start > 3 || line.compare(start, 2, "[!") != 0) return {};
    std::size_t close = line.find(']', start + 2);
    if (close == std::string::npos || close == start + 2) return {};
    std::string type = line.substr(start + 2, close - start - 2);
    for (char &c : type) {
        unsigned char byte = static_cast<unsigned char>(c);
        if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') || c == '-' || c == '_')) return {};
        c = static_cast<char>(std::tolower(byte));
    }
    Header header;
    header.type = canonicalType(type);
    std::size_t after = close + 1;
    if (after < line.size() && (line[after] == '+' || line[after] == '-')) {
        header.fold = line[after] == '+' ? Fold::open : Fold::closed;
        ++after;
    }
    header.title = trim(line.substr(after));
    if (header.title.empty()) {
        header.title = header.type;
        header.title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(header.title[0])));
    }
    return header;
}

std::string render(const Header &header, const std::string &titleHTML, const std::string &bodyHTML) {
    bool folding = header.fold != Fold::none;
    std::string tag = folding ? "details" : "aside";
    std::string titleTag = folding ? "summary" : "div";
    std::string html = "<" + tag + " class=\"callout\" data-callout=\"" + header.type + "\"";
    if (header.fold == Fold::open) html += " open";
    html += ">\n<" + titleTag + " class=\"callout-title\">" + titleHTML + "</" + titleTag + ">";
    if (!bodyHTML.empty()) html += "\n<div class=\"callout-content\">\n" + bodyHTML + "\n</div>";
    return html + "\n</" + tag + ">";
}
} // namespace callouts
