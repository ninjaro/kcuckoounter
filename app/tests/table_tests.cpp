#include "include/table_tests.hpp"

#include "card_helpers/card_sheet.hpp"
#include "settings/theme_palette.hpp"
#include "settings/theme_settings.hpp"
#include "table/table.hpp"
#include "table/table_slot.hpp"

#include "arch/str_label.hpp"
#include "table/card_widget.hpp"

#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QtTest/QtTest>

void table_tests::overlay_palette_applies_to_bars() {
    const QColor original_base = theme_settings::base_color();
    const QColor base_color(0x1B, 0x3C, 0xF0);
    theme_settings::set_base_color(base_color);

    table_slot slot;
    slot.apply_theme();

    const theme_palette_option& palette_option = theme_palette_registry::option(
        theme_palette_registry::id_from_color(base_color)
    );
    const QColor expected_panel = palette_option.panel_color();

    auto settings_frame
        = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY2(
        settings_frame != nullptr,
        "settings bar frame should be present for palette updates"
    );
    QVERIFY2(
        swap_frame != nullptr,
        "swap bar frame should be present for palette updates"
    );
    QCOMPARE(settings_frame->palette().color(QPalette::Window), expected_panel);
    QCOMPARE(swap_frame->palette().color(QPalette::Window), expected_panel);

    theme_settings::set_base_color(original_base);
}

void table_tests::overlay_palette_uses_gold_text_on_frames() {
    const QColor original_base = theme_settings::base_color();
    const QColor base_color(0x1B, 0x3C, 0xF0);
    theme_settings::set_base_color(base_color);

    table_slot slot;
    slot.apply_theme();

    const QColor expected_text = theme_settings::slot_border_color();
    auto settings_frame
        = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY(settings_frame != nullptr);
    QVERIFY(swap_frame != nullptr);
    QCOMPARE(
        settings_frame->palette().color(QPalette::WindowText), expected_text
    );
    QCOMPARE(swap_frame->palette().color(QPalette::WindowText), expected_text);

    theme_settings::set_base_color(original_base);
}

void table_tests::overlay_frames_enable_auto_fill() {
    table_slot slot;
    slot.apply_theme();

    auto settings_frame
        = slot.findChild<QFrame*>(QStringLiteral("settings_bar_frame"));
    auto swap_frame = slot.findChild<QFrame*>(QStringLiteral("swap_bar_frame"));
    QVERIFY(settings_frame != nullptr);
    QVERIFY(swap_frame != nullptr);
    QVERIFY(settings_frame->autoFillBackground());
    QVERIFY(swap_frame->autoFillBackground());
}

void table_tests::quiz_hides_skip_when_skipping_disabled() {
    table_slot slot;
    slot.start_quiz(0);
    slot.set_allow_skipping(false);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(skip_button != nullptr);
    QVERIFY(!skip_button->isVisible());
}

void table_tests::quiz_training_mode_does_not_adjust_score() {
    table_slot slot;
    QSignalSpy score_spy(&slot, &table_slot::score_adjusted);

    auto training_check_box
        = slot.findChild<BaseCheckBox*>(QStringLiteral("training_check_box"));
    QVERIFY(training_check_box != nullptr);
    training_check_box->setChecked(true);

    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(score_spy.count(), 0);

    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    QVERIFY(skip_button != nullptr);
    skip_button->click();
    QCOMPARE(score_spy.count(), 0);
}

void table_tests::quiz_wrong_answer_exhausts_deck_without_training() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto answer_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_answer_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    QVERIFY(spin_box != nullptr);
    QVERIFY(answer_button != nullptr);
    QVERIFY(feedback_label != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 1;
    spin_box->setValue(provided);
    answer_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(slot.is_deck_exhausted());
}

void table_tests::quiz_wrong_answer_shows_continue_in_training() {
    table_slot slot;
    auto training_check_box
        = slot.findChild<BaseCheckBox*>(QStringLiteral("training_check_box"));
    QVERIFY(training_check_box != nullptr);
    training_check_box->setChecked(true);

    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto answer_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_answer_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(answer_button != nullptr);
    QVERIFY(feedback_label != nullptr);
    QVERIFY(continue_button != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 2;
    spin_box->setValue(provided);
    answer_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(continue_button->isVisible());
    QVERIFY(!slot.is_deck_exhausted());
}

void table_tests::quiz_skip_shows_continue_feedback() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    auto feedback_label
        = slot.findChild<QLabel*>(QStringLiteral("quiz_feedback_label"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(skip_button != nullptr);
    QVERIFY(feedback_label != nullptr);
    QVERIFY(continue_button != nullptr);

    auto card = slot.findChild<card_widget*>();
    QVERIFY(card != nullptr);
    const int expected = card->current_total_weight();
    const int provided = expected + 3;
    spin_box->setValue(provided);
    skip_button->click();

    const QString expected_message
        = str_label("You've set %1 while the correct answer is %2.")
              .arg(provided)
              .arg(expected);
    QCOMPARE(feedback_label->text(), expected_message);
    QVERIFY(continue_button->isVisible());
}

void table_tests::quiz_spin_box_remembers_last_input() {
    table_slot slot;
    slot.start_quiz(0);
    for (int i = 0; i < 29; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());

    auto spin_box
        = slot.findChild<BaseSpinBox*>(QStringLiteral("quiz_spin_box"));
    auto skip_button
        = slot.findChild<BasePushButton*>(QStringLiteral("quiz_skip_button"));
    auto continue_button = slot.findChild<BasePushButton*>(
        QStringLiteral("quiz_continue_button")
    );
    QVERIFY(spin_box != nullptr);
    QVERIFY(skip_button != nullptr);
    QVERIFY(continue_button != nullptr);

    spin_box->setValue(7);
    skip_button->click();
    continue_button->click();

    for (int i = 0; i < 30; ++i) {
        slot.advance_card();
    }
    QVERIFY(slot.is_quiz_prompt_active());
    QCOMPARE(spin_box->value(), 7);
}

void table_tests::shared_card_faces_presence_tracks_set_and_clear() {
    table_slot slot;
    QVERIFY(!slot.has_shared_card_faces());

    QVector<QImage> shared_faces;
    shared_faces.push_back(QImage(24, 36, QImage::Format_ARGB32_Premultiplied));
    shared_faces[0].fill(Qt::red);

    slot.set_shared_card_faces(shared_faces, QSize(24, 36));
    QVERIFY(slot.has_shared_card_faces());

    slot.clear_shared_card_faces();
    QVERIFY(!slot.has_shared_card_faces());
}

void table_tests::
    shared_cache_rasterization_includes_back_and_fallback_usage() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    const QString missing_theme_source
        = str_label("assets/non_existent_cards.svg");
    set_card_sheet_source_path(missing_theme_source);

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    const int bucket_px = 128;
    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, bucket_px)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 4000);

    const raster_cache::debug_snapshot snapshot
        = table_widget.shared_raster_cache_service()->get_debug_snapshot();
    std::optional<raster_cache::entry_key> key;
    for (const auto& largest : snapshot.largest_entries) {
        if (largest.name_space != raster_cache::cache_namespace::main
            || largest.kind != raster_cache::resource_kind::card_sheet_faces
            || largest.source_id != missing_theme_source
            || largest.target_bucket_px != bucket_px) {
            continue;
        }
        key = raster_cache::entry_key {
            .name_space = largest.name_space,
            .kind = largest.kind,
            .source_id = largest.source_id,
            .render_scope = largest.render_scope,
            .target_bucket_px = largest.target_bucket_px,
        };
        break;
    }
    QVERIFY(key.has_value());
    const std::optional<raster_cache::result> ready
        = table_widget.shared_raster_cache_service()->get_if_ready(*key);
    QVERIFY(ready.has_value());

    const int required_keys
        = static_cast<int>(required_card_element_ids_with_back().size());
    QCOMPARE(ready->face_images.size(), required_keys);
    QCOMPARE(ready->fallback_usage.active_theme_keys, 0);
    QCOMPARE(ready->fallback_usage.default_theme_keys, required_keys);
    QCOMPARE(ready->fallback_usage.placeholder_keys, 0);

    QCOMPARE(snapshot.fallback_default_theme_keys_ready, required_keys);
    QCOMPARE(snapshot.fallback_placeholder_keys_ready, 0);
}

void table_tests::shared_cache_generation_cutover_stays_bounded() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(str_label("assets/cards_1.svg"));
    table_widget.apply_theme();
    set_card_sheet_source_path(str_label("assets/cards_2.svg"));
    table_widget.apply_theme();
    set_card_sheet_source_path(str_label("assets/cards_0.svg"));
    table_widget.apply_theme();

    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 15000);

    const raster_cache::debug_snapshot snapshot
        = table_widget.shared_raster_cache_service()->get_debug_snapshot();
    QVERIFY(snapshot.ready_entries <= 2);
    QVERIFY(snapshot.in_flight_families <= 1);
}

void table_tests::shared_generation_cutover_keeps_single_visible_generation() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(3);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 256)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 10000);

    set_card_sheet_source_path(str_label("assets/cards_1.svg"));
    table_widget.apply_theme();
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 10000);

    const geometry_debug_snapshot geometry
        = table_widget.current_geometry_debug_snapshot();
    QVERIFY(geometry.active_generation_id > 0);
    QCOMPARE(geometry.warming_generation_id, qint64(0));

    const raster_cache::debug_snapshot snapshot
        = table_widget.shared_raster_cache_service()->get_debug_snapshot();
    QVERIFY(snapshot.ready_entries <= 2);
    QVERIFY(snapshot.in_flight_families <= 1);

    const QList<table_slot*> slot_list
        = table_widget.findChildren<table_slot*>();
    int visible_slot_count = 0;
    for (table_slot* slot : slot_list) {
        if (slot == nullptr || !slot->isVisible()) {
            continue;
        }
        ++visible_slot_count;
        QVERIFY(slot->has_shared_card_faces());
    }
    QVERIFY(visible_slot_count > 0);
}

void table_tests::
    theme_apply_while_shared_worker_busy_clears_stale_in_flight() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(900, 700);
    table_widget.set_slot_count(1);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(str_label("assets/cards_1.svg"));
    table_widget.apply_theme();

    QTest::qWait(50);
    QCoreApplication::processEvents();

    const auto snapshot
        = table_widget.shared_raster_cache_service()->get_debug_snapshot();
    QVERIFY(snapshot.in_flight_families <= 1);
}

void table_tests::theme_and_resize_transitions_are_non_blocking() {
    struct source_restore_guard {
        QString source;

        ~source_restore_guard() { set_card_sheet_source_path(source); }
    } guard { card_sheet_source_path() };

    set_card_sheet_source_path(str_label("assets/cards_0.svg"));

    table table_widget;
    table_widget.resize(920, 720);
    table_widget.set_slot_count(4);
    table_widget.show();
    QCoreApplication::processEvents();

    const bool invoked = QMetaObject::invokeMethod(
        &table_widget, "on_shared_rasterization_requested",
        Qt::DirectConnection, Q_ARG(int, 1024)
    );
    QVERIFY(invoked);
    QTRY_VERIFY_WITH_TIMEOUT(table_widget.is_rasterization_busy(), 4000);

    set_card_sheet_source_path(str_label("assets/cards_2.svg"));
    QElapsedTimer theme_timer;
    theme_timer.start();
    table_widget.apply_theme();
    const qint64 theme_apply_elapsed_ms = theme_timer.elapsed();

    QElapsedTimer resize_timer;
    resize_timer.start();
    table_widget.resize(1000, 760);
    QCoreApplication::processEvents();
    const qint64 resize_elapsed_ms = resize_timer.elapsed();

    QVERIFY(theme_apply_elapsed_ms < 350);
    QVERIFY(resize_elapsed_ms < 350);

    QTRY_VERIFY_WITH_TIMEOUT(!table_widget.is_rasterization_busy(), 15000);
    const geometry_debug_snapshot geometry
        = table_widget.current_geometry_debug_snapshot();
    QVERIFY(geometry.active_generation_id > 0);
    QCOMPARE(geometry.warming_generation_id, qint64(0));
}
