#ifndef KCUCKOOUNTER_ARCH_ASSET_LOCATOR_HPP
#define KCUCKOOUNTER_ARCH_ASSET_LOCATOR_HPP

#include <QString>

// Resolves an immutable asset shipped with kcuckoounter. Explicit user theme
// paths remain the responsibility of their callers.
QString bundled_asset_path(const QString& relative_name);

#endif // KCUCKOOUNTER_ARCH_ASSET_LOCATOR_HPP
