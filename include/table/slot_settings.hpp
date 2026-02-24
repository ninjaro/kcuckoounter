#ifndef KCUCKOOUNTER_INCLUDE_TABLE_SLOT_SETTINGS_HPP
#define KCUCKOOUNTER_INCLUDE_TABLE_SLOT_SETTINGS_HPP

#include "arch/widget_helpers.hpp"

class slot_settings : public BaseWidget {
    Q_OBJECT

public:
    explicit slot_settings(
        BaseWidget* parent = nullptr, bool include_info_button = true
    );
    ~slot_settings() override;

    static QSize minimum_settings_size();

    BaseCheckBox* infinity_check_box() const;
    BaseSpinBox* deck_count_spin_box() const;
    BaseComboBox* strategy_combo_box() const;
    BaseCheckBox* show_card_indexing() const;
    BaseCheckBox* show_strategy_name() const;
    BaseCheckBox* training_check_box() const;
    BasePushButton* info_button() const;

private:
    BaseCheckBox* infinity_check_box_internal;
    BaseSpinBox* deck_count_spin_box_internal;
    BaseComboBox* strategy_combo_box_internal;
    BaseCheckBox* show_card_indexing_internal;
    BaseCheckBox* show_strategy_name_internal;
    BaseCheckBox* training_check_box_internal;
    BasePushButton* info_button_internal;

    void setup_ui(bool include_info_button);
};

#endif // KCUCKOOUNTER_INCLUDE_TABLE_SLOT_SETTINGS_HPP
