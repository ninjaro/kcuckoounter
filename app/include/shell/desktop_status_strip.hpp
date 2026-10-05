#ifndef KCUCKOOUNTER_SHELL_DESKTOP_STATUS_STRIP_HPP
#define KCUCKOOUNTER_SHELL_DESKTOP_STATUS_STRIP_HPP

#include <QFrame>

class QLabel;
class QProgressBar;
class QSlider;

namespace status_strip_support {
class desktop_status_layout;
}

// Shared Qt/KDE composition; the shell owns the values and their handlers.
class desktop_status_strip : public QFrame {
public:
    explicit desktop_status_strip(QWidget* parent);
    void set_instruments(bool enabled);

    QLabel* status = nullptr;
    QLabel* readout = nullptr;
    QLabel* clock = nullptr;
    QSlider* slider = nullptr;
    QProgressBar* progress = nullptr;

private:
    status_strip_support::desktop_status_layout* row = nullptr;
};

#endif // KCUCKOOUNTER_SHELL_DESKTOP_STATUS_STRIP_HPP
