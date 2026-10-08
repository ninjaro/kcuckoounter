// QtTest invokes these slots through the meta-object system.
// NOLINTBEGIN(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
#include "table/gameplay_start_tests.hpp"

#include "table/table_fixture.hpp"

#include <QPointer>
#include <QtTest/QtTest>

#include <stdexcept>

using namespace table_test;

void gameplay_start_tests::initial_capture_matches_start() {
    auto owner = owned_clock_fixture(4);
    QVERIFY(owner->set_traversal(std::vector<std::size_t> { 3, 1, 0, 2 }));
    const auto before = *owner->deck({ 0 });
    const auto initial = gameplay::capture_start(*owner, UINT64_MAX);
    QVERIFY(initial);
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    QCOMPARE(*owner->deck({ 0 }), before);
    QVERIFY(!gameplay::capture_checkpoint(*owner));
    QCOMPARE(owner->random_dealing().seed, std::uint64_t { 0 });
    QVERIFY(owner->start(UINT64_MAX));
    QVERIFY(gameplay::capture_checkpoint(*owner) == initial);
    QVERIFY(!gameplay::capture_start(*owner, 3));
    QVERIFY(owner->pause());
    QVERIFY(!gameplay::capture_start(*owner, 3));
}

void gameplay_start_tests::initial_capture_checks_resources() {
    auto owner = owned_clock_fixture(2);
    gameplay::generation_policy tight;
    tight.maximum_cards = 1;
    QVERIFY(!gameplay::capture_start(*owner, 7, tight));
    QCOMPARE(owner->phase(), gameplay::session_phase::setup);
    auto settings = owner->configuration();
    settings.allow_extra_slots = false;
    QVERIFY(owner->update_slot_limits({ 2, 64 }));
    QVERIFY(owner->configure(settings));
    QVERIFY(owner->update_slot_limits({ 1, 64 }));
    QVERIFY(!gameplay::capture_start(*owner, 7));
    QVERIFY(!owner->start(7)); // same readiness/consent rule
    QVERIFY(owner->update_slot_limits({ 2, 64 }));
    auto config = owner->deck({ 0 })->configuration;
    config.deck_count = 2;
    QVERIFY(owner->configure_deck({ 0 }, config));
    QVERIFY(!gameplay::capture_start(*owner, 7)); // invalidated preparation
    QVERIFY(!owner->start(7));
}

void gameplay_start_tests::gate_precedes_activation() {
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    const auto expected = gameplay::capture_start(*view.gameplay_owner, 91);
    QSignalSpy updates(&view, &table::gameplay_runtime_updated);
    QSignalSpy legacy(&view, &table::score_adjusted);
    std::optional<gameplay_checkpoint> admitted;
    bool setup_before_gate = false;
    QVERIFY(view.start_gameplay_runtime(91, [&](const auto& checkpoint) {
        setup_before_gate
            = view.gameplay_owner->phase() == gameplay::session_phase::setup
            && !view.gameplay_runtime_attached
            && !view.gameplay_timer.isActive()
            && !view.gameplay_elapsed.isValid() && updates.isEmpty();
        admitted = checkpoint;
        return true;
    }));
    QVERIFY(setup_before_gate);
    QVERIFY(admitted && admitted->state == *expected);
    QCOMPARE(admitted->pickup_elapsed_ms, std::uint64_t { 0 });
    QVERIFY(view.gameplay_runtime_attached && view.gameplay_timer.isActive());
    QVERIFY(gameplay::capture_checkpoint(*view.gameplay_owner) == expected);
    QCOMPARE(updates.count(), 1);
    QCOMPARE(legacy.count(), 0);
    view.stop_gameplay_clock();
}

void gameplay_start_tests::gate_rejection_keeps_setup() {
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    auto* owner = view.gameplay_owner.get();
    const auto before = gameplay::capture_start(*owner, 91);
    QSignalSpy updates(&view, &table::gameplay_runtime_updated);
    int called = 0;
    const table::start_gate reject = [&](const auto&) {
        ++called;
        return false;
    };
    QVERIFY(!view.start_gameplay_runtime(91, reject));
    QCOMPARE(called, 1);
    QVERIFY(gameplay::capture_start(*owner, 91) == before);
    QCOMPARE(view.gameplay_owner.get(), owner);
    QVERIFY(!view.gameplay_runtime_attached && !view.gameplay_timer.isActive());
    QVERIFY(!view.gameplay_start_pending);
    QCOMPARE(updates.count(), 0);
    QVERIFY(!view.start_gameplay_runtime(91, table::start_gate {}));
    gameplay::generation_policy tight;
    tight.maximum_cards = 1;
    QVERIFY(!view.start_gameplay_runtime(91, reject, tight));
    view.resize(1, 1);
    QVERIFY(!view.start_gameplay_runtime(91, reject));
    QCOMPARE(called, 1); // no I/O on failed preflight
}

void gameplay_start_tests::gate_blocks_reentrant_start_and_replacement() {
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    auto* owner = view.gameplay_owner.get();
    const auto scene = view.gameplay_scene();
    bool protected_scene = false;
    QVERIFY(view.start_gameplay_runtime(8, [&](const auto& checkpoint) {
        const bool raw = view.start_gameplay_runtime(9);
        const bool gated
            = view.start_gameplay_runtime(10, [](const auto&) { return true; });
        const bool installed
            = view.install_gameplay_session(owned_clock_fixture(1));
        const bool restored = view.restore_gameplay_checkpoint(checkpoint);
        view.clear_gameplay_session();
        protected_scene = !raw && !gated && !installed && !restored
            && view.gameplay_owner.get() == owner
            && view.gameplay_scene() == scene;
        return protected_scene;
    }));
    QVERIFY(protected_scene);
    view.stop_gameplay_clock();
}

void gameplay_start_tests::gate_checks_callback_mutation_data() {
    QTest::addColumn<int>("mutation");
    QTest::newRow("speed") << 0;
    QTest::newRow("policy") << 1;
    QTest::newRow("stream") << 2;
    QTest::newRow("geometry") << 3;
}

void gameplay_start_tests::gate_checks_callback_mutation() {
    QFETCH(int, mutation);
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(2)));
    bool changed = false;
    QVERIFY(!view.start_gameplay_runtime(5, [&](const auto&) {
        if (mutation == 0)
            changed = view.gameplay_owner->set_pick_interval_ms(750);
        else if (mutation == 1) {
            auto config = view.gameplay_owner->configuration();
            config.allow_skip = !config.allow_skip;
            changed = view.gameplay_owner->configure(config);
        } else if (mutation == 2) {
            auto config = view.gameplay_owner->deck({ 0 })->configuration;
            config.deck_count = 2;
            changed = view.gameplay_owner->configure_deck({ 0 }, config);
        } else {
            view.resize(1, 1);
            changed = true;
        }
        return true;
    }));
    QVERIFY(changed);
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view.gameplay_runtime_attached && !view.gameplay_timer.isActive());
    QVERIFY(!view.gameplay_start_pending);
}

void gameplay_start_tests::gate_survives_destruction_and_exceptions() {
    auto* dying = new table;
    dying->resize(1000, 800);
    QVERIFY(dying->install_gameplay_session(owned_clock_fixture(1)));
    QPointer<table> guard(dying);
    QVERIFY(!dying->start_gameplay_runtime(4, [dying](const auto&) {
        delete dying;
        return true;
    }));
    QVERIFY(!guard);
    table view;
    view.resize(1000, 800);
    QVERIFY(view.install_gameplay_session(owned_clock_fixture(1)));
    QVERIFY_EXCEPTION_THROWN(
        (void)view.start_gameplay_runtime(
            4,
            [](const auto&) -> bool {
                throw std::runtime_error("admission failed");
            }
        ),
        std::runtime_error
    );
    QVERIFY(!view.gameplay_start_pending);
    QCOMPARE(view.gameplay_owner->phase(), gameplay::session_phase::setup);
    QVERIFY(!view.gameplay_timer.isActive());
    QVERIFY(view.start_gameplay_runtime(4)); // old explicit non-durable API
    view.stop_gameplay_clock();
}

// NOLINTEND(readability-convert-member-functions-to-static,
// readability-make-member-function-const)
