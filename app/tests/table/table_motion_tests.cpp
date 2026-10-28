#include <QVariantAnimation>
// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QLineEdit>
#include <QtTest/QtTest>

#include <array>
#include <memory>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"
#include <shared_ui/qt/presentation.hpp>

using namespace table_test;

void table_tests::gameplay_resize_paths_data() {
    QTest::addColumn<int>("count");
    for (const int count : { 1, 4, 24, 64 })
        QTest::newRow(qPrintable(QStringLiteral("decks-%1").arg(count)))
            << count;
}

void table_tests::gameplay_resize_paths() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned resize timeline");
#endif
    QFETCH(int, count);
    table view;
    set_gameplay_motion_style(view, 200);
    view.resize(1400, 900);
    view.set_card_orientation(card_orientation_mode::horizontal);
    view.show();
    QVERIFY(view.install_gameplay_session(
        owned_table_fixture(static_cast<std::size_t>(count))
    ));
    QCoreApplication::processEvents();
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    auto& owner = *view.active_gameplay_session();
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    if (count > 1) {
        QVERIFY(owner.edit_quiz_input({ 2 }, 9));
        QVERIFY(
            owner.check_quiz({ 3 })
        ); // retain pending/completed/failed peers
        QCOMPARE(
            owner.deal_step().status, gameplay::dealing_step_status::advanced
        );
    }
    QVERIFY(owner.pause());
    view.refresh_gameplay_session();
    const auto widgets = view.slot_widgets;
    std::vector<gameplay::deck_state> records;
    std::vector<QRect> original;
    for (std::size_t index = 0; index < owner.size(); ++index) {
        records.push_back(*owner.deck({ index }));
        original.push_back(widgets[index]->geometry());
    }
    const auto statistics = owner.statistics();
    const auto timing = owner.timing();
    const auto image_demand
        = std::pair(view.desired_card_source, view.desired_card_size);
    auto* cache = table::shared_image_service();
    QSignalSpy layouts(&view, &table::gameplay_layout_changed);
    QSignalSpy runtime(&view, &table::gameplay_runtime_updated);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    view.resize(800, 1100);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    QCOMPARE(view.scene_motion->progress(), 0.0);
    QCOMPARE(layouts.count(), 1);
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    for (const int time : { 0, 50, 100 }) {
        view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(
            time
        );
        QCOMPARE(
            view.scene_motion->progress(),
            view.scene_motion->findChild<QVariantAnimation*>()
                ->easingCurve()
                .valueForProgress(static_cast<double>(time) / 200.0)
        );
        for (std::size_t index = 0; index < owner.size(); ++index) {
            const auto* motion = view.gameplay_layout->deck_motion({ index });
            QCOMPARE(
                widgets[index]->geometry(),
                widget_rectangle(*motion->sample(view.scene_motion->progress()))
            );
            if (time == 0)
                QCOMPARE(widgets[index]->geometry(), original[index]);
            QCOMPARE(*owner.deck({ index }), records[index]);
        }
    }
    // A same-size/theme notification cannot restart the active path.
    const int halfway_time
        = view.scene_motion->findChild<QVariantAnimation*>()->currentTime();
    const auto halfway_progress = view.scene_motion->progress();
    view.update_layout();
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->currentTime(),
        halfway_time
    );
    QCOMPARE(view.scene_motion->progress(), halfway_progress);
    QCOMPARE(layouts.count(), 1);
    std::vector<QRect> displayed;
    for (auto* widget : widgets)
        displayed.push_back(widget->geometry());
    view.resize(
        220, 180
    ); // interrupt from displayed boxes, not old destinations
    QCOMPARE(view.scene_motion->progress(), 0.0);
    for (std::size_t index = 0; index < owner.size(); ++index)
        QCOMPARE(widgets[index]->geometry(), displayed[index]);
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(50);
    displayed.clear();
    for (auto* widget : widgets)
        displayed.push_back(widget->geometry());
    view.set_card_orientation(card_orientation_mode::vertical);
    QCOMPARE(view.scene_motion->progress(), 0.0);
    for (std::size_t index = 0; index < owner.size(); ++index) {
        QCOMPARE(widgets[index]->geometry(), displayed[index]);
        QCOMPARE(
            view.scene_motion->sample(static_cast<qsizetype>(index)).rotation,
            90.0
        );
        QCOMPARE(
            view.scene_motion->sample(static_cast<qsizetype>(index))
                .target_rotation,
            0.0
        );
    }
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(100);
    displayed.clear();
    for (auto* widget : widgets)
        displayed.push_back(widget->geometry());
    view.set_card_orientation(card_orientation_mode::horizontal);
    for (std::size_t index = 0; index < owner.size(); ++index) {
        QCOMPARE(widgets[index]->geometry(), displayed[index]);
        QCOMPARE(
            view.scene_motion->sample(static_cast<qsizetype>(index)).rotation,
            45.0
        );
        QCOMPARE(
            view.scene_motion->sample(static_cast<qsizetype>(index))
                .target_rotation,
            90.0
        );
    }
    QCOMPARE(table::shared_image_service(), cache);
    QCOMPARE(
        std::pair(view.desired_card_source, view.desired_card_size),
        image_demand
    ); // no frame generations
    QCOMPARE(runtime.count(), 0);
    QCOMPARE(preparation.count(), 0);
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(
        200
    ); // actual finished signal
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    QCOMPARE(view.scene_motion->progress(), 1.0);
    const auto packed = view.pack_gameplay_slots(owner.size());
    QVERIFY(packed);
    QVERIFY(
        std::ranges::equal(
            owner.traversal(), packed->traversal, {},
            [](auto id) { return id.value; }
        )
    );
    for (std::size_t index = 0; index < owner.size(); ++index) {
        QCOMPARE(
            widgets[index]->geometry(),
            widget_rectangle(
                packed->rectangles[owner.slot_for({ index })->value]
            )
        );
        QCOMPARE(
            widgets[index]->gameplay_deck_id(),
            std::optional { gameplay::deck_id { index } }
        );
        QCOMPARE(*owner.deck({ index }), records[index]);
    }
    QCOMPARE(view.slot_widgets, widgets);
    QCOMPARE(owner.statistics(), statistics);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(owner.phase(), gameplay::session_phase::paused);
    QVERIFY(!view.gameplay_timer.isActive());
}

void table_tests::gameplay_resize_motion_policy_and_lifetime() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned resize timeline");
#endif
    table view;
    set_gameplay_motion_style(view, 0);
    view.resize(1100, 800);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    view.resize(700, 1000);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    QCOMPARE(view.scene_motion->progress(), 1.0);
    QCOMPARE(
        view.slot_widgets.front()->geometry(),
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->to)
    );
    set_gameplay_motion_style(view, 2000);
    view.resize(1200, 900);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->duration(), 250
    ); // bounded native hint
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    QVERIFY(!view.install_gameplay_session(owned_table_fixture(65)));
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    const auto destination = view.gameplay_layout->deck_motion({ 0 })->to;
    view.resize(
        1, 88
    ); // Invalid/subpixel final packing leaves valid paths intact.
    QCOMPARE(
        view.gameplay_layout->deck_motion({ 0 })->to.width, destination.width
    );
    QCOMPARE(
        view.gameplay_layout->deck_motion({ 0 })->to.height, destination.height
    );
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    auto& owner = *view.active_gameplay_session();
    QVERIFY(owner.start());
    QVERIFY(owner.pause());
    view.refresh_gameplay_session();
    QVERIFY(
        view.swap_gameplay_decks({ 0 }, { 2 })
    ); // content retargets from the active resize; frames keep slot paths
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    QVERIFY(view.scene_motion->moving_content());
    QCOMPARE(
        view.slot_widgets[0]->geometry(),
        widget_rectangle(
            view.gameplay_layout->slot_motion(*owner.slot_for({ 0 }))->from
        )
    );
    view.resize(900, 1100);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    view.hide();
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    QCOMPARE(view.scene_motion->progress(), 1.0);
    view.resize(800, 1100);
    view.update_layout(); // hidden resize delivery is deferred by QWidget
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    view.show();
    view.resize(1400, 900);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    set_gameplay_motion_style(view, 0); // style disables in-flight animation
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    QCOMPARE(view.scene_motion->progress(), 1.0);
    set_gameplay_motion_style(view, 200);
    view.resize(900, 1200);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    QPointer<table_slot> old = view.slot_widgets.front();
    QVERIFY(view.install_gameplay_session(owned_table_fixture(2)));
    QVERIFY(old.isNull());
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    view.resize(1200, 900);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    old = view.slot_widgets.front();
    view.clear_gameplay_session();
    QVERIFY(old.isNull());
    QVERIFY(view.scene_motion->count() == 0);
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Stopped
    );
    QCoreApplication::processEvents();
    auto retiring = std::make_unique<table>();
    set_gameplay_motion_style(*retiring, 200);
    retiring->resize(1200, 900);
    retiring->show();
    QVERIFY(retiring->install_gameplay_session(owned_table_fixture()));
    retiring->resize(900, 1200);
    QCOMPARE(
        retiring->scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    QPointer<table_slot> retiring_slot = retiring->slot_widgets.front();
    retiring.reset();
    QVERIFY(retiring_slot.isNull());
    QCoreApplication::processEvents();
}

void table_tests::resize_preserves_owned_clock_and_terminal_result() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned resize timeline");
#endif
    table view;
    set_gameplay_motion_style(view, 200);
    view.resize(1400, 900);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        gameplay::quiz_scope::multi, { 1, 3 }
    )));
    QVERIFY(view.start_gameplay_runtime(108));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    const auto prompt = view.gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt);
    QVERIFY(view.edit_gameplay_quiz(*prompt, 9));
    auto* input = view.slot_widgets[2]->findChild<QLineEdit*>(
        QStringLiteral("gameplay_quiz_input")
    );
    QVERIFY(input);
    input->setSelection(0, 1);
    QVERIFY(view.pause_gameplay_runtime());
    const auto record = *view.gameplay_owner->deck({ 2 });
    const auto batch = view.gameplay_owner->current_quiz_batch();
    const auto timing = view.gameplay_owner->timing();
    const auto statistics = view.gameplay_owner->statistics();
    // Layout motion is inert while Running too, not merely in manual Pause.
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    view.resize(900, 1200);
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(20);
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::running);
    QCOMPARE(view.gameplay_owner->current_quiz_batch(), batch);
    QCOMPARE(view.gameplay_owner->timing(), timing);
    QCOMPARE(view.gameplay_owner->statistics(), statistics);
    QCOMPARE(*view.gameplay_owner->deck({ 2 }), record);
    QVERIFY(view.pause_gameplay_runtime());
    for (const int time : { 40, 100, 160 }) {
        view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(
            time
        );
        QCOMPARE(*view.gameplay_owner->deck({ 2 }), record);
        QCOMPARE(view.gameplay_owner->current_quiz_batch(), batch);
        QCOMPARE(view.gameplay_owner->timing(), timing);
        QCOMPARE(view.gameplay_owner->statistics(), statistics);
        QCOMPARE(input->text(), QStringLiteral("9"));
        QCOMPARE(input->selectedText(), QStringLiteral("9"));
        QVERIFY(!view.gameplay_timer.isActive());
        QVERIFY(!input->isEnabled());
        QCOMPARE(
            owned_card(view.slot_widgets[2])->accessibleDescription(),
            str_label("Card hidden")
        );
    }
    QVERIFY(view.finish_gameplay_runtime());
    const auto result = view.gameplay_owner->result();
    QVERIFY(result);
    const std::vector<gameplay::physical_slot_id> order(
        view.gameplay_owner->traversal().begin(),
        view.gameplay_owner->traversal().end()
    );
    view.resize(
        1200, 700
    ); // finished geometry still animates, frozen traversal does not
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(200);
    QCOMPARE(view.gameplay_owner->result(), result);
    QCOMPARE(*view.gameplay_owner->deck({ 2 }), record);
    QVERIFY(std::ranges::equal(view.gameplay_owner->traversal(), order));
    QVERIFY(!view.swap_gameplay_decks({ 0 }, { 1 }));
    QVERIFY(!view.gameplay_timer.isActive());
}

void table_tests::gameplay_swap_content_paths_data() {
    QTest::addColumn<int>("count");
    for (const int count : { 2, 4, 24, 64 })
        QTest::newRow(qPrintable(QStringLiteral("decks-%1").arg(count)))
            << count;
}

void table_tests::gameplay_swap_content_paths() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned Swap timeline");
#endif
    QFETCH(int, count);
    table view;
    set_gameplay_motion_style(view, 200);
    view.resize(1400, 900);
    view.show();
    QVERIFY(view.install_gameplay_session(
        owned_table_fixture(static_cast<std::size_t>(count))
    ));
    QCoreApplication::processEvents();
    auto& owner = *view.gameplay_owner;
    QVERIFY(owner.start());
    QCOMPARE(owner.deal_step().status, gameplay::dealing_step_status::advanced);
    if (count > 2) {
        QVERIFY(owner.edit_quiz_input({ 2 }, 9));
        QVERIFY(owner.check_quiz({ 3 }));
        QCOMPARE(
            owner.deal_step().status, gameplay::dealing_step_status::advanced
        );
        QCOMPARE(owner.deck({ 1 })->status, gameplay::deck_status::completed);
        QCOMPARE(owner.deck({ 3 })->status, gameplay::deck_status::failed);
    }
    QVERIFY(owner.pause());
    view.refresh_gameplay_session();
    const auto widgets = view.slot_widgets;
    std::vector<gameplay::deck_state> records;
    std::vector<QRect> physical;
    for (std::size_t index = 0; index < owner.size(); ++index) {
        records.push_back(*owner.deck({ index }));
        physical.push_back(
            widget_rectangle(view.gameplay_layout->slot_motion({ index })->to)
        );
    }
    const std::vector<gameplay::physical_slot_id> order(
        owner.traversal().begin(), owner.traversal().end()
    );
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    auto* cache = table::shared_image_service();
    const auto image_demand
        = std::pair(view.desired_card_source, view.desired_card_size);
    QSignalSpy layouts(&view, &table::gameplay_layout_changed);
    QSignalSpy runtime(&view, &table::gameplay_runtime_updated);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    const gameplay::deck_id peer { static_cast<std::size_t>(count - 1) };
    QVERIFY(view.swap_gameplay_decks({ 0 }, peer));
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->state(),
        QAbstractAnimation::Running
    );
    QVERIFY(view.scene_motion);
    QVERIFY(view.scene_motion->moving_content());
    QCOMPARE(layouts.count(), 1);
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    for (const int time : { 0, 50, 100, 150 }) {
        view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(
            time
        );
        for (std::size_t index = 0; index < owner.size(); ++index) {
            QCOMPARE(
                widgets[index]->geometry(),
                physical[owner.slot_for({ index })->value]
            );
            QCOMPARE(*owner.deck({ index }), records[index]);
            QCOMPARE(
                widgets[index]->gameplay_deck_id(),
                std::optional { gameplay::deck_id { index } }
            );
        }
        QVERIFY(std::ranges::equal(owner.traversal(), order));
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(owner.statistics(), statistics);
    }
    // Another Swap starts from displayed content, not old endpoints, and
    // changes only the committed mapping. Rejected actions keep this path.
    auto* layer = view.scene_motion;
    const auto displayed = *view.gameplay_layout->deck_motion({ 0 })->sample(
        view.scene_motion->progress()
    );
    QVERIFY(!view.swap_gameplay_decks({ 0 }, { owner.size() }));
    QCOMPARE(layouts.count(), 1);
    QVERIFY(view.swap_gameplay_decks({ 0 }, peer));
    QCOMPARE(view.scene_motion->progress(), 0.0);
    QCOMPARE(
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->from),
        widget_rectangle(displayed)
    );
    QCOMPARE(view.scene_motion, layer);
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(100);
    // A material change repaints the borrowed scene without replacing widgets,
    // moving native controls, restarting motion or mutating the session.
    const auto original_presentation = shared_ui::qt::presentation_for(view);
    auto felt = original_presentation;
    felt.value = shared_ui::qt::material::felt;
    const auto progress = view.scene_motion->progress();
    std::vector<QRect> native_boxes;
    for (auto* widget : widgets)
        native_boxes.push_back(widget->geometry());
    shared_ui::qt::apply_presentation(view, felt);
    view.refresh_material();
    QVERIFY(view.scene_motion->moving_content());
    QCOMPARE(view.scene_motion->progress(), progress);
    QVERIFY(!layer->grab().isNull());
    shared_ui::qt::apply_presentation(view, original_presentation);
    view.refresh_material();
    QCOMPARE(view.scene_motion->progress(), progress);
    for (std::size_t index = 0; index < widgets.size(); ++index) {
        QCOMPARE(widgets[index]->geometry(), native_boxes[index]);
        QCOMPARE(*owner.deck({ index }), records[index]);
    }
    const auto content = *view.gameplay_layout->deck_motion({ 0 })->sample(
        view.scene_motion->progress()
    );
    const auto frame = widgets[0]->geometry();
    const auto angle = view.scene_motion->sample(0).rotation;
    view.set_card_orientation(card_orientation_mode::horizontal);
    view.resize(900, 1200); // Repack during Swap keeps the two path families.
    QCOMPARE(view.scene_motion->progress(), 0.0);
    QCOMPARE(
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->from),
        widget_rectangle(content)
    );
    QCOMPARE(widgets[0]->geometry(), frame);
    QCOMPARE(
        view.scene_motion->sample(static_cast<qsizetype>(0)).rotation, angle
    );
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(100);
    const auto halfway_angle = view.scene_motion->sample(0).rotation;
    const auto halfway_content
        = *view.gameplay_layout->deck_motion({ 0 })->sample(
            view.scene_motion->progress()
        );
    const auto halfway_frame
        = *view.gameplay_layout->slot_motion(*owner.slot_for(peer))
               ->sample(view.scene_motion->progress());
    QVERIFY(
        view.swap_gameplay_decks({ 0 }, peer)
    ); // Swap during rotated repack.
    QCOMPARE(
        view.scene_motion->sample(static_cast<qsizetype>(0)).rotation,
        halfway_angle
    );
    QCOMPARE(
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->from),
        widget_rectangle(halfway_content)
    );
    QCOMPARE(widgets[0]->geometry(), widget_rectangle(halfway_frame));
    QCOMPARE(table::shared_image_service(), cache);
    QCOMPARE(
        std::pair(view.desired_card_source, view.desired_card_size),
        image_demand
    );
    QCOMPARE(runtime.count(), 0);
    QCOMPARE(preparation.count(), 0);
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(200);
    QCOMPARE(view.scene_motion->progress(), 1.0);
    QVERIFY(!view.scene_motion->moving_content());
    QVERIFY(layer->isHidden());
    const auto packed = view.pack_gameplay_slots(owner.size());
    QVERIFY(packed);
    for (std::size_t index = 0; index < owner.size(); ++index) {
        QCOMPARE(*owner.deck({ index }), records[index]);
        QCOMPARE(
            widgets[index]->geometry(),
            widget_rectangle(
                packed->rectangles[owner.slot_for({ index })->value]
            )
        );
    }
    QCOMPARE(view.slot_widgets, widgets);
    QCOMPARE(owner.timing(), timing);
    QCOMPARE(owner.statistics(), statistics);
    QVERIFY(!view.gameplay_timer.isActive());
}

void table_tests::gameplay_swap_pending_state_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("physical-joker-single") << 0;
    QTest::newRow("virtual-single") << 1;
    QTest::newRow("virtual-multi") << 2;
}

void table_tests::gameplay_swap_pending_state() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned Swap timeline");
#endif
    QFETCH(int, kind);
    table view;
    set_gameplay_motion_style(view, 200);
    view.resize(1800, 1200);
    view.show();
    auto prepared = owned_clock_fixture(
        4, true, gameplay::dealing_mode::simultaneous,
        kind == 2 ? gameplay::quiz_scope::multi : gameplay::quiz_scope::single,
        { 1, 4 }
    );
    if (kind == 0) {
        auto settings = prepared->configuration();
        settings.source = gameplay::quiz_source::physical_joker;
        QVERIFY(prepared->configure(settings));
        std::vector<gameplay::prepared_deck> streams(4);
        for (auto& stream : streams) {
            stream.cards = { 0, 52, 1, 2, 3, 4 };
            stream.rank_weights.fill(1);
            stream.initial_running_count = 100;
        }
        QVERIFY(prepared->prepare_decks(streams));
    }
    QVERIFY(view.install_gameplay_session(std::move(prepared)));
    QVERIFY(view.start_gameplay_runtime(212));
    view.stop_gameplay_clock();
    std::vector<gameplay::quiz_answer> answers;
    QVERIFY(view.advance_gameplay_runtime(300, answers));
    view.publish_gameplay_runtime(answers);
    if (kind == 0) {
        // The real clock coalesces a stalled tick; reach the second physical
        // card with another tick instead of assuming 600ms replays two deals.
        answers.clear();
        QVERIFY(view.advance_gameplay_runtime(300, answers));
        view.publish_gameplay_runtime(answers);
    }
    const auto prompt = view.gameplay_quiz_prompt({ 2 });
    QVERIFY(prompt);
    QVERIFY(view.edit_gameplay_quiz(*prompt, 9));
    auto* input = owned_input(view.slot_widgets[2]);
    QVERIFY(input);
    input->setSelection(0, 1);
    QVERIFY(view.pause_gameplay_runtime());
    auto& owner = *view.gameplay_owner;
    std::vector<gameplay::deck_state> records;
    for (std::size_t index = 0; index < owner.size(); ++index)
        records.push_back(*owner.deck({ index }));
    const auto batch = owner.current_quiz_batch();
    const auto timing = owner.timing();
    const auto statistics = owner.statistics();
    const auto revision = view.gameplay_scene_revision;
    QSignalSpy resolved(&view, &table::gameplay_quiz_resolved);
    QVERIFY(view.swap_gameplay_decks({ 2 }, { 0 }));
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    for (const int time : { 0, 60, 100, 180 }) {
        view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(
            time
        );
        if (time == 60) {
            view.set_card_orientation(card_orientation_mode::horizontal);
            view.scene_motion->findChild<QVariantAnimation*>()->pause();
            QCOMPARE(view.scene_motion->progress(), 0.0);
            QCOMPARE(
                view.scene_motion->sample(static_cast<qsizetype>(2))
                    .target_rotation,
                90.0
            );
        }
        QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        view.scene_motion->render(&image); // Real pending paint path.
        for (std::size_t index = 0; index < owner.size(); ++index)
            QCOMPARE(*owner.deck({ index }), records[index]);
        QCOMPARE(owner.current_quiz_batch(), batch);
        QCOMPARE(owner.timing(), timing);
        QCOMPARE(owner.statistics(), statistics);
        QCOMPARE(view.gameplay_scene_revision, revision);
        QCOMPARE(view.gameplay_quiz_prompt({ 2 }), prompt);
        QCOMPARE(owned_input(view.slot_widgets[2]), input);
        QCOMPARE(input->text(), QStringLiteral("9"));
        QCOMPARE(input->selectedText(), QStringLiteral("9"));
        QVERIFY(!input->isEnabled());
        QCOMPARE(
            owned_card(view.slot_widgets[2])->accessibleDescription(),
            str_label("Card hidden")
        );
        QVERIFY(!view.gameplay_timer.isActive());
    }
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(200);
    QCOMPARE(resolved.count(), 0);
    QCOMPARE(*owner.deck({ 2 }), records[2]);
    QVERIFY(view.resume_gameplay_runtime());
    view.stop_gameplay_clock();
    QVERIFY(view.edit_gameplay_quiz(
        *prompt, owner.deck({ 2 })->quiz->expected_count
    ));
    QVERIFY(
        view.check_gameplay_quiz(*prompt)
    ); // Original question remains usable.
    QCOMPARE(resolved.count(), 1);
}

void table_tests::gameplay_swap_motion_policy_and_lifetime() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned Swap timeline");
#endif
    table view;
    set_gameplay_motion_style(view, 0);
    view.resize(1200, 900);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_table_fixture()));
    QVERIFY(view.gameplay_owner->start());
    QVERIFY(view.gameplay_owner->pause());
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(view.scene_motion->isHidden()); // No borrowed painting when inert.
    QVERIFY(!view.scene_motion->moving_content());
    set_gameplay_motion_style(view, 200);
    view.hide();
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(view.scene_motion->isHidden());
    view.show();
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(view.scene_motion->moving_content());
    QPointer<BaseWidget> layer = view.scene_motion;
    view.hide();
    QVERIFY(layer->isHidden());
    QVERIFY(!view.scene_motion->moving_content());
    QCOMPARE(view.scene_motion->progress(), 1.0);
    view.show();
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QCOMPARE(view.scene_motion, layer.data());
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(180);
    set_gameplay_motion_style(view, 50);
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QCOMPARE(
        view.scene_motion->findChild<QVariantAnimation*>()->duration(), 50
    );
    QCOMPARE(view.scene_motion->progress(), 0.0);
    QVERIFY(
        view.scene_motion->moving_content()
    ); // Shorter nonzero hint cannot retire new motion.
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(50);
    QVERIFY(!view.scene_motion->moving_content());
    set_gameplay_motion_style(view, 200);
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    set_gameplay_motion_style(view, 0);
    QVERIFY(layer->isHidden());
    QVERIFY(!view.scene_motion->moving_content());
    set_gameplay_motion_style(view, 200);
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(!view.install_gameplay_session(owned_table_fixture(65)));
    QVERIFY(view.scene_motion->moving_content());
    QVERIFY(view.install_gameplay_session(owned_table_fixture(2)));
    QVERIFY(layer && layer->isHidden());
    QVERIFY(view.scene_motion->isHidden());
    QVERIFY(!view.scene_motion->moving_content());
    QVERIFY(view.gameplay_owner->start());
    QVERIFY(view.gameplay_owner->pause());
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 1 }));
    layer = view.scene_motion;
    view.clear_gameplay_session();
    QVERIFY(layer && layer->isHidden());
    QCOMPARE(view.scene_motion->count(), qsizetype(0));
    QVERIFY(!view.scene_motion->moving_content());
    auto retiring = std::make_unique<table>();
    set_gameplay_motion_style(*retiring, 200);
    retiring->resize(1200, 900);
    retiring->show();
    QVERIFY(retiring->install_gameplay_session(owned_table_fixture()));
    QVERIFY(retiring->gameplay_owner->start());
    QVERIFY(retiring->gameplay_owner->pause());
    QVERIFY(retiring->swap_gameplay_decks({ 0 }, { 2 }));
    layer = retiring->scene_motion;
    QPointer<card_widget> card = owned_card(retiring->slot_widgets[0]);
    retiring.reset();
    QVERIFY(layer.isNull());
    QVERIFY(card.isNull());
    QCoreApplication::processEvents();
}

void table_tests::swap_paints_moving_content_below_controls() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop owned Swap timeline");
#endif
    table view;
    set_gameplay_motion_style(view, 200);
    view.resize(1200, 900);
    view.set_card_orientation(card_orientation_mode::vertical);
    view.show();
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    QVERIFY(view.gameplay_owner->start());
    QCOMPARE(
        view.gameplay_owner->deal_step().status,
        gameplay::dealing_step_status::advanced
    );
    QVERIFY(view.gameplay_owner->pause());
    view.refresh_gameplay_session();
    const std::array<QColor, 2> colors { QColor(240, 30, 20),
                                         QColor(20, 30, 240) };
    for (std::size_t index = 0; index < colors.size(); ++index) {
        QHash<QString, QImage> faces;
        for (const auto& id : required_card_ids_with_back()) {
            QImage face;
            face = QImage(24, 36, QImage::Format_ARGB32_Premultiplied);
            face.fill(colors[index]);
            faces.insert(id, face);
        }
        owned_card(view.slot_widgets[index])->set_presented_card_images(faces);
    }
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 1 }));
    auto* layer = view.scene_motion;
    QVERIFY(layer->testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(layer->focusPolicy(), Qt::NoFocus);
    view.scene_motion->findChild<QVariantAnimation*>()->pause();
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(80);
    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    layer->render(&image, QPoint(), QRegion(), QWidget::RenderFlags {});
    QImage composed(view.size(), QImage::Format_ARGB32_Premultiplied);
    composed.fill(Qt::transparent);
    view.render(&composed);
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const auto moving
            = *view.gameplay_layout->deck_motion({ index })->sample(
                view.scene_motion->progress()
            );
        const QPoint center(
            static_cast<int>(moving.x + moving.width / 2.0),
            static_cast<int>(moving.y + moving.height / 2.0)
        );
        QCOMPARE(
            image.pixelColor(center), colors[index]
        ); // Borrowed back really moves.
        QCOMPARE(
            composed.pixelColor(center), colors[index]
        ); // Not erased by transparent native slot children.
        auto* slot = view.slot_widgets[index];
        QCOMPARE(
            slot->geometry(),
            widget_rectangle(
                view.gameplay_layout
                    ->slot_motion(*view.gameplay_owner->slot_for({ index }))
                    ->to
            )
        );
        auto* button = slot->findChild<QAbstractButton*>(
            QStringLiteral("slot_swap_button")
        );
        QVERIFY(button);
        QVERIFY(button->isVisible());
        const auto global = button->mapToGlobal(button->rect().center());
        QCOMPARE(
            view.childAt(view.mapFromGlobal(global)),
            static_cast<QWidget*>(button)
        );
        QTest::mouseClick(
            button, Qt::LeftButton
        ); // Real native control, layer cannot intercept.
        if (index == 0)
            QVERIFY(slot->swap_selected());
    }
    QCOMPARE(
        view.scene_motion->progress(), 0.0
    ); // Buttons reversed the mapping.
    QCOMPARE(view.scene_motion, layer);
    view.scene_motion->findChild<QVariantAnimation*>()->setCurrentTime(200);
    QVERIFY(layer->isHidden());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
