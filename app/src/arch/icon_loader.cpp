#include "arch/icon_loader.hpp"

#include <shared_ui/qt/icons.hpp>

QIcon icon_loader::themed(
    const std::initializer_list<const char*> names,
    const QStyle::StandardPixmap fallback
) {
    return shared_ui::qt::themed_icon(names, fallback);
}
