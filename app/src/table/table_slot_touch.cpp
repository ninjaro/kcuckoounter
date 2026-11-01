#include "table/table_slot.hpp"
#include <shared_ui/qt/adaptive_surface.hpp>
#include <shared_ui/qt/answer_panel.hpp>
#include <shared_ui/qt/control_overlay.hpp>
#include <shared_ui/qt/item_scene.hpp>
#include <shared_ui/qt/touch_kit.hpp>

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"
#include "table/touch_text.hpp"

#include <QEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>

namespace {
// A right answer's band clears itself; wrong and skipped answers stay until
// dismissed or the deck's next question.
constexpr int right_verdict_ms = 3000;
} // namespace

void table_slot::set_touch_presentation(bool enabled) {
    if (touch_mode == enabled)
        return;
    touch_mode = enabled;
    card_widget_internal->set_deck_frame(enabled);
    if (enabled && !touch_dismiss) {
        auto* dismiss = new shared_ui::qt::touch_button(
            {}, shared_ui::qt::touch_button_style::bare_icon, this
        );
        dismiss->set_icon(shared_ui::qt::touch_icon::close);
        dismiss->setFixedSize(48, 48);
        dismiss->setObjectName(QStringLiteral("touch_dismiss_verdict"));
        dismiss->setAccessibleName(str_label("Dismiss answer result"));
        connect(dismiss, &QAbstractButton::clicked, this, [this] {
            dismiss_touch_verdict();
        });
        touch_dismiss = dismiss;
        touch_verdict_timer = new QTimer(this);
        touch_verdict_timer->setSingleShot(true);
        touch_verdict_timer->setInterval(right_verdict_ms);
        connect(touch_verdict_timer, &QTimer::timeout, this, [this] {
            if (touch_verdict
                && touch_verdict->outcome == gameplay::quiz_outcome::correct)
                dismiss_touch_verdict();
        });
        card_widget_internal->installEventFilter(this);
    }
    if (touch_dismiss)
        touch_dismiss->setVisible(false);
    overlay_widget->setVisible(!enabled);
    update_compact_controls();
    refresh_touch_marks();
}

void table_slot::set_touch_ordinal(const QString& ordinal, bool done) {
    if (touch_ordinal == ordinal && touch_ordinal_done == done)
        return;
    touch_ordinal = ordinal;
    touch_ordinal_done = done;
    refresh_touch_marks();
}

void table_slot::set_touch_dealing(bool dealing) {
    if (touch_live == dealing)
        return;
    touch_live = dealing;
    refresh_touch_marks();
}

void table_slot::dismiss_touch_verdict() {
    if (!touch_verdict)
        return;
    touch_verdict.reset();
    if (touch_verdict_timer)
        touch_verdict_timer->stop();
    refresh_touch_marks();
    // The correction itself stays available to review until dismissed there.
    if (gameplay_feedback)
        on_quiz_continue_button_clicked();
}

void table_slot::refresh_touch_marks() {
    if (!touch_mode || !gameplay_owner)
        return;
    using shared_ui::qt::item_tone;
    const auto* deck = gameplay_owner->deck(gameplay_id);
    card_widget_internal->set_deck_label(
        touch_text::deck_legend(gameplay_id, deck->configuration)
    );
    shared_ui::qt::item_deck_marks marks;
    if (deck->configuration.training)
        marks.tags.append(str_label("TRAINING"));
    const bool asked = gameplay_prompt.has_value();
    // A new question replaces the previous answer's band.
    if (asked && touch_verdict
        && touch_verdict->batch_id != gameplay_prompt->batch_id)
        touch_verdict.reset();
    if (deck->status == gameplay::deck_status::failed) {
        marks.center_tag = str_label("FAILED");
    } else if (deck->status == gameplay::deck_status::completed) {
        marks.band_paper = true;
        marks.dim = true;
        marks.band_stamp = str_label("SHOE FINISHED");
        marks.band_line = str_label("final count %1 · not scored")
                              .arg(shared_ui::qt::touch_signed(
                                  static_cast<qint64>(deck->running_count)
                              ));
    } else {
        if (asked) {
            marks.emphasized = true;
            if (!touch_ordinal.isEmpty() && !touch_ordinal_done)
                marks.ordinal = touch_ordinal;
            else
                marks.status = str_label("STOPPED · YOUR ANSWER");
        } else if (!touch_ordinal.isEmpty() && touch_ordinal_done) {
            marks.ordinal = touch_ordinal;
            marks.ordinal_done = true;
        } else if (touch_live) {
            marks.live = str_label("DEALING");
        }
        if (!asked && deck->configuration.training && deck->show_count) {
            marks.figure_caption = str_label("COUNT");
            marks.figure = shared_ui::qt::touch_signed(
                static_cast<qint64>(deck->running_count), true
            );
        }
    }
    if (swap_selected())
        marks.status = str_label("SWAP 1 OF 2");
    const bool band = touch_verdict && !asked
        && deck->status != gameplay::deck_status::completed;
    if (band) {
        const auto& answer = *touch_verdict;
        const bool training = deck->configuration.training;
        const auto expected = shared_ui::qt::touch_signed(
            static_cast<qint64>(answer.expected_count)
        );
        const auto remembered
            = shared_ui::qt::touch_signed(static_cast<qint64>(deck->latest_input
            ));
        QString note;
        switch (answer.outcome) {
        case gameplay::quiz_outcome::correct:
            marks.band_stamp = str_label("RIGHT");
            marks.band_tone = item_tone::positive;
            marks.band_line = str_label("count %1").arg(expected);
            note = training
                ? str_label("Training: no points, no lives at stake")
                : str_label("+%1 points")
                      .arg(static_cast<qulonglong>(answer.normal_cards));
            break;
        case gameplay::quiz_outcome::wrong:
            marks.band_stamp = str_label("WRONG");
            marks.band_tone = item_tone::negative;
            marks.band_line
                = str_label("you %1 · count")
                      .arg(shared_ui::qt::touch_signed(
                          static_cast<qint64>(answer.submitted_count)
                      ));
            marks.band_emphasis = expected;
            if (training)
                note = str_label("Training: no points, no lives at stake");
            else if (gameplay_owner->configuration().failure
                     == gameplay::failure_policy::lives)
                note = str_label("1 life lost · next question starts from %1")
                           .arg(remembered);
            else
                note = str_label("Deck blocked");
            break;
        case gameplay::quiz_outcome::skipped:
            marks.band_stamp = str_label("SKIPPED");
            marks.band_tone = item_tone::negative;
            marks.band_line = str_label("count");
            marks.band_emphasis = expected;
            note = str_label("No points · next question starts from %1")
                       .arg(remembered);
            break;
        }
        if (answer.timed_out)
            note = str_label("Time's up · ") + note;
        marks.band_note = note;
        marks.band_end_reserve = 52;
    }
    card_widget_internal->set_deck_marks(marks);
    if (touch_dismiss) {
        touch_dismiss->setVisible(band);
        place_touch_dismiss();
    }
}

void table_slot::place_touch_dismiss() {
    if (!touch_dismiss)
        return;
    const QRectF frame = card_widget_internal->deck_frame_rect();
    touch_dismiss->move(
        static_cast<int>(frame.right()) - 4 - touch_dismiss->width(),
        static_cast<int>(frame.bottom()) - 64 + 8
    );
    touch_dismiss->raise();
}
