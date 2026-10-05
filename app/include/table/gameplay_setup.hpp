#ifndef KCUCKOOUNTER_TABLE_GAMEPLAY_SETUP_HPP
#define KCUCKOOUNTER_TABLE_GAMEPLAY_SETUP_HPP

#include "arch/widget_helpers.hpp"
#include "table/gameplay_layout.hpp"
#include "table/gameplay_session.hpp"
#include <optional>

class QLabel;
struct strategy_catalog;

namespace gameplay {

// Native setup view, not another preferences/drill model. The host must keep
// the session alive at the same address and refresh after external mutations.
// Only setup edits are accepted; Pause never unlocks gameplay configuration.
class setup_widget : public BaseWidget {
    Q_OBJECT

public:
    explicit setup_widget(session& owner, BaseWidget* parent = nullptr);
    void refresh();
    // Host feeds actual usable table/theme/orientation on coalesced setup
    // geometry changes, not this form's size. Invalid/live requests are inert.
    [[nodiscard]] bool update_table_geometry(
        packing::extent usable_table, packing::extent item,
        packing::orientation_constraint orientation
        = packing::orientation_constraint::allow_rotation,
        double minimum_short_side = recommended_slot_short_side
    );

signals:
    // Emitted once after an accepted, non-no-op edit, including any mandatory
    // Lives pause change. The owner controls preparation/launch/persistence.
    void configuration_changed();

private:
    void apply_controls();

    session& owner;
    QLabel* recommendation_label;
    BaseCheckBox* allow_extra_slots;
    QLabel* slot_limit_warning;
    BaseComboBox* failure;
    BaseSpinBox* lives;
    QLabel* lives_label;
    QLabel* pause_explanation;
    BaseComboBox* source;
    BaseComboBox* scope;
    BaseCheckBox* global_pause;
    BaseCheckBox* allow_skip;
    BaseComboBox* dealing;
    BaseSpinBox* sequential_count;
    QLabel* sequential_label;
    BaseWidget* sequential_row;
    QLabel* roster_count;
    bool refreshing = false;
    bool minimum_slot_size_met = true;
};

// Native per-deck projection of the same host-owned session. Binding follows
// deck identity through Swap, not a physical-slot position. The owner/catalog
// must outlive this view at stable addresses. Construction/refresh NEVER apply
// recommendations to an existing choice; use create_fresh_session explicitly
// for new preset defaults before constructing the view.
class deck_setup_widget : public BaseWidget {
    Q_OBJECT

public:
    enum class edit_mode { immediate, staged };
    explicit deck_setup_widget(
        session& owner, deck_id id, const strategy_catalog& catalog,
        BaseWidget* parent = nullptr, edit_mode mode = edit_mode::immediate
    );
    void refresh();
    // Staged hosts keep only a small edit draft, not a second session. Phase
    // or external settings changes invalidate it; Swap/preparation do not.
    [[nodiscard]] bool pending_changes_current() const;
    // Interprets native pending input, commits before notifications. Returns
    // false for non-staged/stale drafts; destruction discards unapplied edits.
    [[nodiscard]] bool apply_pending_changes();

signals:
    // Accepted non-no-op configuration edits, including domain invalidation.
    void configuration_changed();
    // Training Show Count toggle; never regenerate shoes for this.
    void show_count_changed();

private:
    void apply_configuration();
    void apply_show_count();

    struct pending_edit {
        deck_configuration original;
        deck_configuration configuration;
        bool original_show_count;
        bool show_count;
        session_phase phase;
    };

    session& owner;
    deck_id id;
    const strategy_catalog& catalog;
    std::optional<pending_edit> pending;
    BaseComboBox* strategy;
    BaseSpinBox* deck_count;
    BaseCheckBox* infinite;
    BaseCheckBox* training;
    BaseCheckBox* show_count;
    QLabel* recommendation;
    bool refreshing = false;
};

} // namespace gameplay

#endif // KCUCKOOUNTER_TABLE_GAMEPLAY_SETUP_HPP
