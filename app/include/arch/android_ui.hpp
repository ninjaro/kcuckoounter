#ifndef KCUCKOOUNTER_ARCH_ANDROID_UI_HPP
#define KCUCKOOUNTER_ARCH_ANDROID_UI_HPP

#include "arch/widget_helpers.hpp"

class QSlider;

enum class android_button_profile {
    primary,
    compact_action,
    inline_info,
    quiz_action
};

namespace android_ui {

BasePushButton* create_button(BaseWidget* parent = nullptr);
void apply_button_style(BasePushButton* button, android_button_profile profile);
void apply_spin_box_style(BaseSpinBox* spin_box);
void apply_combo_box_style(BaseComboBox* combo_box);
void apply_check_box_style(BaseCheckBox* check_box);
void apply_slider_style(QSlider* slider);
void apply_toolbar_style(BaseToolBar* toolbar);

} // namespace android_ui

#endif // KCUCKOOUNTER_ARCH_ANDROID_UI_HPP
