#ifndef KCUCKOOUNTER_TABLE_TABLE_HPP
#define KCUCKOOUNTER_TABLE_TABLE_HPP

#include "arch/random_generator.hpp"
#include "arch/time_interface.hpp"
#include "arch/widget_helpers.hpp"
#include "image/raster_cache.hpp"
#include "image/rasterization_runner.hpp"
#include "settings/preferences.hpp"
#include "settings/session_checkpoint.hpp"
#include "table/gameplay_layout.hpp"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <QtGlobal>
#include <memory>
#include <optional>
#include <vector>

class QGridLayout;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class table_slot;
Q_MOC_INCLUDE("table/table_slot.hpp")

class table : public BaseWidget {
    Q_OBJECT
    friend class table_tests;

public:
    explicit table(BaseWidget* parent = nullptr);
    ~table() override;

    // Explicit target scene installation; takes ownership even on rejection.
    // Preflight leaves the displayed scene unchanged. A populated legacy game
    // must be explicitly cleared first. No gameplay timer/launcher is started.
    [[nodiscard]] bool
    install_gameplay_session(std::unique_ptr<gameplay::session> owner);
    [[nodiscard]] gameplay::session* active_gameplay_session();
    [[nodiscard]] const gameplay::session* active_gameplay_session() const;
    // Host calls after accepted domain changes. Never repacks or advances play.
    void refresh_gameplay_session();
    [[nodiscard]] bool
    swap_gameplay_decks(gameplay::deck_id first, gameplay::deck_id second);
    // Destroys deck views before their owner; explicitly returns to empty
    // legacy mode.
    void clear_gameplay_session();

    // Opt-in target runtime, GUI thread only. Installation is not Start.
    // All actions settle the same monotonic clock before domain mutation.
    [[nodiscard]] bool start_gameplay_runtime(std::uint64_t dealing_seed);
    [[nodiscard]] bool pause_gameplay_runtime();
    [[nodiscard]] bool resume_gameplay_runtime();
    [[nodiscard]] bool finish_gameplay_runtime();
    [[nodiscard]] bool set_gameplay_pick_interval(int interval_ms);

    // GUI-only observation key, not a checkpoint/schema. A non-global Single
    // deck can be re-asked within the same batch; scene and reveal position
    // prevent a stale view action from answering that replacement question.
    struct gameplay_prompt_key {
        gameplay::deck_id owner;
        std::uint64_t batch_id;
        std::uint64_t physical_cards;
        std::uint64_t scene_revision;
        bool operator==(const gameplay_prompt_key&) const = default;
    };

    [[nodiscard]] std::optional<gameplay_prompt_key>
    gameplay_quiz_prompt(gameplay::deck_id id) const;
    [[nodiscard]] bool
    edit_gameplay_quiz(const gameplay_prompt_key& prompt, std::int64_t input);
    [[nodiscard]] bool check_gameplay_quiz(const gameplay_prompt_key& prompt);
    [[nodiscard]] bool skip_gameplay_quiz(const gameplay_prompt_key& prompt);
    // Read-only target projections for the existing native HUD, not a timer
    // or duplicated scoring model. Empty without an installed owned scene.
    [[nodiscard]] QString gameplay_status_text() const;
    [[nodiscard]] QString gameplay_clock_text() const;
    [[nodiscard]] bool focus_gameplay_quiz(
        std::optional<gameplay::deck_id> after = std::nullopt,
        bool reverse = false
    );

    void set_slot_count(int count);
    void start_quiz(int quiz_type_index, bool wait_for_answers);
    void clear_quiz();
    void set_paused(bool paused);
    void set_pick_interval(int interval_ms);
    [[nodiscard]] int pick_interval() const;
    [[nodiscard]] card_orientation_mode current_card_orientation() const;
    [[nodiscard]] bool has_open_gameplay_settings() const;
    [[nodiscard]] bool has_usable_gameplay_layout() const;
    // Existing slot-owned latest corrections, not a second answer history.
    // At most one small record per deck; this projection copies no shoes.
    [[nodiscard]] bool has_gameplay_corrections() const;
    [[nodiscard]] std::vector<gameplay::quiz_answer>
    gameplay_corrections() const;
    void set_dealing_mode(int mode_index);
    void set_allow_skipping(bool allow);
    void set_card_orientation(card_orientation_mode orientation);
    void set_frame_style(slot_frame_style style);
    void set_action_style(slot_action_style style);
    void set_settings_style(slot_settings_style style);
    void set_quiz_presentation(
        quiz_answer_style answer, quiz_feedback_style feedback
    );
    void schedule_card_preload();
    void prepare_cards_for_start();
    void apply_theme();
    bool is_rasterization_busy() const;
    raster_cache* shared_raster_cache_service();
    const raster_cache* shared_raster_cache_service() const;
    [[nodiscard]] table_session_state capture_session_state() const;
    [[nodiscard]] QVector<drill_slot_preferences>
    capture_drill_settings() const;
    // Invalid/unsupported input leaves the entire table unchanged. This
    // prepares a fresh session; normal start_quiz still creates the new shoes.
    bool configure_drill(const training_drill& drill);
    bool restore_session_state(const table_session_state& state);

public slots:
    void on_clock_tick(qint64 elapsed_ms, qint64 delta_ms);

signals:
    // Close external setup views synchronously before their owner is replaced.
    void gameplay_session_about_to_change();
    void gameplay_session_changed();
    void gameplay_layout_changed();
    // Accepted setup edit invalidated preparation. The launcher owns seed,
    // budgets and generation; typing and Show Count never emit this.
    void gameplay_preparation_required();
    // Target-only notifications; no legacy score/game-over or v1 callbacks.
    void gameplay_runtime_updated();
    void gameplay_quiz_resolved(const gameplay::quiz_answer& answer);
    // One notification per published answer batch, or explicit dismissal;
    // never a clock tick. The retained records still belong to bound slots.
    void gameplay_corrections_changed();
    void gameplay_runtime_failed(const QString& diagnostic);
    void rasterization_busy_changed(bool busy);
    void game_over();
    void dialog_opened();
    void score_adjusted(int correct_delta, int total_delta);

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

    std::vector<table_slot*> slot_widgets;
    std::unique_ptr<gameplay::session> gameplay_owner;
    std::unique_ptr<gameplay::layout_transition> gameplay_layout;
    // Ephemeral presentation only; never advances the gameplay clock.
    QVariantAnimation gameplay_layout_animation;
    double gameplay_layout_progress = 1.0;
    BaseWidget* gameplay_motion_layer = nullptr;
    bool gameplay_content_motion = false;

    struct rotation_motion {
        double from;
        double to;
    };

    std::vector<rotation_motion> gameplay_rotation_paths;
    QTimer gameplay_timer;
    QElapsedTimer gameplay_elapsed;
    qint64 gameplay_sampled_ms = 0;
    bool gameplay_runtime_attached = false;
    std::uint64_t gameplay_pick_elapsed_ms = 0;
    std::uint64_t gameplay_scene_revision = 0;
    std::optional<std::uint64_t> gameplay_presented_batch;
    QString gameplay_runtime_failure;
    table_slot* swap_source_slot;
    table_slot* copy_source_slot;
    card_orientation_mode card_orientation;
    slot_frame_style frame_style = slot_frame_style::classic;
    slot_action_style action_style = slot_action_style::classic;
    slot_settings_style settings_style = slot_settings_style::classic;
    quiz_answer_style answer_style = quiz_answer_style::numeric;
    quiz_feedback_style feedback_style = quiz_feedback_style::classic;
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
    std::optional<raster_cache::entry_key> displayed_shared_faces_key;
    std::optional<raster_cache::entry_key> warming_shared_faces_key;
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
    void
    update_shared_card_face_need(bool immediate = false, bool force = false);
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
    [[nodiscard]] std::unique_ptr<table_slot> create_slot_widget();
    void destroy_slot_widgets();
    [[nodiscard]] std::optional<packing::equal_packing_result>
    pack_gameplay_slots(std::size_t count) const;
    void project_gameplay_layout();
    void paint_gameplay_motion(QPainter& painter);
    void retire_gameplay_content_motion();
    void finish_gameplay_layout_transition();
    void restart_gameplay_layout_transition();
    void stop_gameplay_clock();
    void on_gameplay_clock_tick();
    [[nodiscard]] bool
    synchronize_gameplay_clock(std::vector<gameplay::quiz_answer>& answers);
    // Deterministic clock kernel, also used by the actual-source table tests.
    [[nodiscard]] bool advance_gameplay_runtime(
        std::uint64_t elapsed_ms, std::vector<gameplay::quiz_answer>& answers
    );
    void
    publish_gameplay_runtime(const std::vector<gameplay::quiz_answer>& answers);
    [[nodiscard]] bool
    current_gameplay_prompt(const gameplay_prompt_key& prompt) const;
    [[nodiscard]] bool
    answer_gameplay_quiz(const gameplay_prompt_key& prompt, bool skip);
    void on_pick_timeout();
    void update_rasterization_state(table_slot* slot, bool busy);
    void refresh_rasterization_busy_state();
    void clear_swap_selection();
    void clear_copy_selection();
    void update_copy_button_labels(table_slot* selected_slot = nullptr);
    bool all_slots_exhausted() const;
    void handle_game_over();
};

Q_DECLARE_METATYPE(gameplay::quiz_answer)

#endif // KCUCKOOUNTER_TABLE_TABLE_HPP
