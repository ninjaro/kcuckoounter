#include "arch/android_ui.hpp"

#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QString>

namespace android_ui_support {

class android_push_button : public BasePushButton {
public:
    explicit android_push_button(BaseWidget* parent = nullptr)
        : BasePushButton(parent) {
        setAutoDefault(false);
        setDefault(false);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setIconSize(QSize(22, 22));
    }
};

QString button_style_sheet(
    int minimum_height, int vertical_padding, int horizontal_padding,
    int border_radius, int font_weight
) {
    return QString(
               "QPushButton {"
               " min-height: %1px;"
               " padding: %2px %3px;"
               " border-radius: %4px;"
               " font-weight: %5;"
               "}"
    )
        .arg(minimum_height)
        .arg(vertical_padding)
        .arg(horizontal_padding)
        .arg(border_radius)
        .arg(font_weight);
}

void apply_android_button_style(
    BasePushButton* button, android_button_profile profile
) {
    if (button == nullptr) {
        return;
    }

    button->setAutoDefault(false);
    button->setDefault(false);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setIconSize(QSize(22, 22));

    if (profile == android_button_profile::primary) {
        button->setStyleSheet(button_style_sheet(48, 10, 18, 14, 600));
        return;
    }

    if (profile == android_button_profile::compact_action) {
        button->setStyleSheet(button_style_sheet(40, 8, 10, 12, 500));
        return;
    }

    if (profile == android_button_profile::inline_info) {
        button->setStyleSheet(button_style_sheet(38, 6, 10, 10, 500));
        return;
    }

    button->setStyleSheet(button_style_sheet(44, 8, 14, 12, 600));
}

void apply_android_spin_box_style(BaseSpinBox* spin_box) {
    if (spin_box == nullptr) {
        return;
    }

    spin_box->setMinimumHeight(48);
    spin_box->setStyleSheet(QStringLiteral(
        "QSpinBox { padding: 8px 12px; }"
        "QSpinBox::up-button, QSpinBox::down-button { width: 36px; }"
    ));
}

void apply_android_combo_box_style(BaseComboBox* combo_box) {
    if (combo_box == nullptr) {
        return;
    }

    combo_box->setMinimumHeight(48);
    combo_box->setStyleSheet(QStringLiteral(
        "QComboBox { padding: 8px 12px; }"
        "QComboBox::drop-down { width: 40px; }"
    ));
}

void apply_android_check_box_style(BaseCheckBox* check_box) {
    if (check_box == nullptr) {
        return;
    }

    check_box->setStyleSheet(QStringLiteral(
        "QCheckBox { spacing: 12px; padding: 8px 0px; }"
        "QCheckBox::indicator { width: 28px; height: 28px; }"
    ));
}

void apply_android_slider_style(QSlider* slider) {
    if (slider == nullptr) {
        return;
    }

    slider->setMinimumHeight(48);
    slider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height: 8px; border-radius: 4px; }"
        "QSlider::handle:horizontal { width: 28px; margin: -10px 0; }"
    ));
}

void apply_android_toolbar_style(BaseToolBar* toolbar) {
    if (toolbar == nullptr) {
        return;
    }

    toolbar->setIconSize(QSize(28, 28));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setMinimumHeight(72);
    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 2px; padding: 2px; }"
        "QToolButton { min-width: 56px; min-height: 62px; padding: 2px; }"
    ));
}

} // namespace android_ui_support

namespace android_ui {

BasePushButton* create_button(BaseWidget* parent) {
#if defined(Q_OS_ANDROID)
    auto* button = new android_ui_support::android_push_button(parent);
#else
    auto* button = new BasePushButton(parent);
#endif

    return button;
}

void apply_button_style(
    BasePushButton* button, android_button_profile profile
) {
#if defined(Q_OS_ANDROID)
    android_ui_support::apply_android_button_style(button, profile);
#else
    Q_UNUSED(button);
    Q_UNUSED(profile);
#endif
}

void apply_spin_box_style(BaseSpinBox* spin_box) {
#if defined(Q_OS_ANDROID)
    android_ui_support::apply_android_spin_box_style(spin_box);
#else
    Q_UNUSED(spin_box);
#endif
}

void apply_combo_box_style(BaseComboBox* combo_box) {
#if defined(Q_OS_ANDROID)
    android_ui_support::apply_android_combo_box_style(combo_box);
#else
    Q_UNUSED(combo_box);
#endif
}

void apply_check_box_style(BaseCheckBox* check_box) {
#if defined(Q_OS_ANDROID)
    android_ui_support::apply_android_check_box_style(check_box);
#else
    Q_UNUSED(check_box);
#endif
}

void apply_slider_style(QSlider* slider) {
#if defined(Q_OS_ANDROID)
    android_ui_support::apply_android_slider_style(slider);
#else
    Q_UNUSED(slider);
#endif
}

void apply_toolbar_style(BaseToolBar* toolbar) {
#if defined(Q_OS_ANDROID)
    android_ui_support::apply_android_toolbar_style(toolbar);
#else
    Q_UNUSED(toolbar);
#endif
}

} // namespace android_ui
