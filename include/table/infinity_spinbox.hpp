#ifndef KCUCKOOUNTER_INCLUDE_TABLE_INFINITY_SPINBOX_HPP
#define KCUCKOOUNTER_INCLUDE_TABLE_INFINITY_SPINBOX_HPP

#include "arch/widget_helpers.hpp"

/**
 * @class infinity_spinbox
 * @brief A QSpinBox that can display an "infinity" label instead of a number.
 *
 * When *infinity mode* is enabled via set_infinity_mode(), the spin box
 * ignores the numeric value for presentation and returns a constant textual
 * representation from textFromValue(), e.g. `"inf"`. This affects only how
 * the value is displayed; the underlying integer value returned by
 * QSpinBox::value() remains unchanged and can be used for calculations or
 * persistence.
 *
 * The widget behaves like a regular QSpinBox in all other respects
 * (keyboard / mouse handling, signals, ranges, etc.).
 */
class infinity_spinbox : public BaseSpinBox {
public:
    /**
     * @brief Constructs an infinity_spinbox.
     *
     * The widget behaves like a standard QSpinBox by default, i.e. infinity
     * mode is initially disabled (see infinity_mode()).
     *
     * @param parent Pointer to the parent QWidget, or `nullptr` if the widget
     *               has no parent.
     */
    explicit infinity_spinbox(BaseWidget* parent = nullptr);

    /**
     * @brief Enables or disables infinity mode.
     *
     * When infinity mode is enabled, textFromValue() will return a constant
     * textual representation (such as `"inf"`) instead of the formatted
     * numeric value. When disabled, the standard QSpinBox behavior is used
     * and the current integer value is formatted and displayed normally.
     *
     * This function is idempotent: calling it repeatedly with the same
     * @p enabled value has no effect beyond the first call.
     *
     * @param enabled Set to `true` to enable infinity mode, or `false` to
     *                restore standard numeric display behavior.
     *
     * @see infinity_mode()
     * @see textFromValue()
     */
    void set_infinity_mode(bool enabled);

    /**
     * @brief Returns whether infinity mode is currently enabled.
     *
     * @return `true` if infinity mode is enabled, `false` otherwise.
     *
     * @see set_infinity_mode()
     */
    bool infinity_mode() const;

protected:
    /**
     * @brief Returns the textual representation of the specified value.
     *
     * This function is called by QSpinBox to determine the text that should
     * be displayed for the current integer value. When infinity mode is
     * enabled (see set_infinity_mode()), this override returns a constant
     * string, typically `"inf"`. When infinity mode is disabled, the
     * implementation falls back to QSpinBox::textFromValue(), preserving the
     * default formatting behavior.
     *
     * @param value The integer value associated with the spin box.
     *              In infinity mode this parameter may be ignored for
     *              presentation purposes.
     *
     * @return A QString containing the text to display in the editor.
     */
    QString textFromValue(int value) const override;

private:
    /**
     * @brief Tracks whether infinity mode is enabled.
     *
     * When `true`, textFromValue() returns a constant string instead of the
     * numeric value. When `false`, the spin box behaves like a standard
     * QSpinBox with respect to textual representation.
     *
     * @see set_infinity_mode()
     * @see infinity_mode()
     */
    bool infinity_mode_flag;
};

#endif // KCUCKOOUNTER_INCLUDE_TABLE_INFINITY_SPINBOX_HPP
