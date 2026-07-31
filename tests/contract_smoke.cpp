#include "markdownmay/document_session.hpp"
#include "markdownmay/editor.hpp"
#include "markdownmay/error_codes.hpp"
#include "markdownmay/export.hpp"
#include "markdownmay/fileio.hpp"
#include "markdownmay/markdown.hpp"
#include "markdownmay/platform.hpp"
#include "markdownmay/services.hpp"

#include <type_traits>

int main() {
    using namespace markdownmay;

    static_assert(std::is_same_v<std::underlying_type_t<ViewMode>, std::uint8_t>);
    static_assert(static_cast<std::uint32_t>(ErrorCode::ok) == 0);
    static_assert(
        static_cast<std::uint32_t>(ErrorCode::editor_cannot_enter_render_mode) ==
        2004);
    static_assert(
        static_cast<std::uint32_t>(ErrorCode::export_revision_not_current) ==
        6001);

    const PageSettings a4;
    if (a4.width_micrometers != 210000 ||
        a4.height_micrometers != 297000) {
        return 1;
    }

    const Settings defaults;
    if (defaults.default_mode != ViewMode::render) {
        return 2;
    }

    return 0;
}
