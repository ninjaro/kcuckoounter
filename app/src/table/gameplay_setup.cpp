#include "table/gameplay_setup.hpp"

#include "arch/str_label.hpp"
#include "settings/strategy_data.hpp"
#include "table/gameplay_strategy.hpp"

#include <QLabel>
#include <QPointer>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace gameplay {
namespace {

    // Data stores domain enums rather than relying on translated labels or
    // order.
    template <typename Enum>
    void add_option(BaseComboBox* combo, const QString& label, Enum value) {
        combo->addItem(label, static_cast<int>(value));
    }

    template <typename Enum>
    void select_option(BaseComboBox* combo, Enum value) {
        combo->setCurrentIndex(combo->findData(static_cast<int>(value)));
    }

    void identify(QWidget* control, const char* name, const QString& label) {
        control->setObjectName(QString::fromLatin1(name));
        control->setAccessibleName(label);
    }

} // namespace

setup_widget::setup_widget(session& owner_value, BaseWidget* parent)
    : shared_ui::qt::settings_section({}, parent)
    , owner(owner_value)
    , recommendation_label(nullptr)
    , slot_limit_warning(nullptr)
    , pause_explanation(nullptr)
    , sequential_count(new BaseSpinBox(this))
    , sequential_row(nullptr)
    , roster_count(new QLabel(this)) {
    setObjectName(QStringLiteral("gameplay_setup"));
    set_fields_expanding(true);

    recommendation_label = add_message();
    identify(
        recommendation_label, "gameplay_slot_recommendation",
        str_label("Recommended table capacity")
    );
    allow_extra_slots = add_control<BaseCheckBox>(
        str_label("Allow more slots than recommended"),
        QStringLiteral("gameplay_allow_extra_slots"),
        str_label(
            "Allow a denser table than recommended, but never bypass the "
            "technical safety cap. Choose fewer slots before turning this off "
            "when above the recommendation."
        )
    );
    slot_limit_warning = add_message();
    identify(
        slot_limit_warning, "gameplay_slot_limit_warning",
        str_label("Table capacity guidance")
    );

    failure = add_control<BaseComboBox>(
        str_label("Failure policy"), QStringLiteral("gameplay_failure_policy"),
        str_label(
            "Lives are shared by the session. Block stops only the wrong deck. "
            "Training decks do not lose lives or fail."
        )
    );
    add_option(failure, str_label("Lives"), failure_policy::lives);
    add_option(failure, str_label("Block deck"), failure_policy::block);
    lives = add_control<BaseSpinBox>(
        str_label("Starting lives"), QStringLiteral("gameplay_initial_lives"),
        str_label(
            "Start with 1 to 12 shared lives. Wrong non-Training answers lose "
            "a life."
        )
    );
    lives->setRange(1, 12);
    source = add_control<BaseComboBox>(
        str_label("Quiz source"), QStringLiteral("gameplay_quiz_source"),
        str_label(
            "Physical Jokers are cards in the shoe. Virtual questions "
            "interrupt a shoe without Jokers."
        )
    );
    add_option(
        source, str_label("Physical Jokers"), quiz_source::physical_joker
    );
    add_option(
        source, str_label("Virtual questions"), quiz_source::virtual_interrupt
    );
    scope = add_control<BaseComboBox>(
        str_label("Quiz scope"), QStringLiteral("gameplay_quiz_scope"),
        str_label("Single asks the affected deck. Multi asks all active decks.")
    );
    add_option(scope, str_label("Single question"), quiz_scope::single);
    add_option(scope, str_label("Multi question"), quiz_scope::multi);
    global_pause = add_control<BaseCheckBox>(
        str_label("Pause for all answers"),
        QStringLiteral("gameplay_global_pause"),
        str_label(
            "Freeze all dealing during a quiz. Without this, only queried "
            "decks wait. Lives always requires global quiz pause."
        )
    );
    pause_explanation = add_message();
    pause_explanation->setObjectName(
        QStringLiteral("gameplay_pause_explanation")
    );

    allow_skip = add_control<BaseCheckBox>(
        str_label("Allow skipping questions"),
        QStringLiteral("gameplay_allow_skip"),
        str_label(
            "Skip shows the expected count without credit, an error, a lost "
            "life or a failed deck."
        )
    );
    dealing = add_control<BaseComboBox>(
        str_label("Dealing mode"), QStringLiteral("gameplay_dealing_mode"),
        str_label(
            "Sequential deals in table traversal order; Random selects one "
            "eligible deck; Simultaneous deals to every eligible deck."
        )
    );
    add_option(dealing, str_label("Sequential"), dealing_mode::sequential);
    add_option(dealing, str_label("Random"), dealing_mode::random);
    add_option(dealing, str_label("Simultaneous"), dealing_mode::simultaneous);
    identify(
        sequential_count, "gameplay_sequential_count",
        str_label("Cards per step")
    );
    sequential_count->setToolTip(str_label(
        "Reveal one card on up to N distinct eligible decks per step, in table "
        "traversal order. N need not divide M."
    ));
    sequential_row = field(add_inline_row(
        str_label("Cards per step (N/M)"), { sequential_count, roster_count }
    ));

    for (auto* combo : { failure, source, scope, dealing }) {
        connect(combo, &BaseComboBox::currentIndexChanged, this, [this](int) {
            apply_controls();
        });
    }
    for (auto* spin : { lives, sequential_count }) {
        connect(spin, &BaseSpinBox::valueChanged, this, [this](int) {
            apply_controls();
        });
    }
    for (auto* check : { allow_extra_slots, global_pause, allow_skip }) {
        connect(check, &BaseCheckBox::toggled, this, [this](bool) {
            apply_controls();
        });
    }
    refresh();
}

bool setup_widget::update_table_geometry(
    packing::extent usable_table, packing::extent item,
    packing::orientation_constraint orientation, double minimum_short_side
) {
    if (owner.phase() != session_phase::setup)
        return false;
    const auto recommendation
        = recommend_slots(usable_table, item, orientation, minimum_short_side);
    if (!recommendation || !owner.update_slot_limits(recommendation->bounds))
        return false;
    minimum_slot_size_met = recommendation->minimum_size_met;
    refresh();
    return true;
}

void setup_widget::refresh() {
    refreshing = true;
    const auto& settings = owner.configuration();
    const auto bounds = owner.slot_constraints();
#ifdef KC_KDE
    recommendation_label->setText(i18n(
        "Recommended maximum: %1 table slots",
        static_cast<qulonglong>(bounds.recommended)
    ));
    recommendation_label->setToolTip(i18n(
        "Technical safety cap: %1 slots. The recommendation override does not "
        "change this cap.",
        static_cast<qulonglong>(bounds.technical_cap)
    ));
#else
    recommendation_label->setText(
        str_label("Recommended maximum: %1 table slots")
            .arg(QString::number(static_cast<qulonglong>(bounds.recommended)))
    );
    recommendation_label->setToolTip(
        str_label(
            "Technical safety cap: %1 slots. The recommendation override does "
            "not change this cap."
        )
            .arg(QString::number(static_cast<qulonglong>(bounds.technical_cap)))
    );
#endif
    allow_extra_slots->setChecked(settings.allow_extra_slots);
    QStringList guidance;
    if (!minimum_slot_size_met) {
        guidance.append(str_label(
            "Even one slot is smaller than the readability target. Enlarge the "
            "table area; the recommendation falls back to one slot."
        ));
    }
    if (owner.size() > bounds.recommended) {
        guidance.append(
            settings.allow_extra_slots
                ? str_label(
                      "This table exceeds the recommendation. The explicit "
                      "override is enabled; the technical cap still applies."
                  )
                : str_label(
                      "This table exceeds the recommendation. Allow more slots "
                      "or choose fewer before starting. Existing decks are "
                      "kept."
                  )
        );
    }
    slot_limit_warning->setText(guidance.join(QLatin1Char('\n')));
    slot_limit_warning->setVisible(!guidance.isEmpty());
    select_option(failure, settings.failure);
    lives->setValue(settings.initial_lives);
    select_option(source, settings.source);
    select_option(scope, settings.scope);
    global_pause->setChecked(settings.global_quiz_pause);
    allow_skip->setChecked(settings.allow_skip);
    select_option(dealing, settings.dealing);
    // QSpinBox's representable range is not a gameplay/roster safety cap.
    // R-006 calibrates the separate, provisional desktop slot policy.
    const auto maximum = std::min(
        owner.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())
    );
    sequential_count->setRange(1, static_cast<int>(maximum));
    sequential_count->setValue(
        static_cast<int>(std::min(settings.sequential_count, maximum))
    );
#ifdef KC_KDE
    roster_count->setText(
        i18n("of %1 decks (M)", static_cast<qulonglong>(owner.size()))
    );
#else
    roster_count->setText(
        str_label("of %1 decks (M)")
            .arg(QString::number(static_cast<qulonglong>(owner.size())))
    );
#endif

    const bool editable = owner.phase() == session_phase::setup;
    const bool uses_lives = settings.failure == failure_policy::lives;
    const bool sequential = settings.dealing == dealing_mode::sequential;
    allow_extra_slots->setEnabled(editable);
    failure->setEnabled(editable);
    lives->setEnabled(editable && uses_lives);
    set_field_visible(lives, uses_lives);
    source->setEnabled(editable);
    scope->setEnabled(editable);
    global_pause->setEnabled(editable && !uses_lives);
    pause_explanation->setText(
        uses_lives ? str_label(
                         "Lives requires global quiz pause. All already-open "
                         "answers are processed before Game Over."
                     )
                   : str_label(
                         "Block supports either pause policy for Single and "
                         "Multi questions."
                     )
    );
    allow_skip->setEnabled(editable);
    dealing->setEnabled(editable);
    sequential_count->setEnabled(
        editable && sequential && settings.sequential_count <= maximum
    );
    set_field_visible(sequential_row, sequential);
    refreshing = false;
}

void setup_widget::apply_controls() {
    if (refreshing)
        return;
    for (const auto* combo : { failure, source, scope, dealing }) {
        if (combo->currentIndex() < 0) {
            refresh();
            return;
        }
    }
    // Begin with the domain record; preserve settings not owned by the view
    // and inactive N/lives. The override bypasses only the recommendation.
    auto candidate = owner.configuration();
    candidate.allow_extra_slots = allow_extra_slots->isChecked();
    candidate.failure
        = static_cast<failure_policy>(failure->currentData().toInt());
    if (candidate.failure == failure_policy::lives)
        candidate.initial_lives = lives->value();
    candidate.source = static_cast<quiz_source>(source->currentData().toInt());
    candidate.scope = static_cast<quiz_scope>(scope->currentData().toInt());
    candidate.global_quiz_pause = candidate.failure == failure_policy::lives
        || global_pause->isChecked();
    candidate.allow_skip = allow_skip->isChecked();
    candidate.dealing
        = static_cast<dealing_mode>(dealing->currentData().toInt());
    if (candidate.dealing == dealing_mode::sequential
        && candidate.sequential_count
            <= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        candidate.sequential_count
            = static_cast<std::size_t>(sequential_count->value());
    const bool changed
        = candidate != owner.configuration() && owner.configure(candidate);
    // Also refresh on rejected edits/phase changes. Disabled controls alone
    // must never be trusted as the configuration lock.
    refresh();
    if (changed)
        emit configuration_changed();
}

deck_setup_widget::deck_setup_widget(
    session& owner_value, deck_id id_value,
    const strategy_catalog& catalog_value, BaseWidget* parent, edit_mode mode
)
    : shared_ui::qt::settings_section({}, parent)
    , owner(owner_value)
    , id(id_value)
    , catalog(catalog_value)
    , recommendation(nullptr) {
    if (mode == edit_mode::staged) {
        if (const auto* state = owner.deck(id))
            pending = pending_edit { state->configuration, state->configuration,
                                     state->show_count, state->show_count,
                                     owner.phase() };
    }
    setObjectName(QStringLiteral("gameplay_deck_setup"));
    set_fields_expanding(true);

    strategy = add_control<BaseComboBox>(
        str_label("Weight strategy"), QStringLiteral("gameplay_deck_strategy"),
        str_label(
            "Strategy weights and any starting count are applied automatically "
            "at preparation. Changing strategy keeps your chosen deck count."
        )
    );
    strategy->setPlaceholderText(str_label("Choose a strategy"));
    strategy->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon
    );
    strategy->setMinimumContentsLength(12);
    if (catalog.is_valid()) {
        for (const auto& entry : catalog.strategies)
            strategy->addItem(entry.name, entry.slug);
    }

    deck_count = add_control<BaseSpinBox>(
        str_label("Card decks"), QStringLiteral("gameplay_deck_count"),
        str_label(
            "Card decks in this shoe, not table slots. Fewer than the strategy "
            "recommendation are allowed. In infinite play this choice still "
            "sets the initial count; internal buffer size is independent. "
            "Preparation checks the session's aggregate resource limits."
        )
    );
    deck_count->setRange(1, std::numeric_limits<int>::max());
    deck_count->setKeyboardTracking(false);
    recommendation = add_message();
    identify(
        recommendation, "gameplay_deck_recommendation",
        str_label("Strategy deck-count guidance")
    );

    infinite = add_control<BaseCheckBox>(
        str_label("Infinite deck"), QStringLiteral("gameplay_deck_infinite"),
        str_label(
            "Generate a continuing stream in bounded shuffled chunks. Running "
            "count and progress continue across chunks; the starting count is "
            "applied only once."
        )
    );
    training = add_control<BaseCheckBox>(
        str_label("Training mode"), QStringLiteral("gameplay_deck_training"),
        str_label(
            "Practice without verified-card score, errors, lost lives or "
            "failure. Training is chosen before Start."
        )
    );
    show_count = add_control<BaseCheckBox>(
        str_label("Show count"), QStringLiteral("gameplay_deck_show_count"),
        str_label(
            "Show the expected running count on this Training deck. Change "
            "this only before Start or during manual Pause."
        )
    );
    connect(strategy, &BaseComboBox::currentIndexChanged, this, [this](int) {
        apply_configuration();
    });
    connect(deck_count, &BaseSpinBox::valueChanged, this, [this](int) {
        apply_configuration();
    });
    for (auto* check : { infinite, training }) {
        connect(check, &BaseCheckBox::toggled, this, [this](bool) {
            apply_configuration();
        });
    }
    connect(show_count, &BaseCheckBox::toggled, this, [this](bool) {
        apply_show_count();
    });
    refresh();
}

void deck_setup_widget::refresh() {
    refreshing = true;
    const auto* state = owner.deck(id);
    if (!state) {
        strategy->setCurrentIndex(-1);
        deck_count->setValue(1);
        for (auto* check : { infinite, training, show_count })
            check->setChecked(false);
        for (auto* control : std::array<QWidget*, 5> {
                 strategy, deck_count, infinite, training, show_count })
            control->setEnabled(false);
        recommendation->setText(str_label("This deck is unavailable."));
        refreshing = false;
        return;
    }
    const auto& configuration
        = pending ? pending->configuration : state->configuration;
    strategy->setCurrentIndex(
        strategy->findData(QString::fromStdString(configuration.strategy_slug))
    );
    const auto maximum
        = static_cast<std::size_t>(std::numeric_limits<int>::max());
    deck_count->setValue(
        static_cast<int>(std::min(configuration.deck_count, maximum))
    );
    infinite->setChecked(configuration.infinite);
    training->setChecked(configuration.training);
    show_count->setChecked(pending ? pending->show_count : state->show_count);

    QStringList guidance;
    const auto parameters = resolve_strategy(catalog, configuration);
    if (parameters) {
#ifdef KC_KDE
        guidance.append(i18n(
            "Strategy recommends %1 card decks. Fewer are allowed for "
            "practice.",
            static_cast<qulonglong>(parameters->recommended_decks)
        ));
#else
        guidance.append(
            str_label(
                "Strategy recommends %1 card decks. Fewer are allowed for "
                "practice."
            )
                .arg(
                    QString::number(
                        static_cast<qulonglong>(parameters->recommended_decks)
                    )
                )
        );
#endif
    } else if (!catalog.is_valid()) {
        guidance.append(str_label(
            "Strategy data is unavailable. Your existing choices are kept."
        ));
        recommendation->setToolTip(catalog.diagnostic_summary());
    } else {
#ifdef KC_KDE
        guidance.append(i18n(
            "Strategy '%1' cannot be prepared with this count. Choose an "
            "available strategy/count; your existing choices are kept.",
            QString::fromStdString(configuration.strategy_slug)
        ));
#else
        guidance.append(
            str_label(
                "Strategy '%1' cannot be prepared with this count. Choose an "
                "available strategy/count; your existing choices are kept."
            )
                .arg(QString::fromStdString(configuration.strategy_slug))
        );
#endif
    }
    if (catalog.is_valid())
        recommendation->setToolTip(QString());
    if (configuration.deck_count > maximum) {
#ifdef KC_KDE
        guidance.append(i18n(
            "Current choice: %1 card decks. This control cannot edit that "
            "large a value; unrelated edits keep it unchanged.",
            static_cast<qulonglong>(configuration.deck_count)
        ));
#else
        guidance.append(
            str_label(
                "Current choice: %1 card decks. This control cannot edit that "
                "large a value; unrelated edits keep it unchanged."
            )
                .arg(
                    QString::number(
                        static_cast<qulonglong>(configuration.deck_count)
                    )
                )
        );
#endif
    }
    recommendation->setText(guidance.join(QLatin1Char('\n')));
    const bool current = !pending || pending_changes_current();
    const bool editable = current && owner.phase() == session_phase::setup;
    strategy->setEnabled(editable && catalog.is_valid());
    deck_count->setEnabled(editable && configuration.deck_count <= maximum);
    infinite->setEnabled(editable);
    training->setEnabled(editable);
    show_count->setVisible(configuration.training);
    show_count->setEnabled(
        current && configuration.training
        && (editable || owner.phase() == session_phase::paused)
    );
    if (!current)
        recommendation->setText(str_label(
            "This settings draft is no longer current. Close and reopen it."
        ));
    refreshing = false;
}

bool deck_setup_widget::pending_changes_current() const {
    const auto* state = owner.deck(id);
    return pending && state && owner.phase() == pending->phase
        && (owner.phase() == session_phase::setup
            || owner.phase() == session_phase::paused)
        && state->configuration == pending->original
        && state->show_count == pending->original_show_count;
}

bool deck_setup_widget::apply_pending_changes() {
    if (!pending_changes_current())
        return false;
    // Keyboard tracking is deliberately off. OK must commit text even when
    // the native editor has not yet lost focus.
    deck_count->interpretText();
    if (!pending_changes_current())
        return false;
    const auto candidate = pending->configuration;
    const bool desired_show_count = candidate.training && pending->show_count;
    const bool configured = candidate != pending->original;
    if (configured
        && (owner.phase() != session_phase::setup
            || !resolve_strategy(catalog, candidate)
            || !owner.configure_deck(id, candidate)))
        return false;
    // No event-loop callbacks/notifications occur between these operations.
    // A valid ID, setup/Pause and Training are precisely set_show_count's
    // preconditions, already checked above; configuration clears Challenge's
    // hint itself. Do not touch preparation for a count-only edit.
    if (candidate.training && owner.deck(id)->show_count != desired_show_count)
        (void)owner.set_show_count(id, desired_show_count);
    const bool shown = desired_show_count != pending->original_show_count;
    pending->original = candidate;
    pending->original_show_count = desired_show_count;
    refresh();
    QPointer<deck_setup_widget> lifetime(this);
    if (configured)
        emit configuration_changed();
    if (shown && lifetime)
        emit show_count_changed();
    return true;
}

void deck_setup_widget::apply_configuration() {
    if (refreshing)
        return;
    const auto* state = owner.deck(id);
    if (!state || strategy->currentIndex() < 0
        || (pending && !pending_changes_current())) {
        refresh();
        return;
    }
    auto candidate = pending ? pending->configuration : state->configuration;
    candidate.strategy_slug = strategy->currentData().toString().toStdString();
    if (candidate.deck_count
        <= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        candidate.deck_count = static_cast<std::size_t>(deck_count->value());
    candidate.infinite = infinite->isChecked();
    candidate.training = training->isChecked();
    if (pending) {
        if (owner.phase() == session_phase::setup
            && resolve_strategy(catalog, candidate)) {
            pending->configuration = std::move(candidate);
            if (!pending->configuration.training)
                pending->show_count = false;
        }
        refresh();
        return;
    }
    const bool changed = candidate != state->configuration
        && resolve_strategy(catalog, candidate)
        && owner.configure_deck(id, candidate);
    refresh();
    if (changed)
        emit configuration_changed();
}

void deck_setup_widget::apply_show_count() {
    if (refreshing)
        return;
    const auto* state = owner.deck(id);
    if (pending) {
        if (pending_changes_current() && pending->configuration.training)
            pending->show_count = show_count->isChecked();
        refresh();
        return;
    }
    const bool changed = state && state->show_count != show_count->isChecked()
        && owner.set_show_count(id, show_count->isChecked());
    refresh();
    if (changed)
        emit show_count_changed();
}

} // namespace gameplay
