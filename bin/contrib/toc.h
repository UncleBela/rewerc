#ifndef WERC_TOC_H
#define WERC_TOC_H

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace toc {
// One document's heading IDs and outline. No source reparse or global cache.
class Document {
    struct Heading {
        unsigned level;
        std::string id, labelHTML;
        std::vector<std::size_t> children;
    };
    bool enabled;
    bool hungarian;
    std::set<std::string> ids;
    std::map<std::string, std::size_t> suffixes;
    std::vector<Heading> headings;
    std::vector<std::size_t> roots, parents;
    std::string listHTML(const std::vector<std::size_t> &items) const;
public:
    explicit Document(bool visible = true, const std::string &language = "")
        : enabled(visible), hungarian(language == "hu" || language.compare(0, 3, "hu-") == 0) {}
    // Receives already-rendered inline HTML; never renders it a second time.
    // Nested/footnote headings may get IDs without joining the article outline.
    std::string heading(unsigned level, const std::string &inlineHTML, bool include = true);
    std::string render() const;
};
} // namespace toc
#endif
