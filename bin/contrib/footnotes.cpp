#include "footnotes.h"

#include <cctype>
#include <optional>
#include <utility>

namespace footnotes {
namespace {
struct Label {
    std::string name;
    std::size_t end;
};
std::optional<Label> labelAt(const std::string &text, std::size_t pos) {
    if (text.compare(pos, 2, "[^") != 0) return {};
    for (std::size_t i = pos + 2; i < text.size(); ++i) {
        if (text[i] == ']') {
            if (i == pos + 2) return {};
            return Label{text.substr(pos + 2, i - pos - 2), i + 1};
        }
        if (text[i] == '[' || std::isspace(static_cast<unsigned char>(text[i]))) return {};
    }
    return {};
}
std::size_t indentation(const std::string &line) {
    auto first = line.find_first_not_of(' ');
    return first == std::string::npos ? line.size() : first;
}
bool blank(const std::string &line) { return line.find_first_not_of(" \t") == std::string::npos; }
std::string referenceID(std::size_t number, std::size_t use) {
    return "fnref-" + std::to_string(number) + (use == 1 ? "" : "-" + std::to_string(use));
}
} // namespace

bool Document::extract(Lines &lines, std::size_t &line) {
    const std::string &first = lines[line];
    std::size_t start = indentation(first);
    if (start > 3) return false;
    auto label = labelAt(first, start);
    if (!label || label->end == first.size() || first[label->end] != ':') return false;
    std::size_t content = label->end + 1;
    if (content < first.size() && first[content] == ' ') ++content;
    Lines body{first.substr(content)};
    std::size_t end = line + 1;
    while (end < lines.size()) {
        if (blank(lines[end])) {
            std::size_t next = end;
            while (next < lines.size() && blank(lines[next])) ++next;
            if (next == lines.size() || indentation(lines[next]) < 4) break;
            while (end < next) { body.emplace_back(); ++end; }
        }
        if (indentation(lines[end]) < 4) break;
        body.push_back(lines[end].substr(4));
        ++end;
    }
    // First definition wins. Unreferenced definitions do not produce output.
    notes.try_emplace(label->name, Note{std::move(body), 0, 0});
    for (std::size_t i = line; i < end; ++i) lines[i].clear();
    line = end - 1;
    return true;
}

std::size_t Document::referenceEnd(const std::string &text, std::size_t pos) const {
    if (notes.empty()) return pos;
    auto label = labelAt(text, pos);
    return label && notes.find(label->name) != notes.end() ? label->end : pos;
}

std::string Document::reference(const std::string &text, std::size_t pos) {
    auto label = labelAt(text, pos);
    if (!label) return "";
    auto found = notes.find(label->name);
    if (found == notes.end()) return "";
    Note &note = found->second;
    if (note.number == 0) {
        note.number = order.size() + 1;
        order.push_back(label->name);
    }
    ++note.uses;
    std::string number = std::to_string(note.number);
    return "<sup class=\"footnote-ref\" id=\"" + referenceID(note.number, note.uses) +
           "\"><a href=\"#fn-" + number + "\" role=\"doc-noteref\" aria-label=\"Footnote " +
           number + "\">" + number + "</a></sup>";
}

std::string Document::render(const std::function<std::string(Lines)> &renderBlocks,
                             const std::string &emptyElementSuffix) {
    if (order.empty()) return "";
    std::vector<std::string> bodies;
    // Rendering a note may discover a reference to another defined note. It is
    // appended once, so self/cyclic references never recursively render bodies.
    for (std::size_t i = 0; i < order.size(); ++i) {
        Lines body = notes.at(order[i]).body;
        bodies.push_back(renderBlocks(std::move(body)));
    }
    std::string html = "\n\n<section class=\"footnotes\" role=\"doc-endnotes\" aria-label=\"Footnotes\">\n<hr" +
                       emptyElementSuffix + "\n<ol>\n";
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Note &note = notes.at(order[i]);
        html += "<li id=\"fn-" + std::to_string(note.number) + "\">\n" + bodies[i] +
                "<span class=\"footnote-backlinks\">";
        // Backlinks are emitted only after every body has been rendered, when
        // all reference occurrences (including cross-references) are known.
        for (std::size_t use = 1; use <= note.uses; ++use) {
            // I'll have to fix this later because I want the footnotes to be inline with the text itself
            if (use > 1) html += ' ';
            html += "<a href=\"#" + referenceID(note.number, use) +
                    "\" class=\"footnote-backref\" aria-label=\"Back to reference " +
                    std::to_string(note.number) + " (" + std::to_string(use) + ")\">&#8617;";
            if (use > 1) html += "<sup>" + std::to_string(use) + "</sup>";
            html += "</a>";
        }
        html += "</span>\n</li>\n";
    }
    return html + "</ol>\n</section>";
}
} // namespace footnotes
