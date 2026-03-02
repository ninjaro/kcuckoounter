#ifndef KCUCKOOUNTER_IMAGE_RASTER_CACHE_HPP
#define KCUCKOOUNTER_IMAGE_RASTER_CACHE_HPP

#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSize>
#include <QString>
#include <QVector>

#include <optional>

class raster_cache : public QObject {
    Q_OBJECT

public:
    enum class cache_namespace {
        main,
        settings,
    };

    enum class resource_kind {
        single_svg,
        card_sheet_faces,
    };

    enum class debug_consumer_scope {
        unknown,
        table_slots,
        settings_theme_carousel,
        settings_strategy_preview,
        image_cacher,
    };

    struct request {
        cache_namespace name_space;
        resource_kind kind;
        QString source_id;
        QString render_scope;
        int need_short_px;
        int target_bucket_px;
        bool high_priority;
        bool interactive;
        bool preview;
    };

    struct entry_key {
        cache_namespace name_space;
        resource_kind kind;
        QString source_id;
        QString render_scope;
        int target_bucket_px;

        bool operator==(const entry_key& other) const;
    };

    struct family_key {
        cache_namespace name_space;
        resource_kind kind;
        QString source_id;
        QString render_scope;

        bool operator==(const family_key& other) const;
    };

    struct result {
        struct debug_fallback_usage {
            int active_theme_keys = 0;
            int default_theme_keys = 0;
            int placeholder_keys = 0;
        };

        entry_key key;
        QSize raster_size;
        int generation;
        qint64 timestamp_ms;
        int use_count;
        QImage single_image;
        QVector<QImage> face_images;
        debug_fallback_usage fallback_usage {};

        bool is_ready() const;
    };

    enum class request_state {
        cache_hit,
        start_async,
        already_in_flight,
        pending_coalesced,
    };

    struct submit_outcome {
        request_state state;
        entry_key key;
        std::optional<result> ready_result;
    };

    struct finish_outcome {
        bool accepted_completion;
        std::optional<entry_key> next_entry_to_start;
    };

    struct debug_delta_counters {
        int entries_added = 0;
        int entries_removed = 0;
        qint64 bytes_added = 0;
        qint64 bytes_removed = 0;
        int images_added = 0;
        int images_removed = 0;
    };

    struct debug_snapshot {
        enum class timing_stage {
            raster_lifecycle,
            coalesced_wait,
        };

        struct debug_size_bucket {
            int target_bucket_px = 0;
            int entry_count = 0;
            qint64 total_bytes = 0;
        };

        struct debug_largest_entry {
            cache_namespace name_space = cache_namespace::main;
            resource_kind kind = resource_kind::single_svg;
            QString source_id;
            QString render_scope;
            int target_bucket_px = 0;
            qint64 estimated_bytes = 0;
        };

        struct debug_requested_entry {
            cache_namespace name_space = cache_namespace::main;
            resource_kind kind = resource_kind::single_svg;
            QString source_id;
            QString render_scope;
            int target_bucket_px = 0;
            int request_count = 0;
        };

        struct debug_expensive_task {
            timing_stage stage = timing_stage::raster_lifecycle;
            cache_namespace name_space = cache_namespace::main;
            resource_kind kind = resource_kind::single_svg;
            QString source_id;
            QString render_scope;
            int target_bucket_px = 0;
            int completed_samples = 0;
            qint64 avg_elapsed_ms = 0;
            qint64 max_elapsed_ms = 0;
        };

        struct debug_subsystem_summary {
            cache_namespace name_space = cache_namespace::main;
            resource_kind kind = resource_kind::single_svg;
            int ready_entries = 0;
            qint64 ready_bytes = 0;
            int request_samples = 0;
            int timing_samples = 0;
            qint64 timing_max_elapsed_ms = 0;
        };

        struct debug_consumer_summary {
            debug_consumer_scope consumer = debug_consumer_scope::unknown;
            int displayed_recent_entries = 0;
            int displayed_recent_images = 0;
            qint64 displayed_recent_ready_bytes = 0;
            qint64 displayed_recent_widget_local_bytes_estimated = 0;
        };

        qint64 snapshot_sequence = 0;
        qint64 timestamp_ms = 0;
        int ready_entries = 0;
        qint64 ready_bytes = 0;
        int ready_images = 0;
        int in_flight_families = 0;
        int pending_families = 0;
        int displayed_ready_entries = 0;
        int cached_only_ready_entries = 0;
        int displayed_ready_images = 0;
        int cached_only_ready_images = 0;
        qint64 widget_local_rasterized_bytes_estimated = 0;
        qint64 widget_local_scaled_bytes_estimated = 0;
        qint64 widget_local_display_bytes_estimated = 0;
        int fallback_active_theme_keys_ready = 0;
        int fallback_default_theme_keys_ready = 0;
        int fallback_placeholder_keys_ready = 0;
        qint64 displayed_entry_window_ms = 0;
        int displayed_entry_coverage_percent = 0;
        int high_water_ready_entries = 0;
        qint64 high_water_ready_bytes = 0;
        int high_water_ready_images = 0;
        debug_delta_counters lifetime_deltas;
        debug_delta_counters interval_deltas;
        int raster_timing_samples = 0;
        qint64 raster_timing_avg_ms = 0;
        qint64 raster_timing_max_ms = 0;
        int coalesced_wait_samples = 0;
        qint64 coalesced_wait_avg_ms = 0;
        qint64 coalesced_wait_max_ms = 0;
        int deadline_readiness_samples = 0;
        int deadline_ready_early = 0;
        int deadline_ready_on_time = 0;
        int deadline_ready_late = 0;
        int unique_size_buckets = 0;
        QVector<debug_size_bucket> size_buckets;
        QVector<debug_largest_entry> largest_entries;
        QVector<debug_requested_entry> top_requested_entries;
        QVector<debug_expensive_task> top_expensive_tasks;
        QVector<debug_subsystem_summary> subsystem_summaries;
        QVector<debug_consumer_summary> consumer_summaries;
    };

    struct debug_task_timing_key {
        debug_snapshot::timing_stage stage
            = debug_snapshot::timing_stage::raster_lifecycle;
        entry_key entry;

        bool operator==(const debug_task_timing_key& other) const;
    };

    explicit raster_cache(QObject* parent = nullptr);

    std::optional<result> get_if_ready(const entry_key& key) const;
    std::optional<result>
    get_if_ready_with_namespace_fallback(const entry_key& key) const;
    void insert_or_update_result(const result& new_result);

    submit_outcome submit_request(const request& req);
    finish_outcome finish_active_request(
        const family_key& key, const entry_key& completed_entry
    );

    bool is_in_flight(const family_key& key) const;
    void mark_in_flight(const family_key& key, const entry_key& active_key);
    void clear_in_flight(const family_key& key);

    void
    set_pending_latest(const family_key& key, const entry_key& pending_key);
    std::optional<entry_key> take_pending_latest(const family_key& key);

    int ready_entry_count() const;
    int ready_entry_count(cache_namespace name_space) const;
    int in_flight_count() const;
    void note_entry_displayed(
        const entry_key& key,
        debug_consumer_scope consumer = debug_consumer_scope::unknown
    );
    void note_entry_no_longer_displayed(
        const entry_key& key,
        debug_consumer_scope consumer = debug_consumer_scope::unknown
    );
    void set_namespace_entry_limit(cache_namespace name_space, int limit);
    bool erase_result(const entry_key& key);
    debug_snapshot get_debug_snapshot() const;
    debug_delta_counters take_interval_deltas();

signals:
    void result_updated(const raster_cache::entry_key& key);
    void debug_snapshot_updated(const raster_cache::debug_snapshot& snapshot);

private:
    static entry_key make_entry_key(const request& req);
    static family_key make_family_key(const request& req);

    struct family_state {
        bool in_flight = false;
        entry_key active_entry;
        qint64 active_started_ms = 0;
        qint64 active_deadline_budget_ms = 0;
        bool has_pending = false;
        entry_key pending_entry;
        qint64 pending_submitted_ms = 0;
        qint64 pending_deadline_budget_ms = 0;
    };

    struct debug_deadline_counters {
        int samples = 0;
        int ready_early = 0;
        int ready_on_time = 0;
        int ready_late = 0;
    };

    struct debug_timing_accumulator {
        int samples = 0;
        qint64 total_ms = 0;
        qint64 max_ms = 0;
    };

    struct debug_task_timing_aggregate {
        int completed_samples = 0;
        qint64 total_elapsed_ms = 0;
        qint64 max_elapsed_ms = 0;
    };

    QHash<entry_key, result> ready_results;
    QHash<cache_namespace, QQueue<entry_key>> ready_entry_order;
    QHash<cache_namespace, int> namespace_entry_limits;
    QHash<family_key, family_state> families;
    qint64 ready_bytes = 0;
    int ready_images = 0;
    int high_water_ready_entries = 0;
    qint64 high_water_ready_bytes = 0;
    int high_water_ready_images = 0;
    debug_delta_counters lifetime_deltas;
    debug_delta_counters interval_deltas;
    qint64 snapshot_sequence = 0;
    debug_timing_accumulator raster_timing_accumulator;
    debug_timing_accumulator coalesced_wait_accumulator;
    debug_deadline_counters deadline_counters;
    QHash<entry_key, int> request_counts;
    QHash<debug_task_timing_key, debug_task_timing_aggregate> task_timing;

    struct displayed_entry_observation {
        qint64 last_seen_ms = 0;
        quint32 consumer_mask = 0;
    };

    QHash<entry_key, displayed_entry_observation> displayed_entry_observations;

    static constexpr qint64 displayed_entry_window_ms = 2000;

    void enforce_namespace_limit(cache_namespace name_space);
    static qint64 estimate_result_bytes(const result& value);
    static int count_result_images(const result& value);
    void update_high_water_marks();
    static qint64 now_ms();
    static void
    add_timing_sample(debug_timing_accumulator& accumulator, qint64 value_ms);
    static qint64
    average_timing_ms(const debug_timing_accumulator& accumulator);
    static qint64 deadline_budget_ms_for_request(const request& req);
    static void add_deadline_sample(
        debug_deadline_counters& counters, qint64 elapsed_ms, qint64 budget_ms
    );
    void add_task_timing_sample(
        const entry_key& entry, debug_snapshot::timing_stage stage,
        qint64 value_ms
    );
    void emit_debug_snapshot();
};

size_t qHash(const raster_cache::entry_key& key, size_t seed = 0);
size_t qHash(const raster_cache::family_key& key, size_t seed = 0);
size_t qHash(const raster_cache::debug_task_timing_key& key, size_t seed = 0);

#endif // KCUCKOOUNTER_IMAGE_RASTER_CACHE_HPP
