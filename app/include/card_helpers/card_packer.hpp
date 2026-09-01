#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_PACKER_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_PACKER_HPP

#include <cstddef>
#include <tuple>
#include <vector>

// Compatibility value type retained at the application boundary. The packing
// implementation and its richer value types live in the shared packing
// library and do not leak into Qt widget headers.
struct placed_card {
    double x { 0.0 };
    double y { 0.0 };
    bool rotated { false };
};

class card_packer {
public:
    explicit card_packer(std::size_t card_count);

    [[nodiscard]] std::tuple<double, std::vector<placed_card>>
    pack(double width, double height) const;

private:
    std::size_t card_count { 0 };
};

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_PACKER_HPP
