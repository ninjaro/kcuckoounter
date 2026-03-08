#ifndef KCUCKOOUNTER_ARCH_NUM_HELPERS_HPP
#define KCUCKOOUNTER_ARCH_NUM_HELPERS_HPP

#include <QtGlobal>

#include <limits>

namespace num_helpers {

inline int to_int(qsizetype value) {
    return value > static_cast<qsizetype>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(value);
}

} // namespace num_helpers

#endif // KCUCKOOUNTER_ARCH_NUM_HELPERS_HPP
