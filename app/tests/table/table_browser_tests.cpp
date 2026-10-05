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

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QTextTable>
#include <QTimer>
#include <QtTest/QtTest>

#include <memory>

#ifdef KC_KDE
#include <KActionCollection>
#endif

#include "table/table_fixture.hpp"

using namespace table_test;

void table_tests::strategy_browser_preserves_identity_and_reference_values() {
    const auto& catalog = strategy_repository();
    QVERIFY(catalog.is_valid());
    const auto& strategy = catalog.strategies.at(1);
    std::unique_ptr<QWidget> browser(
        create_strategy_browser(catalog, strategy.slug)
    );
    auto* list = browser->findChild<QListView*>(
        QStringLiteral("strategy_browser_list")
    );
    auto* details = browser->findChild<QTextBrowser*>(
        QStringLiteral("strategy_browser_details")
    );
    auto* comparison = browser->findChild<QTableView*>(
        QStringLiteral("strategy_comparison")
    );
    auto* search
        = browser->findChild<QLineEdit*>(QStringLiteral("strategy_search"));
    auto* tabs = browser->findChild<QTabWidget*>(
        QStringLiteral("strategy_browser_views")
    );
    QVERIFY(list && details && comparison && search && tabs);
    QCOMPARE(list->model()->rowCount(), catalog.strategies.size());
    QCOMPARE(list->currentIndex().data().toString(), strategy.name);
    QVERIFY(details->toPlainText().contains(strategy.description));
    QCOMPARE(comparison->selectionModel(), list->selectionModel());

    auto* weights = qobject_cast<QTextTable*>(
        details->document()->rootFrame()->childFrames().first()
    );
    QVERIFY(weights);
    QCOMPARE(weights->rows(), 2);
    QCOMPARE(weights->columns(), 13);
    for (int rank = 0; rank < 13; ++rank) {
        auto cursor = weights->cellAt(1, rank).firstCursorPosition();
        cursor.setPosition(
            weights->cellAt(1, rank).lastCursorPosition().position(),
            QTextCursor::KeepAnchor
        );
        QCOMPARE(cursor.selectedText().toInt(), strategy.weights[rank]);
        if (strategy.weights[rank] > 0)
            QVERIFY(cursor.selectedText().startsWith(QLatin1Char('+')));
    }
    for (const auto order : { Qt::AscendingOrder, Qt::DescendingOrder }) {
        for (int column = 0; column < comparison->model()->columnCount();
             ++column) {
            comparison->sortByColumn(column, order);
            QCOMPARE(
                list->currentIndex().siblingAtColumn(0).data().toString(),
                strategy.name
            );
            QVERIFY(details->toPlainText().contains(strategy.description));
        }
    }
    search->setText(strategy.slug.toUpper());
    QCOMPARE(list->model()->rowCount(), 1);
    QCOMPARE(list->currentIndex().data().toString(), strategy.name);
    search->clear();
    QCOMPARE(list->currentIndex().data().toString(), strategy.name);
    browser->resize(820, 560);
    browser->show();
    for (const int width : { 820, 360 }) {
        browser->resize(width, 480);
        for (int tab = 0; tab < tabs->count(); ++tab) {
            tabs->setCurrentIndex(tab);
            QCoreApplication::processEvents();
            QCOMPARE(browser->size(), QSize(width, 480));
            QVERIFY(browser->rect().contains(
                QRect(search->mapTo(browser.get(), QPoint()), search->size())
            ));
            QVERIFY(browser->rect().contains(
                QRect(tabs->mapTo(browser.get(), QPoint()), tabs->size())
            ));
            if (tab == 0) {
                auto* rank_table = qobject_cast<QTextTable*>(
                    details->document()->rootFrame()->childFrames().first()
                );
                QVERIFY(rank_table);
                for (int row = 0; row < 2; ++row)
                    for (int rank = 0; rank < 13; ++rank)
                        QCOMPARE(
                            rank_table->cellAt(row, rank)
                                .firstCursorPosition()
                                .block()
                                .layout()
                                ->lineCount(),
                            1
                        );
            }
        }
    }
    search->setFocus();
    QTest::keyClicks(search, "no-match-for-this-query");
    QCOMPARE(list->model()->rowCount(), 0);
    QVERIFY(!list->currentIndex().isValid());
    QVERIFY(!details->toPlainText().contains(strategy.description));
    QVERIFY(browser
                ->findChild<QLabel*>(QStringLiteral("strategy_search_status"))
                ->text()
                .contains(str_label("No matching strategies.")));
    search->clear();
    QCOMPARE(list->model()->rowCount(), catalog.strategies.size());
    tabs->setCurrentIndex(1);
    comparison->setFocus();
    QTest::keyClick(comparison, Qt::Key_Down);
    const auto selected
        = comparison->currentIndex().siblingAtColumn(0).data().toString();
    QTest::keyClick(comparison, Qt::Key_Return);
    QCOMPARE(tabs->currentIndex(), 0);
    QVERIFY(details->toPlainText().contains(selected));
}

void table_tests::strategy_browser_handles_missing_and_filtered_metadata() {
    strategy_catalog catalog;
    for (int i = 0; i < 3; ++i) {
        strategy_data strategy;
        strategy.id = i + 1;
        strategy.slug = QString::number(i);
        strategy.name = QStringLiteral("Same name");
        strategy.description
            = QStringLiteral("<b>Literal description %1</b>").arg(i);
        strategy.weights = QVector<int>(13, i - 1);
        if (i < 2)
            strategy.metrics.insert(
                QStringLiteral("betting_correlation"), i == 0 ? 2.0 : 10.0
            );
        catalog.strategies.append(strategy);
    }
    // Deliberately outside normal correlation values: sorting is numeric, not
    // lexicographic, and the UI does not clamp values or invent a meter scale.
    std::unique_ptr<QWidget> browser(
        create_strategy_browser(catalog, QStringLiteral("1"))
    );
    auto* comparison = browser->findChild<QTableView*>(
        QStringLiteral("strategy_comparison")
    );
    auto* details = browser->findChild<QTextBrowser*>(
        QStringLiteral("strategy_browser_details")
    );
    auto* search
        = browser->findChild<QLineEdit*>(QStringLiteral("strategy_search"));
    const auto description = catalog.strategies[1].description;
    QVERIFY(details->toPlainText().contains(description)); // literal markup
    QVERIFY(details->toPlainText().contains(str_label("Not provided")));
    comparison->sortByColumn(1, Qt::AscendingOrder);
    QCOMPARE(comparison->model()->index(0, 1).data().toDouble(), 2.0);
    QCOMPARE(comparison->model()->index(1, 1).data().toDouble(), 10.0);
    QCOMPARE(
        comparison->model()->index(2, 1).data().toString(),
        str_label("Not provided")
    );
    QVERIFY(details->toPlainText().contains(description));
    comparison->sortByColumn(1, Qt::DescendingOrder);
    QCOMPARE(comparison->model()->index(0, 1).data().toDouble(), 10.0);
    QCOMPARE(comparison->model()->index(1, 1).data().toDouble(), 2.0);
    QCOMPARE(
        comparison->model()->index(2, 1).data().toString(),
        str_label("Not provided")
    );
    QVERIFY(details->toPlainText().contains(description));
    auto* field = browser->findChild<BaseComboBox*>(
        QStringLiteral("strategy_sort_field")
    );
    auto* order = browser->findChild<BaseComboBox*>(
        QStringLiteral("strategy_sort_order")
    );
    QVERIFY(field && order);
    QCOMPARE(field->currentIndex(), 1);
    QCOMPARE(order->currentIndex(), 1);
    browser->show();
    browser->findChild<QTabWidget*>(QStringLiteral("strategy_browser_views"))
        ->setCurrentIndex(1);
    order->setFocus();
    QTest::keyClick(order, Qt::Key_Home);
    QCOMPARE(comparison->model()->index(0, 1).data().toDouble(), 2.0);
    QTest::keyClick(order, Qt::Key_End);
    QCOMPARE(comparison->model()->index(0, 1).data().toDouble(), 10.0);
    QCOMPARE(
        comparison->model()->index(2, 1).data().toString(),
        str_label("Not provided")
    );
    // Filtering out the selected entry clears its details, rather than
    // displaying a different strategy under a stale name/row index.
    search->setText(QStringLiteral("description 0"));
    QCOMPARE(comparison->model()->rowCount(), 1);
    QVERIFY(!comparison->currentIndex().isValid());
    QVERIFY(!details->toPlainText().contains(description));
    comparison->setCurrentIndex(comparison->model()->index(0, 0));
    QVERIFY(details->toPlainText().contains(catalog.strategies[0].description));

    for (const bool invalid : { false, true }) {
        strategy_catalog unavailable;
        if (invalid) {
            unavailable = catalog;
            unavailable.diagnostics.append(
                QStringLiteral("Invalid catalogue fixture")
            );
        }
        std::unique_ptr<QWidget> empty(
            create_strategy_browser(unavailable, {})
        );
        QCOMPARE(
            empty->findChild<QTableView*>(QStringLiteral("strategy_comparison"))
                ->model()
                ->rowCount(),
            0
        );
        QVERIFY(
            empty->findChild<QLabel*>(QStringLiteral("strategy_search_status"))
                ->text()
                .contains(str_label("Strategies unavailable"))
        );
    }
    std::unique_ptr<QWidget> unknown(
        create_strategy_browser(catalog, QStringLiteral("removed-slug"))
    );
    QVERIFY(
        !unknown->findChild<QListView*>(QStringLiteral("strategy_browser_list"))
             ->currentIndex()
             .isValid()
    );
}

void table_tests::strategy_browser_routes_do_not_change_gameplay() {
#if defined(KC_ANDROID) || defined(Q_OS_ANDROID)
    QSKIP("Desktop strategy browser");
#endif
    const auto& catalog = strategy_repository();
    QVERIFY(catalog.is_valid());
    const auto preferences = load_trainer_preferences();
    const auto appearance = load_desktop_ui_preferences();
    table view;
    view.set_slot_count(4);
    view.start_quiz(0, true);
    const auto session = view.capture_session_state();
    const auto visit = [&](settings_template_widget* classic) {
        auto* browse = classic->findChild<QPushButton*>(
            QStringLiteral("browse_strategies")
        );
        QVERIFY(browse);
        bool visited = false;
        QTimer::singleShot(0, classic, [&] {
            auto* dialog = classic->findChild<QDialog*>(
                QStringLiteral("strategy_browser_dialog")
            );
            QVERIFY(dialog);
            struct reject_on_exit {
                QDialog* dialog;
                ~reject_on_exit() { dialog->reject(); }
            } closer { dialog };
            visited = true;
            auto* list = dialog->findChild<QListView*>(
                QStringLiteral("strategy_browser_list")
            );
            QVERIFY(list);
            list->setCurrentIndex(
                list->model()->index(list->model()->rowCount() - 1, 0)
            );
            auto* tabs = dialog->findChild<QTabWidget*>(
                QStringLiteral("strategy_browser_views")
            );
            tabs->setCurrentIndex(1);
            dialog->resize(360, 480);
            QCoreApplication::processEvents();
            QCOMPARE(dialog->width(), 360);
            QCOMPARE(view.capture_session_state(), session);
        });
        browse->click();
        QVERIFY(visited);
    };
    // Same Classic widget as the Settings / Strategies tab.
    settings_template_widget classic(
        settings_tab_kind::strategies, nullptr, catalog.strategies[1].name
    );
    classic.show();
    auto* classic_list = classic.findChild<QListWidget*>();
    QVERIFY(classic_list);
    QCOMPARE(classic_list->currentItem()->text(), catalog.strategies[1].name);
    visit(&classic);
    QCOMPARE(classic_list->currentItem()->text(), catalog.strategies[1].name);
    // Actual slot Info route, not a second selection/apply implementation.
    auto* slot = view.findChild<table_slot*>();
    bool info_visited = false;
    QTimer::singleShot(0, slot, [&] {
        auto* info = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(info);
        auto* editor = info->findChild<settings_template_widget*>();
        QVERIFY(editor);
        info_visited = true;
        visit(editor);
        info->reject();
    });
    QVERIFY(
        QMetaObject::invokeMethod(
            slot, "on_info_button_clicked", Qt::DirectConnection
        )
    );
    QVERIFY(info_visited);
    QCOMPARE(view.capture_session_state(), session);
    QCOMPARE(load_trainer_preferences(), preferences);
    QCOMPARE(load_desktop_ui_preferences(), appearance);
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
