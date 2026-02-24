#include "include/infinity_spinbox_tests.hpp"
#include "arch/str_label.hpp"
#include "table/infinity_spinbox.hpp"

#include <QtTest/QtTest>

infinity_spinbox_tests::infinity_spinbox_tests(QObject* parent)
    : QObject(parent) { }

void infinity_spinbox_tests::default_state_is_not_infinity() {
    infinity_spinbox spin_box;

    QVERIFY(!spin_box.infinity_mode());
}

void infinity_spinbox_tests::set_infinity_mode_toggles_flag() {
    infinity_spinbox spin_box;

    QVERIFY(!spin_box.infinity_mode());

    spin_box.set_infinity_mode(true);
    QVERIFY(spin_box.infinity_mode());

    spin_box.set_infinity_mode(true);
    QVERIFY(spin_box.infinity_mode());

    spin_box.set_infinity_mode(false);
    QVERIFY(!spin_box.infinity_mode());

    spin_box.set_infinity_mode(false);
    QVERIFY(!spin_box.infinity_mode());
}

void infinity_spinbox_tests::text_is_numeric_when_not_infinity_mode() {
    infinity_spinbox spin_box;
    spin_box.setRange(0, 100);

    spin_box.set_infinity_mode(false);

    spin_box.setValue(0);
    QCOMPARE(spin_box.text(), str_label("0"));

    spin_box.setValue(5);
    QCOMPARE(spin_box.text(), str_label("5"));

    spin_box.setValue(42);
    QCOMPARE(spin_box.text(), str_label("42"));
}

void infinity_spinbox_tests::text_is_inf_when_infinity_mode() {
    infinity_spinbox spin_box;
    spin_box.setRange(0, 100);

    spin_box.set_infinity_mode(true);

    spin_box.setValue(0);
    QCOMPARE(spin_box.text(), str_label("inf"));

    spin_box.setValue(5);
    QCOMPARE(spin_box.text(), str_label("inf"));

    spin_box.setValue(42);
    QCOMPARE(spin_box.text(), str_label("inf"));
}
