#ifndef KCUCKOOUNTER_TESTS_MONITOR_PARITY_CHECKER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_MONITOR_PARITY_CHECKER_TESTS_HPP

#include <QObject>

class monitor_parity_checker_tests : public QObject {
    Q_OBJECT

private slots:
    void aligned_embedded_and_external_payloads_have_no_warnings();
    void drifting_payloads_surface_warnings();
};

#endif // KCUCKOOUNTER_TESTS_MONITOR_PARITY_CHECKER_TESTS_HPP
