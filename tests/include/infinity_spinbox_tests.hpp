#ifndef KCUCKOOUNTER_INFINITY_SPINBOX_TESTS_HPP
#define KCUCKOOUNTER_INFINITY_SPINBOX_TESTS_HPP
#include <QObject>

class infinity_spinbox_tests : public QObject {
    Q_OBJECT

public:
    explicit infinity_spinbox_tests(QObject* parent = nullptr);

private slots:
    /// @brief Verifies default state is not infinity mode.
    void default_state_is_not_infinity();
    /// @brief Verifies toggling infinity mode updates the flag.
    void set_infinity_mode_toggles_flag();
    /// @brief Verifies numeric text when infinity mode is disabled.
    void text_is_numeric_when_not_infinity_mode();
    /// @brief Verifies infinity text when infinity mode is enabled.
    void text_is_inf_when_infinity_mode();
};

#endif // KCUCKOOUNTER_INFINITY_SPINBOX_TESTS_HPP
