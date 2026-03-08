#ifndef KCUCKOOUNTER_TESTS_DEBUG_BROADCASTER_TESTS_HPP
#define KCUCKOOUNTER_TESTS_DEBUG_BROADCASTER_TESTS_HPP

#include <QObject>

class debug_broadcaster_tests : public QObject {
    Q_OBJECT

private slots:
    void is_disabled_until_explicitly_enabled();
    void publishes_messages_over_local_ipc();
    void backpressure_drops_low_priority_before_high_priority();
};

#endif // KCUCKOOUNTER_TESTS_DEBUG_BROADCASTER_TESTS_HPP
