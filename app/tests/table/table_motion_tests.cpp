// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)

#include "table/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/strategy_data.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "settings/training_progress.hpp"
#include "shell/main_window.hpp"
#include "table/gameplay_setup.hpp"
#include "table/gameplay_strategy.hpp"
#include "table/settings_template.hpp"
#include "table/slot_settings.hpp"
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
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
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
    const auto generation = view.next_shared_generation_id;
    auto* cache = view.shared_raster_cache_service();
    QSignalSpy layouts(&view, &table::gameplay_layout_changed);
    QSignalSpy runtime(&view, &table::gameplay_runtime_updated);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    view.resize(800, 1100);
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    QCOMPARE(view.gameplay_layout_progress, 0.0);
    QCOMPARE(layouts.count(), 1);
    view.gameplay_layout_animation.pause();
    for (const int time : { 0, 50, 100 }) {
        view.gameplay_layout_animation.setCurrentTime(time);
        QCOMPARE(
            view.gameplay_layout_progress,
            view.gameplay_layout_animation.easingCurve().valueForProgress(
                static_cast<double>(time) / 200.0
            )
        );
        for (std::size_t index = 0; index < owner.size(); ++index) {
            const auto* motion = view.gameplay_layout->deck_motion({ index });
            QCOMPARE(
                widgets[index]->geometry(),
                widget_rectangle(*motion->sample(view.gameplay_layout_progress))
            );
            if (time == 0)
                QCOMPARE(widgets[index]->geometry(), original[index]);
            QCOMPARE(*owner.deck({ index }), records[index]);
        }
    }
    // A same-size/theme notification cannot restart the active path.
    const int halfway_time = view.gameplay_layout_animation.currentTime();
    const auto halfway_progress = view.gameplay_layout_progress;
    view.update_layout();
    QCOMPARE(view.gameplay_layout_animation.currentTime(), halfway_time);
    QCOMPARE(view.gameplay_layout_progress, halfway_progress);
    QCOMPARE(layouts.count(), 1);
    std::vector<QRect> displayed;
    for (auto* widget : widgets)
        displayed.push_back(widget->geometry());
    view.resize(
        220, 180
    ); // interrupt from displayed boxes, not old destinations
    QCOMPARE(view.gameplay_layout_progress, 0.0);
    for (std::size_t index = 0; index < owner.size(); ++index)
        QCOMPARE(widgets[index]->geometry(), displayed[index]);
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(50);
    displayed.clear();
    for (auto* widget : widgets)
        displayed.push_back(widget->geometry());
    view.set_card_orientation(card_orientation_mode::vertical);
    QCOMPARE(view.gameplay_layout_progress, 0.0);
    for (std::size_t index = 0; index < owner.size(); ++index) {
        QCOMPARE(widgets[index]->geometry(), displayed[index]);
        QCOMPARE(view.gameplay_rotation_paths[index].from, 90.0);
        QCOMPARE(view.gameplay_rotation_paths[index].to, 0.0);
    }
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(100);
    displayed.clear();
    for (auto* widget : widgets)
        displayed.push_back(widget->geometry());
    view.set_card_orientation(card_orientation_mode::horizontal);
    for (std::size_t index = 0; index < owner.size(); ++index) {
        QCOMPARE(widgets[index]->geometry(), displayed[index]);
        QCOMPARE(view.gameplay_rotation_paths[index].from, 45.0);
        QCOMPARE(view.gameplay_rotation_paths[index].to, 90.0);
    }
    QCOMPARE(view.shared_raster_cache_service(), cache);
    QCOMPARE(
        view.next_shared_generation_id, generation
    ); // no frame generations
    QCOMPARE(runtime.count(), 0);
    QCOMPARE(preparation.count(), 0);
    view.gameplay_layout_animation.setCurrentTime(
        200
    ); // actual finished signal
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QCOMPARE(view.gameplay_layout_progress, 1.0);
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
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QCOMPARE(view.gameplay_layout_progress, 1.0);
    QCOMPARE(
        view.slot_widgets.front()->geometry(),
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->to)
    );
    set_gameplay_motion_style(view, 2000);
    view.resize(1200, 900);
    QCOMPARE(
        view.gameplay_layout_animation.duration(), 250
    ); // bounded native hint
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    QVERIFY(!view.install_gameplay_session(owned_table_fixture(65)));
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
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
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    auto& owner = *view.active_gameplay_session();
    QVERIFY(owner.start());
    QVERIFY(owner.pause());
    view.refresh_gameplay_session();
    QVERIFY(
        view.swap_gameplay_decks({ 0 }, { 2 })
    ); // content retargets from the active resize; frames keep slot paths
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    QVERIFY(view.gameplay_content_motion);
    QCOMPARE(
        view.slot_widgets[0]->geometry(),
        widget_rectangle(
            view.gameplay_layout->slot_motion(*owner.slot_for({ 0 }))->from
        )
    );
    view.resize(900, 1100);
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    view.hide();
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QCOMPARE(view.gameplay_layout_progress, 1.0);
    view.resize(800, 1100);
    view.update_layout(); // hidden resize delivery is deferred by QWidget
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    view.show();
    view.resize(1400, 900);
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    set_gameplay_motion_style(view, 0); // style disables in-flight animation
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QCOMPARE(view.gameplay_layout_progress, 1.0);
    set_gameplay_motion_style(view, 200);
    view.resize(900, 1200);
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    QPointer<table_slot> old = view.slot_widgets.front();
    QVERIFY(view.install_gameplay_session(owned_table_fixture(2)));
    QVERIFY(old.isNull());
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    view.resize(1200, 900);
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    old = view.slot_widgets.front();
    view.clear_gameplay_session();
    QVERIFY(old.isNull());
    QVERIFY(view.gameplay_rotation_paths.empty());
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QCoreApplication::processEvents();
    view.set_slot_count(2);
    view.start_quiz(0, false);
    view.resize(900, 1200);
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Stopped
    );
    QVERIFY(
        view.capture_session_state().quiz_running
    ); // legacy stays immediate
    view.clear_quiz();
    auto retiring = std::make_unique<table>();
    set_gameplay_motion_style(*retiring, 200);
    retiring->resize(1200, 900);
    retiring->show();
    QVERIFY(retiring->install_gameplay_session(owned_table_fixture()));
    retiring->resize(900, 1200);
    QCOMPARE(
        retiring->gameplay_layout_animation.state(), QAbstractAnimation::Running
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
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(20);
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::running);
    QCOMPARE(view.gameplay_owner->current_quiz_batch(), batch);
    QCOMPARE(view.gameplay_owner->timing(), timing);
    QCOMPARE(view.gameplay_owner->statistics(), statistics);
    QCOMPARE(*view.gameplay_owner->deck({ 2 }), record);
    QVERIFY(view.pause_gameplay_runtime());
    for (const int time : { 40, 100, 160 }) {
        view.gameplay_layout_animation.setCurrentTime(time);
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
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    view.gameplay_layout_animation.setCurrentTime(200);
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
    auto* cache = view.shared_raster_cache_service();
    const auto generation = view.next_shared_generation_id;
    QSignalSpy layouts(&view, &table::gameplay_layout_changed);
    QSignalSpy runtime(&view, &table::gameplay_runtime_updated);
    QSignalSpy preparation(&view, &table::gameplay_preparation_required);
    const gameplay::deck_id peer { static_cast<std::size_t>(count - 1) };
    QVERIFY(view.swap_gameplay_decks({ 0 }, peer));
    QCOMPARE(
        view.gameplay_layout_animation.state(), QAbstractAnimation::Running
    );
    QVERIFY(view.gameplay_motion_layer);
    QVERIFY(view.gameplay_content_motion);
    QCOMPARE(layouts.count(), 1);
    view.gameplay_layout_animation.pause();
    for (const int time : { 0, 50, 100, 150 }) {
        view.gameplay_layout_animation.setCurrentTime(time);
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
    auto* layer = view.gameplay_motion_layer;
    const auto displayed = *view.gameplay_layout->deck_motion({ 0 })->sample(
        view.gameplay_layout_progress
    );
    QVERIFY(!view.swap_gameplay_decks({ 0 }, { owner.size() }));
    QCOMPARE(layouts.count(), 1);
    QVERIFY(view.swap_gameplay_decks({ 0 }, peer));
    QCOMPARE(view.gameplay_layout_progress, 0.0);
    QCOMPARE(
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->from),
        widget_rectangle(displayed)
    );
    QCOMPARE(view.gameplay_motion_layer, layer);
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(100);
    const auto content = *view.gameplay_layout->deck_motion({ 0 })->sample(
        view.gameplay_layout_progress
    );
    const auto frame = widgets[0]->geometry();
    const auto angle = std::lerp(
        view.gameplay_rotation_paths[0].from,
        view.gameplay_rotation_paths[0].to, view.gameplay_layout_progress
    );
    view.set_card_orientation(card_orientation_mode::horizontal);
    view.resize(900, 1200); // Repack during Swap keeps the two path families.
    QCOMPARE(view.gameplay_layout_progress, 0.0);
    QCOMPARE(
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->from),
        widget_rectangle(content)
    );
    QCOMPARE(widgets[0]->geometry(), frame);
    QCOMPARE(view.gameplay_rotation_paths[0].from, angle);
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(100);
    const auto halfway_angle = std::lerp(
        view.gameplay_rotation_paths[0].from,
        view.gameplay_rotation_paths[0].to, view.gameplay_layout_progress
    );
    const auto halfway_content
        = *view.gameplay_layout->deck_motion({ 0 })->sample(
            view.gameplay_layout_progress
        );
    const auto halfway_frame
        = *view.gameplay_layout->slot_motion(*owner.slot_for(peer))
               ->sample(view.gameplay_layout_progress);
    QVERIFY(
        view.swap_gameplay_decks({ 0 }, peer)
    ); // Swap during rotated repack.
    QCOMPARE(view.gameplay_rotation_paths[0].from, halfway_angle);
    QCOMPARE(
        widget_rectangle(view.gameplay_layout->deck_motion({ 0 })->from),
        widget_rectangle(halfway_content)
    );
    QCOMPARE(widgets[0]->geometry(), widget_rectangle(halfway_frame));
    QCOMPARE(view.shared_raster_cache_service(), cache);
    QCOMPARE(view.next_shared_generation_id, generation);
    QCOMPARE(runtime.count(), 0);
    QCOMPARE(preparation.count(), 0);
    view.gameplay_layout_animation.setCurrentTime(200);
    QCOMPARE(view.gameplay_layout_progress, 1.0);
    QVERIFY(!view.gameplay_content_motion);
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
    view.gameplay_layout_animation.pause();
    for (const int time : { 0, 60, 100, 180 }) {
        view.gameplay_layout_animation.setCurrentTime(time);
        if (time == 60) {
            view.set_card_orientation(card_orientation_mode::horizontal);
            view.gameplay_layout_animation.pause();
            QCOMPARE(view.gameplay_layout_progress, 0.0);
            QCOMPARE(view.gameplay_rotation_paths[2].to, 90.0);
        }
        QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        view.gameplay_motion_layer->render(&image); // Real pending paint path.
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
    view.gameplay_layout_animation.setCurrentTime(200);
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
    QVERIFY(
        !view.gameplay_motion_layer
    ); // No paint-layer allocation when inert.
    QVERIFY(!view.gameplay_content_motion);
    set_gameplay_motion_style(view, 200);
    view.hide();
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(!view.gameplay_motion_layer);
    view.show();
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(view.gameplay_content_motion);
    QPointer<BaseWidget> layer = view.gameplay_motion_layer;
    view.hide();
    QVERIFY(layer->isHidden());
    QVERIFY(!view.gameplay_content_motion);
    QCOMPARE(view.gameplay_layout_progress, 1.0);
    view.show();
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QCOMPARE(view.gameplay_motion_layer, layer.data());
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(180);
    set_gameplay_motion_style(view, 50);
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QCOMPARE(view.gameplay_layout_animation.duration(), 50);
    QCOMPARE(view.gameplay_layout_progress, 0.0);
    QVERIFY(
        view.gameplay_content_motion
    ); // Shorter nonzero hint cannot retire new motion.
    view.gameplay_layout_animation.setCurrentTime(50);
    QVERIFY(!view.gameplay_content_motion);
    set_gameplay_motion_style(view, 200);
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    set_gameplay_motion_style(view, 0);
    QVERIFY(layer->isHidden());
    QVERIFY(!view.gameplay_content_motion);
    set_gameplay_motion_style(view, 200);
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 2 }));
    QVERIFY(!view.install_gameplay_session(owned_table_fixture(65)));
    QVERIFY(view.gameplay_content_motion);
    QVERIFY(view.install_gameplay_session(owned_table_fixture(2)));
    QVERIFY(layer.isNull());
    QVERIFY(!view.gameplay_motion_layer);
    QVERIFY(!view.gameplay_content_motion);
    QVERIFY(view.gameplay_owner->start());
    QVERIFY(view.gameplay_owner->pause());
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 1 }));
    layer = view.gameplay_motion_layer;
    view.clear_gameplay_session();
    QVERIFY(layer.isNull());
    QVERIFY(!view.gameplay_content_motion);
    auto retiring = std::make_unique<table>();
    set_gameplay_motion_style(*retiring, 200);
    retiring->resize(1200, 900);
    retiring->show();
    QVERIFY(retiring->install_gameplay_session(owned_table_fixture()));
    QVERIFY(retiring->gameplay_owner->start());
    QVERIFY(retiring->gameplay_owner->pause());
    QVERIFY(retiring->swap_gameplay_decks({ 0 }, { 2 }));
    layer = retiring->gameplay_motion_layer;
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
        QVector<QImage> faces(card_element_ids().size() + 1);
        for (auto& face : faces) {
            face = QImage(24, 36, QImage::Format_ARGB32_Premultiplied);
            face.fill(colors[index]);
        }
        owned_card(view.slot_widgets[index])
            ->set_shared_card_faces(faces, QSize(24, 36));
    }
    QVERIFY(view.swap_gameplay_decks({ 0 }, { 1 }));
    auto* layer = view.gameplay_motion_layer;
    QVERIFY(layer->testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(layer->focusPolicy(), Qt::NoFocus);
    view.gameplay_layout_animation.pause();
    view.gameplay_layout_animation.setCurrentTime(80);
    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    layer->render(&image, QPoint(), QRegion(), QWidget::RenderFlags {});
    QImage composed(view.size(), QImage::Format_ARGB32_Premultiplied);
    composed.fill(Qt::transparent);
    view.render(&composed);
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const auto moving
            = *view.gameplay_layout->deck_motion({ index })->sample(
                view.gameplay_layout_progress
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
        auto* button
            = slot->findChild<QPushButton*>(QStringLiteral("slot_swap_button"));
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
        view.gameplay_layout_progress, 0.0
    ); // Buttons reversed the mapping.
    QCOMPARE(view.gameplay_motion_layer, layer);
    view.gameplay_layout_animation.setCurrentTime(200);
    QVERIFY(layer->isHidden());
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
