#ifndef KCUCKOOUNTER_TABLE_TABLE_HPP
#define KCUCKOOUNTER_TABLE_TABLE_HPP

#include "arch/widget_helpers.hpp"
#include "image/card_images.hpp"
#include "settings/gameplay_checkpoint.hpp"
#include "settings/preferences.hpp"
#include "table/gameplay_layout.hpp"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMap>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QtGlobal>
#include <functional>
#include <memory>
#include <optional>
#include <shared_ui/qt/scene_transition.hpp>
#include <vector>

class QGridLayout;
class QPaintEvent;
class QResizeEvent;
class table_slot;
Q_MOC_INCLUDE("table/table_slot.hpp")

class table : public BaseWidget {
    Q_OBJECT
    friend class table_tests;
    friend class gameplay_start_tests;
    friend class gameplay_restore_tests;
    friend class gameplay_completion_tests;
    friend class gameplay_host_tests;
    friend class gameplay_mobile_tests;
    friend class gameplay_reservation;

public:
    explicit table(BaseWidget* parent = nullptr);
    ~table() override;

    // Session installation takes ownership even on rejection.
    // Preflight leaves the displayed scene unchanged; installation never
    // Starts.
    [[nodiscard]] bool
    install_gameplay_session(std::unique_ptr<gameplay::session> owner);
    /// Settle active time before capture; no automatic Pause or settings I/O.
    /// A replaced/finished scene cannot produce an active checkpoint.
    [[nodiscard]] std::optional<gameplay_checkpoint>
    capture_gameplay_checkpoint(
        const gameplay::generation_policy& resources = {}
    );
    /// Preflight current Packing and install one paused, resumable owner.
    /// Never Start, restore animation history or spend background wall time.
    [[nodiscard]] bool restore_gameplay_checkpoint(
        const gameplay_checkpoint& checkpoint,
        const gameplay::generation_policy& resources = {}
    );
    using scene_binding = std::function<void(std::uint64_t)>;
    /// Bind host metadata after Paused installation, before any publication.
    /// The callback must not throw, do I/O, publish or mutate the gameplay
    /// scene.
    [[nodiscard]] bool restore_gameplay_checkpoint(
        const gameplay_checkpoint& checkpoint,
        const gameplay::generation_policy& resources, const scene_binding& bind
    );
    [[nodiscard]] gameplay::session* active_gameplay_session();
    [[nodiscard]] const gameplay::session* active_gameplay_session() const;
    // Host calls after accepted domain changes. Never repacks or advances play.
    void refresh_gameplay_session();
    [[nodiscard]] bool
    swap_gameplay_decks(gameplay::deck_id first, gameplay::deck_id second);
    // Destroys deck views before their owner and returns to an empty table.
    void clear_gameplay_session();

    // Runtime commands are GUI thread only. Installation is not Start.
    // All actions settle the same monotonic clock before domain mutation.
    [[nodiscard]] bool start_gameplay_runtime(std::uint64_t dealing_seed);
    using start_gate = std::function<bool(const gameplay_checkpoint&)>;
    /// Admit the exact initial state before clock/publication; rejection leaves
    /// Setup intact. A successful durable admission is never implicitly undone.
    [[nodiscard]] bool start_gameplay_runtime(
        std::uint64_t dealing_seed, const start_gate& admit,
        const gameplay::generation_policy& resources = {}
    );
    /// Ephemeral ownership token, never a persisted completion sequence.
    [[nodiscard]] std::uint64_t gameplay_scene() const;
    /// Attached native runtime, including Paused/Finished, not clock activity.
    [[nodiscard]] bool has_gameplay_runtime() const;
    [[nodiscard]] bool pause_gameplay_runtime();
    [[nodiscard]] bool resume_gameplay_runtime();
    [[nodiscard]] bool finish_gameplay_runtime();
    [[nodiscard]] bool set_gameplay_pick_interval(int interval_ms);

    // GUI-only observation key, not a checkpoint/schema. A non-global Single
    // deck can be re-asked within the same batch; scene and reveal position
    // prevent a stale view action from answering that replacement question.
    struct gameplay_prompt_key {
        gameplay::deck_id owner;
        std::uint64_t batch_id = 0;
        std::uint64_t physical_cards = 0;
        std::uint64_t scene_revision = 0;
        bool operator==(const gameplay_prompt_key&) const = default;
    };

    [[nodiscard]] std::optional<gameplay_prompt_key>
    gameplay_quiz_prompt(gameplay::deck_id id) const;
    [[nodiscard]] bool
    edit_gameplay_quiz(const gameplay_prompt_key& prompt, std::int64_t input);
    [[nodiscard]] bool check_gameplay_quiz(const gameplay_prompt_key& prompt);
    [[nodiscard]] bool skip_gameplay_quiz(const gameplay_prompt_key& prompt);
    // Read-only projections for the existing native HUD, not a timer
    // or duplicated scoring model. Empty without an installed owned scene.
    [[nodiscard]] QString gameplay_status_text() const;
    [[nodiscard]] QString gameplay_clock_text() const;
    [[nodiscard]] bool focus_gameplay_quiz(
        std::optional<gameplay::deck_id> after = std::nullopt,
        bool reverse = false
    );

    [[nodiscard]] card_orientation_mode current_card_orientation() const;
    [[nodiscard]] bool has_open_gameplay_settings() const;
    [[nodiscard]] bool has_usable_gameplay_layout() const;
    // Existing slot-owned latest corrections, not a second answer history.
    // At most one small record per deck; this projection copies no shoes.
    [[nodiscard]] bool has_gameplay_corrections() const;
    [[nodiscard]] std::vector<gameplay::quiz_answer>
    gameplay_corrections() const;
    void set_card_orientation(card_orientation_mode orientation);
    void set_frame_style(slot_frame_style style);
    void set_action_style(slot_action_style style);
    void set_settings_style(slot_settings_style style);
    void set_quiz_presentation(
        quiz_answer_style answer, quiz_feedback_style feedback
    );
    void schedule_card_preload();
    void apply_theme();
    // Restyles slot controls for the current window chrome; no repacking.
    void refresh_material();
    [[nodiscard]] bool is_rasterization_busy() const;
    [[nodiscard]] static const rasterflow::qt::image_service*
    shared_image_service();
signals:
    // Close external setup views synchronously before their owner is replaced.
    void gameplay_session_about_to_change();
    void gameplay_session_changed();
    void gameplay_layout_changed();
    // Accepted setup edit invalidated preparation. The launcher owns seed,
    // budgets and generation; typing and Show Count never emit this.
    void gameplay_preparation_required();
    // Notifications project the same owned session.
    void gameplay_runtime_updated();
    void gameplay_quiz_resolved(const gameplay::quiz_answer& answer);
    // One notification per published answer batch, or explicit dismissal;
    // never a clock tick. The retained records still belong to bound slots.
    void gameplay_corrections_changed();
    void gameplay_runtime_failed(const QString& diagnostic);
    void rasterization_busy_changed(bool busy);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_slot_swap(table_slot* slot);
    void on_slot_rasterization_busy_changed(bool busy);
    void on_card_images_changed();

private:
    std::vector<table_slot*> slot_widgets;
    std::unique_ptr<gameplay::session> gameplay_owner;
    std::unique_ptr<gameplay::layout_transition> gameplay_layout;
    shared_ui::qt::scene_transition* scene_motion;
    QTimer gameplay_timer;
    QElapsedTimer gameplay_elapsed;
    qint64 gameplay_sampled_ms = 0;
    bool gameplay_runtime_attached = false;
    std::uint64_t gameplay_pick_elapsed_ms = 0;
    std::uint64_t gameplay_scene_revision = 0;
    std::optional<std::uint64_t> gameplay_presented_batch;
    QString gameplay_runtime_failure;
    table_slot* swap_source_slot;
    card_orientation_mode card_orientation;
    slot_frame_style frame_style = slot_frame_style::classic;
    slot_action_style action_style = slot_action_style::classic;
    slot_settings_style settings_style = slot_settings_style::classic;
    quiz_answer_style answer_style = quiz_answer_style::numeric;
    quiz_feedback_style feedback_style = quiz_feedback_style::classic;
    QSet<table_slot*> rasterizing_slots;
    bool rasterization_busy;
    card_image_consumer card_images;
    QHash<QString, QImage> displayed_card_images;
    QMap<QString, QSize> desired_card_sizes;
    QString desired_card_source;
    QSize desired_card_size;
    QString displayed_card_source;
    bool card_demand_active = false;
    bool layout_demand_queued = false;
    void update_card_image_demands(bool immediate = false, bool force = false);
    void clear_presented_card_images();
    [[nodiscard]] bool apply_displayed_card_images();
    void update_layout();
    [[nodiscard]] std::unique_ptr<table_slot> create_slot_widget();
    void destroy_slot_widgets();
    [[nodiscard]] bool install_gameplay_scene(
        std::unique_ptr<gameplay::session> owner,
        const packing::equal_packing_result& packed,
        std::optional<std::uint64_t> pickup_elapsed = std::nullopt,
        const scene_binding& bind = {}
    );
    [[nodiscard]] std::optional<packing::equal_packing_result>
    pack_gameplay_slots(std::size_t count) const;
    void project_gameplay_layout();
    [[nodiscard]] QList<shared_ui::qt::scene_track> layout_tracks() const;
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
    void update_rasterization_state(table_slot* slot, bool busy);
    void refresh_rasterization_busy_state();
    void clear_swap_selection();
    bool gameplay_start_pending = false;
    // One explicit shell-owned metadata sink. No settings I/O or Qt
    // publication.
    std::function<void(const gameplay::session_result&)> gameplay_completion;
};

Q_DECLARE_METATYPE(gameplay::quiz_answer)

#endif // KCUCKOOUNTER_TABLE_TABLE_HPP
