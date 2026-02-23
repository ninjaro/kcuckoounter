#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_PACKER_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_PACKER_HPP

#include <tuple>
#include <vector>

/**
 * @brief Describes the placement of a single card inside the packing area.
 *
 * The coordinate system is a simple 2D Cartesian grid measured in the same
 * units as the arguments passed to card_packer::pack().
 *
 * - The origin (0, 0) is at the top-left corner of the packed rectangle.
 * - The x axis grows to the right.
 * - The y axis grows downward.
 *
 * A card is always axis-aligned (no arbitrary rotation; only 0 or 90 degrees).
 * The effective width and height of the card depend on the global scale chosen
 * by card_packer::pack() and on the @ref placed_card::rotated flag:
 *
 * - Let H0 = card_ratio.first  and W0 = card_ratio.second.
 * - Let s be the final scale returned by card_packer::pack().
 *
 * If rotated == false:
 *   - width  = s * H0  (along +X),
 *   - height = s * W0  (along +Y).
 *
 * If rotated == true:
 *   - width  = s * W0  (along +X),
 *   - height = s * H0  (along +Y).
 *
 * The (x, y) fields always store the top-left corner of the card's bounding
 * rectangle in this coordinate system.
 */
struct placed_card {
    /**
     * @brief X coordinate of the top-left corner of the card.
     *
     * Measured in the same units as the width parameter passed to
     * card_packer::pack(). Guaranteed to satisfy:
     *
     *  0 <= x
     *  x + effective_card_width <= pack(width, height).first argument's width
     *  (for all returned cards in the final result).
     */
    double x;

    /**
     * @brief Y coordinate of the top-left corner of the card.
     *
     * Measured in the same units as the height parameter passed to
     * card_packer::pack(). Guaranteed to satisfy:
     *
     *  0 <= y
     *  y + effective_card_height <= pack(width, height).first argument's height
     *  (for all returned cards in the final result).
     */
    double y;

    /**
     * @brief Orientation flag of the card.
     *
     * - false: the card is placed in its horizontal orientation:
     *          width  = scale * card_ratio.first
     *          height = scale * card_ratio.second
     *
     * - true:  the card is rotated by 90 degrees into vertical orientation:
     *          width  = scale * card_ratio.second
     *          height = scale * card_ratio.first
     *
     * The actual value of the scale is determined by card_packer::pack().
     */
    bool rotated;
};

/**
 * @brief Packs a fixed number of equal-sized cards into a rectangular area.
 *
 * This class computes a uniform isotropic scale factor and axis-aligned
 * positions for a requested number of identical rectangular cards so that:
 *
 *  - every card lies completely inside the [0, width] x [0, height]
 *    bounding box passed to card_packer::pack(),
 *  - cards do not overlap at the scale and positions chosen by the algorithm,
 *  - the scale is as large as possible under these constraints
 *    (within a fixed numeric tolerance),
 *  - the returned vector contains at most @ref card_count placements.
 *
 * The algorithm:
 *  - treats a card as having a fixed aspect ratio (see @ref card_ratio),
 *  - performs a binary search on the uniform scale factor in the range
 *    [0, max_scale(width, height, card_count)],
 *  - for each candidate scale, uses a recursive "frame" filling pattern that
 *    tries to place cards along the outer boundary of the area (forming one
 *    or more rectangular frames) and then recurses into the remaining
 *    inner region,
 *  - keeps track of all successfully placed cards for that scale.
 *
 * The final call to pack() returns:
 *  - the largest scale for which the recursion was able to place at least
 *    @ref card_count cards in total,
 *  - a vector of up to card_count @ref placed_card entries.
 *
 * **Important detail about the returned vector:**
 *
 * The internal recursion tends to place border "frame" cards first and more
 * central cards later. After the final packing pass, the implementation keeps
 * only the *tail* (last) card_count placements from the chronological
 * placement order. This typically results in a more centered cluster of cards
 * being returned to the caller, rather than the entire outer frame.
 *
 * Usage:
 *
 * @code
 *   card_packer packer(52);               // target: 52 cards
 *   auto [scale, cards] = packer.pack(    // width = 800, height = 600
 *       800.0,
 *       600.0
 *   );
 *   // 'scale' is the common scale factor for all cards.
 *   // 'cards' contains up to 52 placed_card instances.
 * @endcode
 *
 * Thread-safety:
 *  - card_packer holds mutable state and is not thread-safe.
 *  - do not call pack() concurrently from multiple threads on the same
 *    instance.
 */
class card_packer {
public:
    /**
     * @brief Constructs a card_packer for a fixed number of cards.
     *
     * @param card_count
     *        The desired number of cards the caller wants to place.
     *        pack() will try to find the maximal common scale at which at
     *        least @p card_count cards fit into the given rectangle. The final
     *        returned vector will contain at most @p card_count placements.
     *
     * @note The constructor does not perform any heavy computations. All
     *       packing work is done inside pack().
     */
    explicit card_packer(size_t card_count);

    /**
     * @brief Pack cards into a rectangle and compute the maximal uniform scale.
     *
     * This function drives the packing process:
     *  - it performs a binary search on the uniform scale factor,
     *  - for each candidate scale it calls the internal recursive routine
     *    cards_init() to populate a working buffer of placements,
     *  - it chooses the largest scale for which at least card_count cards
     *    can be placed,
     *  - on the final pass, it collects all placements from cards_init();
     *    if more than card_count cards were placed, it retains at most
     *      card_count of them (currently the ones closest to (0,0) in
     *      the packing coordinate system) and discards the rest. The
     *      order of the returned placements is not specified.

     *
     * @param width
     *        Width of the outer packing rectangle in arbitrary units.
     *        Must be strictly positive.
     *
     * @param height
     *        Height of the outer packing rectangle in arbitrary units.
     *        Must be strictly positive.
     *
     * @return A tuple (scale, placements) where:
     *         - scale >= 0 is the uniform scale factor applied to both
     *           dimensions of the base card size given by card_ratio.
     *         - placements is a vector of up to card_count @ref placed_card
     *           instances describing the positions and orientations of the
     *           cards in the final configuration.
     *
     * @warning The function assumes that width and height are finite and
     *          non-negative. Zero or extremely small dimensions may result in
     *          an empty placement set and a zero or tiny scale.
     */
    std::tuple<double, std::vector<placed_card>>
    pack(double width, double height);

private:
    /**
     * @brief Fill a vertical strip of the current region with rotated cards.
     *
     * This helper places cards aligned along the Y axis, treating the long
     * side of the card as the step along Y. It is typically used to create
     * vertical parts of a rectangular frame.
     *
     * @param offset
     *        Starting offset along the Y axis relative to the local origin
     *        (x, y) of the current sub-rectangle. The first card is attempted
     *        at y + offset.
     *
     * @param height
     *        Height of the current local sub-rectangle in the same units as
     *        the top-level height passed to pack().
     *
     * @param card_width
     *        The effective (scaled) long side of the card. This value is used
     *        as the vertical step between placed cards along the strip.
     *
     * @param x
     *        Local origin X coordinate of the current sub-rectangle.
     *
     * @param y
     *        Local origin Y coordinate of the current sub-rectangle.
     *
     * @return The final offset value at which no more cards could be placed
     *         (i.e. the offset used for the last failed placement attempt).
     */
    double fill_vertical(
        double offset, double height, double card_width, double x, double y
    );

    /**
     * @brief Fill a horizontal strip of the current region with non-rotated
     * cards.
     *
     * This helper places cards aligned along the X axis, treating the long
     * side of the card as the step along X. It is typically used to create
     * horizontal parts of a rectangular frame.
     *
     * @param offset
     *        Starting offset along the X axis relative to the local origin
     *        (x, y) of the current sub-rectangle. The first card is attempted
     *        at x + offset.
     *
     * @param width
     *        Width of the current local sub-rectangle in the same units as
     *        the top-level width passed to pack().
     *
     * @param card_width
     *        The effective (scaled) long side of the card. This value is used
     *        as the horizontal step between placed cards along the strip.
     *
     * @param x
     *        Local origin X coordinate of the current sub-rectangle.
     *
     * @param y
     *        Local origin Y coordinate of the current sub-rectangle.
     *
     * @return The final offset value at which no more cards could be placed
     *         (i.e. the offset used for the last failed placement attempt).
     */
    double fill_horizontal(
        double offset, double width, double card_width, double x, double y
    );

    /**
     * @brief Recursive core of the frame-based packing algorithm.
     *
     * The function operates on a sub-rectangle of the overall packing area,
     * defined by its size (width, height) and local origin (x, y). For the
     * current global scale:
     *
     *  - It checks whether at least one card can fit into the sub-rectangle
     *    in either orientation. If not, it returns immediately.
     *  - It computes how many full steps of the card's long side fit along
     *    the sub-rectangle dimensions and examines the remaining "gaps"
     *    (leftover width/height).
     *  - Depending on which side can host an additional strip of rotated
     *    cards, it decides between several patterns:
     *      * single side frame,
     *      * corner frame,
     *      * full rectangular frame,
     *      * or only a partial frame (top/bottom or left/right).
     *  - It places the corresponding strips of cards using fillh() and
     *    fillv(), updates the global placement buffer and the @ref index,
     *    and then recurses into the interior sub-rectangle if any area
     *    remains.
     *
     * The caller (pack()) is responsible for resetting @ref index and ensuring
     * that the @ref cards buffer is large enough before invoking cards_init().
     *
     * @param width
     *        Width of the current sub-rectangle.
     *
     * @param height
     *        Height of the current sub-rectangle.
     *
     * @param x
     *        X coordinate of the top-left corner of the current sub-rectangle
     *        in the global coordinate system. Defaults to 0 for the outermost
     *        call.
     *
     * @param y
     *        Y coordinate of the top-left corner of the current sub-rectangle
     *        in the global coordinate system. Defaults to 0 for the outermost
     *        call.
     */
    void cards_init(double width, double height, double x = 0, double y = 0);

    /**
     * @brief Compute a theoretical upper bound for the uniform card scale.
     *
     * This helper computes an optimistic upper bound on the scale at which
     * @p count cards of base size card_ratio could possibly fit into a
     * rectangle of size (width x height), assuming an ideal packing by area:
     *
     *  - Let H0 = card_ratio.first and W0 = card_ratio.second.
     *  - At scale s, a single card occupies area s^2 * W0 * H0.
     *  - The whole region has area width * height.
     *  - Requiring that count * s^2 * W0 * H0 <= width * height yields:
     *
     *    s <= sqrt(width * height / (W0 * H0 * count)).
     *
     * The returned value is exactly this bound and is used as the right edge
     * of the binary search interval in pack().
     *
     * @param width
     *        Width of the outer rectangle.
     *
     * @param height
     *        Height of the outer rectangle.
     *
     * @param count
     *        Target number of cards.
     *
     * @return The maximal theoretical uniform scale factor by pure area
     *         considerations.
     */
    double max_scale(double width, double height, size_t count) const;

    /**
     * @brief Target number of cards requested by the caller.
     *
     * pack() attempts to place at least this many cards. The final returned
     * vector will contain at most this number of placements (or fewer if the
     * geometry makes it impossible to place that many cards at any scale).
     */
    size_t card_count;

    /**
     * @brief Number of placements currently written into @ref cards.
     *
     * This is used by cards_init(), fillh() and fillv() as a simple cursor
     * into the working placement buffer.
     */
    size_t index {};

    /**
     * @brief Base (unscaled) aspect ratio of a card as (height, width).
     *
     * The first component is the long (vertical) side of the card, the second
     * component is the short (horizontal) side. These values are multiplied
     * by the current scale to obtain the actual card dimensions.
     */
    std::pair<int, int> card_ratio { 88, 63 };

    /**
     * @brief Current candidate scale factor.
     *
     * This value is updated during the binary search in pack() and used by
     * cards_init() to derive actual card dimensions for the current step.
     */
    double scale { 1.0 };

    /**
     * @brief Working buffer for all placements during a single pack() pass.
     *
     * The vector is pre-allocated to be large enough for the worst-case number
     * of placements at a given scale. The effective number of used entries is
     * stored in @ref index.
     */
    std::vector<placed_card> cards;
};

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_PACKER_HPP
