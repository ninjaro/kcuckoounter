#ifndef KCUCKOOUNTER_INCLUDE_TABLE_TABLE_HPP
#define KCUCKOOUNTER_INCLUDE_TABLE_TABLE_HPP

#include "arch/random_generator.hpp"
#include "arch/time_interface.hpp"
#include "arch/widget_helpers.hpp"
#include "image/raster_cache.hpp"
#include "image/rasterization_runner.hpp"
#include <QFutureWatcher>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <memory>
#include <optional>
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
    raster_cache* shared_raster_cache_service();
    const raster_cache* shared_raster_cache_service() const;

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
    void on_shared_rasterization_requested(int target_cache_px);
    void on_shared_cache_result_updated(const raster_cache::entry_key& key);
    void on_shared_rasterization_finished();

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
    rasterization_runner main_faces_runner;
    raster_cache raster_cache_service;
    QFutureWatcher<QVector<QImage>> shared_faces_watcher;
    std::optional<raster_cache::entry_key> active_shared_faces_key;
    QString active_card_sheet_source_id;
    int rasterization_delay_ms() const;
    int compute_max_card_face_need_short_px() const;
    void update_shared_card_face_need(bool immediate = false);
    void clear_shared_card_faces();
    void apply_shared_card_faces_from_entry(const raster_cache::entry_key& key);
    void update_layout();
    void on_pick_timeout();
    void update_rasterization_state(table_slot* slot, bool busy);
    void refresh_rasterization_busy_state();
    void clear_swap_selection();
    void clear_copy_selection();
    void update_copy_button_labels(table_slot* selected_slot = nullptr);
    bool all_slots_exhausted() const;
    void handle_game_over();
};

#endif // KCUCKOOUNTER_INCLUDE_TABLE_TABLE_HPP
