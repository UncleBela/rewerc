#ifndef WERC_CALLOUTS_H
#define WERC_CALLOUTS_H

#include <optional>
#include <string>

namespace callouts {
enum class Fold { none, open, closed };
struct Header {
    std::string type;
    std::string title;
    Fold fold = Fold::none;
};

// Parse the first line after the Markdown parser removes one blockquote marker.
std::optional<Header> parse(const std::string &line);
// The caller renders Markdown in title/body; this module owns the outer markup.
std::string render(const Header &header, const std::string &titleHTML, const std::string &bodyHTML);
} // namespace callouts
#endif
