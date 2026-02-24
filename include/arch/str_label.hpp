#ifndef KCUCKOOUNTER_INCLUDE_ARCH_STR_LABEL_HPP
#define KCUCKOOUNTER_INCLUDE_ARCH_STR_LABEL_HPP

#include <QString>

#ifdef KC_KDE
#include <KLocalizedString>
#define str_label(text) i18n(text)
#else
#define str_label(text) QStringLiteral(text)
#endif

#endif // KCUCKOOUNTER_INCLUDE_ARCH_STR_LABEL_HPP
