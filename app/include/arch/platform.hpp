#ifndef KCUCKOOUNTER_ARCH_PLATFORM_HPP
#define KCUCKOOUNTER_ARCH_PLATFORM_HPP

#include <QtGlobal>

// KC_ANDROID selects the phone profile: touch shell, maximized windows and
// mobile lifecycle. Android and iOS builds get it from their platform; a
// desktop build can set it to run the same profile natively (tests, captures).
#if (defined(Q_OS_ANDROID) || defined(Q_OS_IOS)) && !defined(KC_ANDROID)
#define KC_ANDROID 1
#endif

#endif // KCUCKOOUNTER_ARCH_PLATFORM_HPP
