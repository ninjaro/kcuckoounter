#ifndef KCUCKOOUNTER_WIDGETS_TABLE_HPP
#define KCUCKOOUNTER_WIDGETS_TABLE_HPP

#include "helpers/random_generator.hpp"
#include "helpers/time_interface.hpp"
#include "helpers/widget_helpers.hpp"
#include <QSet>
#include <QSize>
#include <memory>
#include <vector>

class QGridLayout;
class QPaintEvent;
class QResizeEvent;
class table_slot;
class card_packer;

class table : public BaseWidget {
    Q_OBJECT

public:
    explicit table(BaseWidget* parent = nullptr);
    ~table() override;

    void set_slot_count(int count);
    void start_quiz(int quiz_type_index, bool wait_for_answers);
    void clear_quiz();
    void set_paused(bool paused);
    void set_pick_interval(int interval_ms);
    void set_dealing_mode(int mode_index);
    void set_allow_skipping(bool allow);
    void schedule_card_preload();
    void prepare_cards_for_start();
    void apply_theme();
    bool is_rasterization_busy() const;

public slots:
    void on_clock_tick(qint64 elapsed_ms, qint64 delta_ms);

signals:
    void rasterization_busy_changed(bool busy);
    void game_over();
    void dialog_opened();
    void score_adjusted(int correct_delta, int total_delta);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_slot_swap(table_slot* slot);
    void on_slot_copy(table_slot* slot);
    void on_slot_copy_all(table_slot* slot);
    void on_preload_tick();

private:
    enum class dealing_mode { sequential, random, simultaneous };

    std::vector<table_slot*> slot_widgets;
    table_slot* swap_source_slot;
    table_slot* copy_source_slot;
    std::unique_ptr<card_packer> card_packer_instance;
    int pick_interval_ms;
    qint64 pick_elapsed_ms;
    bool quiz_running;
    bool quiz_paused;
    bool allow_skipping;
    dealing_mode current_mode;
    int next_slot_index;
    QSet<table_slot*> rasterizing_slots;
    bool rasterization_busy;
    random_generator random_gen;
    std::unique_ptr<time_interface> preload_timer;
    int rasterization_delay_ms() const;
    void update_layout();
    void on_pick_timeout();
    void update_rasterization_state(table_slot* slot, bool busy);
    bool all_slots_exhausted() const;
    void handle_game_over();
};

#endif // KCUCKOOUNTER_WIDGETS_TABLE_HPP
