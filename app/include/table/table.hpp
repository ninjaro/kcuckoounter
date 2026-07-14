#ifndef KCUCKOOUNTER_TABLE_TABLE_HPP
#define KCUCKOOUNTER_TABLE_TABLE_HPP

#include "arch/random_generator.hpp"
#include "arch/time_interface.hpp"
#include "arch/widget_helpers.hpp"
#include "image/raster_cache.hpp"
#include "image/rasterization_runner.hpp"
#include "monitor/geometry_debug_telemetry.hpp"
#include <QFutureWatcher>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>
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
    geometry_debug_snapshot current_geometry_debug_snapshot() const;

public slots:
    void on_clock_tick(qint64 elapsed_ms, qint64 delta_ms);

signals:
    void rasterization_busy_changed(bool busy);
    void game_over();
    void dialog_opened();
    void score_adjusted(int correct_delta, int total_delta);
    void
    debug_geometry_snapshot_updated(const geometry_debug_snapshot& snapshot);
    void debug_resize_transition_recorded(
        const resize_transition_debug_event& event
    );

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_slot_swap(table_slot* slot);
    void on_slot_copy(table_slot* slot);
    void on_slot_copy_all(table_slot* slot);
    void on_slot_rasterization_busy_changed(bool busy);
    void on_preload_tick();
    void on_shared_rasterization_requested(int target_cache_px);
    void on_shared_cache_result_updated(const raster_cache::entry_key& key);
    void on_shared_rasterization_finished();

private:
    enum class dealing_mode { sequential, random, simultaneous };
    enum class cache_update_trigger {
        runtime,
        slot_count_change,
        window_resize,
        screen_change,
        game_start,
        quiz_state_change,
        theme_change,
        raster_completion,
    };

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
    rasterization_runner::decision_kind last_cache_decision;
    cache_update_trigger last_cache_trigger;
    int last_requested_target_bucket_px;
    raster_cache raster_cache_service;
    QFutureWatcher<QVector<QImage>> shared_faces_watcher;
    std::optional<raster_cache::entry_key> active_shared_faces_key;
    std::optional<raster_cache::entry_key> displayed_shared_faces_key;
    std::optional<raster_cache::entry_key> warming_shared_faces_key;
    raster_cache::result::debug_fallback_usage
        active_shared_faces_fallback_usage;
    bool shared_faces_refresh_queued;
    QString active_card_sheet_source_id;
    int active_shared_bucket_px;
    qint64 active_shared_generation_id;
    QString warming_card_sheet_source_id;
    int warming_shared_bucket_px;
    qint64 warming_shared_generation_id;
    qint64 next_shared_generation_id;
    QSet<raster_cache::entry_key> retained_shared_faces_keys;
    int rasterization_delay_ms() const;
    int max_card_need_short_px() const;
    void update_shared_card_face_need(
        bool immediate = false,
        cache_update_trigger trigger = cache_update_trigger::runtime,
        bool force = false
    );
    void record_cache_evaluation(
        const rasterization_runner::evaluation& evaluation,
        cache_update_trigger trigger
    );
    static QString
    cache_decision_label(rasterization_runner::decision_kind decision);
    static QString cache_trigger_label(cache_update_trigger trigger);
    void clear_shared_card_faces();
    static QString generation_render_scope(qint64 generation_id);
    static qint64 generation_id_from_render_scope(const QString& render_scope);
    static raster_cache::entry_key entry_key_for_generation(
        const QString& source_id, int target_bucket_px, qint64 generation_id
    );
    void
    begin_warming_generation(const QString& source_id, int target_bucket_px);
    void retire_warming_generation();
    bool start_shared_raster_for_key(const raster_cache::entry_key& key);
    void cutover_to_ready_generation(const raster_cache::entry_key& key);
    void apply_shared_faces_entry(const raster_cache::entry_key& key);
    void remember_shared_faces_key(const raster_cache::entry_key& key);
    void forget_shared_faces_key(const raster_cache::entry_key& key);
    void enforce_shared_generation_bounds();
    void update_layout();
    void on_pick_timeout();
    void update_rasterization_state(table_slot* slot, bool busy);
    void refresh_rasterization_busy_state();
    geometry_debug_snapshot build_geometry_debug_snapshot(
        const raster_cache::debug_snapshot& cache_snapshot
    ) const;
    void emit_geometry_debug_snapshot();
    void clear_swap_selection();
    void clear_copy_selection();
    void update_copy_button_labels(table_slot* selected_slot = nullptr);
    bool all_slots_exhausted() const;
    void handle_game_over();
};

#endif // KCUCKOOUNTER_TABLE_TABLE_HPP
