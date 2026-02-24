#include "table/infinity_spinbox.hpp"

#include "arch/str_label.hpp"

#include <QLineEdit>

infinity_spinbox::infinity_spinbox(BaseWidget* parent)
    : BaseSpinBox(parent)
    , infinity_mode_flag(false) { }

void infinity_spinbox::set_infinity_mode(bool enabled) {
    if (infinity_mode_flag == enabled) {
        return;
    }

    infinity_mode_flag = enabled;
    lineEdit()->setText(textFromValue(value()));
    update();
}

bool infinity_spinbox::infinity_mode() const { return infinity_mode_flag; }

QString infinity_spinbox::textFromValue(int value) const {
    if (infinity_mode_flag) {
        return str_label("inf");
    }

    return BaseSpinBox::textFromValue(value);
}
