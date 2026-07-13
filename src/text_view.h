#ifndef TEXT_VIEW_H
#define TEXT_VIEW_H

#include <string_view>

namespace rots::text {

/// Returns the textual prefix ending before the first null character.
[[nodiscard]] constexpr std::string_view truncate_at_null(std::string_view text) noexcept
{
    return text.substr(0, text.find('\0'));
}

} // namespace rots::text

#endif // TEXT_VIEW_H
