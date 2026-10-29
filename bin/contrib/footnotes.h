#ifndef WERC_FOOTNOTES_H
#define WERC_FOOTNOTES_H

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace footnotes {
using Lines = std::vector<std::string>;

// State belongs to one rendered document, never a process-wide cache.
class Document {
    struct Note {
        Lines body;
        std::size_t number = 0;
        std::size_t uses = 0;
    };
    std::map<std::string, Note> notes;
    std::vector<std::string> order;
public:
    // Called only in Markdown definition context, outside code/HTML/math.
    bool extract(Lines &lines, std::size_t &line);
    // Lookahead is separate from rendering so it cannot change numbering.
    std::size_t referenceEnd(const std::string &text, std::size_t pos) const;
    std::string reference(const std::string &text, std::size_t pos);
    // The callback keeps full Markdown rendering in the main parser.
    std::string render(const std::function<std::string(Lines)> &renderBlocks,
                       const std::string &emptyElementSuffix);
};
} // namespace footnotes
#endif
