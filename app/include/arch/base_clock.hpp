#ifndef KCUCKOOUNTER_ARCH_BASE_CLOCK_HPP
#define KCUCKOOUNTER_ARCH_BASE_CLOCK_HPP

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class base_clock : public QObject {
    Q_OBJECT

public:
    explicit base_clock(QObject* parent = nullptr);
    ~base_clock() override;

    base_clock(const base_clock&) = delete;
    base_clock& operator=(const base_clock&) = delete;
    base_clock(base_clock&&) = delete;
    base_clock& operator=(base_clock&&) = delete;

    void set_interval(int interval_ms);
    void set_single_shot(bool single_shot);

    void start(bool emit_immediately = false);
    void pause();
    void stop();
    void reset();
    bool is_active() const;

    void restart_elapsed();
    qint64 elapsed_time_ms() const;
    double elapsed_time_sec() const;
    QString time_string_mm_ss() const;
    QString time_string_hh_mm_ss() const;

    static void single_shot(
        int interval_ms, QObject* context, std::function<void()> handler
    );

signals:
    void timeout();
    void ticked(qint64 elapsed_ms, qint64 delta_ms);
    void ticked_seconds(double elapsed_sec, double delta_sec);

private:
    struct clock_state;
    std::unique_ptr<clock_state> state;

    void on_timeout();
};

#endif // KCUCKOOUNTER_ARCH_BASE_CLOCK_HPP
