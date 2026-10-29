#include "metadata.h"

#include <cctype>
#include <cstdlib>
#include <istream>
#include <optional>

namespace metadata {
using std::string;
namespace {
bool space(char c) { return std::isspace(static_cast<unsigned char>(c)); }
string trim(string s) {
    auto first = s.find_first_not_of(" \t\r\n");
    auto last = s.find_last_not_of(" \t\r\n");
    return first == string::npos ? "" : s.substr(first, last - first + 1);
}
string lower(string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
string escape(const string &text) {
    string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}
bool publishingField(const string &key) {
    string name = lower(key);
    return name == "title" || name == "description" || name == "author" ||
           name == "date" || name == "updated" || name == "lang" || name == "toc";
}
string propertyHTML(const string &value) {
    string scheme = lower(value.substr(0, 8));
    std::size_t prefix = scheme.rfind("https://", 0) == 0 ? 8 : scheme.rfind("http://", 0) == 0 ? 7 : 0;
    bool link = prefix && value.size() > prefix;
    for (char c : value) if (space(c) || string("<>\"'").find(c) != string::npos) link = false;
    string escaped = escape(value);
    return link ? "<a href=\"" + escaped + "\">" + escaped + "</a>" : escaped;
}
std::optional<string> parseScalar(string text) {
    text = trim(text);
    if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
        text = text.substr(1, text.size() - 2);
        std::size_t pos = 0;
        while ((pos = text.find("''", pos)) != string::npos) {
            text.replace(pos, 2, "'");
            ++pos;
        }
    } else if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
        string decoded;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) ++i;
            decoded += text[i];
        }
        text = decoded;
    } else {
        for (std::size_t i = 1; i < text.size(); ++i) {
            if (text[i] == '#' && space(text[i - 1])) { text = trim(text.substr(0, i)); break; }
        }
        if (!text.empty() && string("[]{}&*!|>\"'").find(text[0]) != string::npos) return {};
        if (text == "---") return {};
    }
    return text;
}
std::optional<Lines> parseValues(string raw) {
    raw = trim(raw);
    if (raw.empty() || raw[0] != '[') {
        auto value = parseScalar(raw);
        if (!value) return {};
        return value->empty() ? Lines{} : Lines{*value};
    }
    std::size_t end = raw.rfind(']');
    if (end == string::npos) return {};
    string tail = trim(raw.substr(end + 1));
    if (!tail.empty() && tail[0] != '#') return {};
    string body = raw.substr(1, end - 1), part;
    char quote = 0;
    Lines result;
    for (std::size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (quote) {
            part += c;
            if (c == '\\' && quote == '"' && i + 1 < body.size()) part += body[++i];
            else if (c == quote) {
                if (quote == '\'' && i + 1 < body.size() && body[i + 1] == '\'') part += body[++i];
                else quote = 0;
            }
        } else if (c == '\'' || c == '"') { quote = c; part += c; }
        else if (c == ',') {
            auto value = parseScalar(part);
            if (!value || trim(part).empty()) return {};
            result.push_back(*value);
            part.clear();
        } else part += c;
    }
    if (quote) return {};
    if (!trim(part).empty()) {
        auto value = parseScalar(part);
        if (!value) return {};
        result.push_back(*value);
    }
    return result;
}

// A bounded reader never consumes a whole article looking for a missing delimiter.
bool readLine(std::istream &input, string &line, std::size_t &bytes) {
    line.clear();
    char c;
    while (bytes < maxHeaderBytes && input.get(c)) {
        ++bytes;
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        line += c;
    }
    return !line.empty() && bytes < maxHeaderBytes;
}
string openingLine(string line) {
    if (line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
    return line;
}
bool delimiter(const string &line) {
    return line.rfind("---", 0) == 0 && trim(line) == "---";
}
string plainText(string text) {
    // rc exports list elements separated by byte 1. Join defaults as text.
    for (char &c : text) if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    return trim(text);
}
string environment(const char *name) {
    const char *value = std::getenv(name);
    return value ? plainText(value) : "";
}
bool validDate(const string &date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    for (std::size_t i = 0; i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return false;
    int year = std::stoi(date.substr(0, 4));
    int month = std::stoi(date.substr(5, 2));
    int day = std::stoi(date.substr(8, 2));
    if (year == 0 || month < 1 || month > 12) return false;
    static constexpr int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int maximum = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ++maximum;
    return day >= 1 && day <= maximum;
}
bool validLanguage(const string &language) {
    // Check subtag shape in one scan; leave registry validation to the author.
    if (language.empty()) return false;
    std::size_t length = 0;
    bool first = true;
    for (char c : language) {
        if (c == '-') {
            if (length == 0) return false;
            length = 0;
            first = false;
        } else {
            bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            if ((!alpha && (first || c < '0' || c > '9')) || ++length > 8) return false;
        }
    }
    return length != 0;
}
string rcAssignment(const string &name, const string &value) {
    string quoted;
    for (char c : value) quoted += c == '\'' ? "''" : string(1, c);
    return name + "='" + quoted + "'\n";
}
} // namespace

Document parse(const Lines &lines) {
    Document document;
    if (lines.empty() || !delimiter(openingLine(lines[0]))) return document;
    std::size_t end = 1, bytes = lines[0].size() + 1;
    for (; end < lines.size(); ++end) {
        bytes += lines[end].size() + 1;
        if (bytes > maxHeaderBytes) return {};
        if (delimiter(lines[end])) break;
    }
    if (end == lines.size()) return {};
    for (std::size_t i = 1; i < end; ++i) {
        const string &line = lines[i];
        string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#') continue;
        if (space(line[0])) {
            std::size_t marker = line.find_first_not_of(" \t");
            if (document.fields.empty() || marker + 1 >= line.size() || line[marker] != '-' || !space(line[marker + 1])) return {};
            auto value = parseScalar(line.substr(marker + 2));
            if (!value) return {};
            document.fields.back().second.push_back(*value);
        } else {
            std::size_t colon = line.find(':');
            if (colon == string::npos || colon == 0) return {};
            auto value = parseValues(line.substr(colon + 1));
            if (!value) return {};
            document.fields.push_back({trim(line.substr(0, colon)), *value});
        }
    }
    if (!document.fields.empty()) document.bodyLine = end + 1;
    return document;
}

Document read(std::istream &input) {
    Lines prefix;
    string line;
    std::size_t bytes = 0;
    if (!readLine(input, line, bytes)) return {};
    prefix.push_back(line);
    if (!delimiter(openingLine(line))) {
        // Match werc's existing opening Setext title + blank-line convention.
        string underline, blank;
        if (readLine(input, underline, bytes) && readLine(input, blank, bytes) &&
            !underline.empty() && underline.find_first_not_of('=') == string::npos &&
            blank.find_first_not_of(' ') == string::npos) {
            Document document;
            document.legacyTitle = line;
            return document;
        }
        return {};
    }
    while (readLine(input, line, bytes)) {
        prefix.push_back(line);
        if (delimiter(line)) return parse(prefix);
    }
    return {};
}

string Document::value(const string &key) const {
    for (auto it = fields.rbegin(); it != fields.rend(); ++it)
        if (lower(it->first) == key && it->second.size() == 1) return plainText(it->second[0]);
    return "";
}

string Document::propertiesHTML() const {
    string rows;
    for (const auto &property : fields) {
        if (publishingField(property.first)) continue;
        rows += "<div class=\"markdown-property\"><dt>" + propertyHTML(property.first) + "</dt><dd>";
        if (property.second.empty()) rows += "<span class=\"property-empty\" aria-label=\"Empty\">&#8212;</span>";
        bool tags = lower(property.first) == "tags" || lower(property.first) == "tag";
        for (std::size_t i = 0; i < property.second.size(); ++i) {
            if (i) rows += ' ';
            rows += "<span class=\"" + string(tags ? "property-tag" : "property-value") + "\">" +
                    propertyHTML(property.second[i]) + "</span>";
        }
        rows += "</dd></div>\n";
    }
    return rows.empty() ? "" : "<div class=\"markdown-properties\" role=\"group\" aria-label=\"Properties\">\n<dl>\n" + rows + "</dl>\n</div>\n\n";
}

string pageAssignments(const Document &document) {
    auto resolve = [&](const string &key, const char *fallback) {
        string value = document.value(key);
        return value.empty() ? environment(fallback) : value;
    };
    string title = document.value("title");
    if (title.empty()) title = document.legacyTitle.empty() ? environment("pageTitle") : document.legacyTitle;
    string description = resolve("description", "meta_description");
    string author = resolve("author", "meta_author");
    auto date = [&](const string &key, const char *fallback) {
        string value = document.value(key);
        if (!validDate(value)) value = environment(fallback);
        return validDate(value) ? value : "";
    };
    string published = date("date", "meta_date"), updated = date("updated", "meta_updated");
    string language = document.value("lang");
    if (!validLanguage(language)) language = environment("html_lang");
    if (!validLanguage(language)) language.clear();

    Lines details;
    if (!author.empty()) details.push_back("<span class=\"article-author\">By " + escape(author) + "</span>");
    if (!published.empty()) details.push_back("<time datetime=\"" + published + "\">Published " + published + "</time>");
    if (!updated.empty()) details.push_back("<time datetime=\"" + updated + "\">Updated " + updated + "</time>");
    string byline;
    if (!details.empty()) {
        byline = "<div class=\"article-metadata\">";
        for (const auto &detail : details) byline += detail;
        byline += "</div>";
    }
    return rcAssignment("pageTitle", escape(plainText(title))) +
           rcAssignment("meta_description", escape(description)) +
           rcAssignment("meta_author", escape(author)) +
           rcAssignment("meta_date", published) + rcAssignment("meta_updated", updated) +
           rcAssignment("html_language_attribute", language.empty() ? "" : " lang=\"" + language + "\"") +
           rcAssignment("article_metadata", byline);
}
} // namespace metadata
