#ifndef WERC_METADATA_H
#define WERC_METADATA_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace metadata {
using Lines = std::vector<std::string>;
using Property = std::pair<std::string, Lines>;

// The same small front-matter grammar serves the renderer and the early reader.
// Unsupported or oversized blocks are left as ordinary Markdown.
inline constexpr std::size_t maxHeaderBytes = 64 * 1024;
struct Document {
    std::vector<Property> fields;
    std::size_t bodyLine = 0;
    std::string legacyTitle;

    std::string propertiesHTML() const;
    std::string value(const std::string &key) const;
};

Document parse(const Lines &lines);
Document read(std::istream &input);

// Resolve page fields against werc's inherited site defaults and emit only
// fixed-name, HTML-escaped, safely quoted rc assignments. No author text is code.
std::string pageAssignments(const Document &document);
} // namespace metadata
#endif
