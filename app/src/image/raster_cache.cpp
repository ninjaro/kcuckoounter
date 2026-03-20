#include "image/raster_cache.hpp"
#include "arch/num_helpers.hpp"

#include <QtGlobal>

#include <QDateTime>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <limits>

size_t raster_cache_combine_hash(size_t lhs, size_t rhs) {
    return lhs ^ (rhs + 0x9e3779b9 + (lhs << 6U) + (lhs >> 2U));
}

QString raster_cache::normalize_render_scope(const QString& raw_scope) {
    const QString simplified = raw_scope.simplified();
    if (!simplified.startsWith(QStringLiteral("subset:"))) {
        return simplified;
    }

    QStringList ids = simplified.sliced(QStringLiteral("subset:").size())
                          .split(',', Qt::SkipEmptyParts);
    for (QString& id : ids) {
        id = id.simplified();
    }

    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    return QStringLiteral("subset:%1").arg(ids.join(','));
}

qint64 raster_cache::estimate_widget_local_scaled_bytes(
    const raster_cache::entry_key& key, qint64 cache_accounted_bytes
) {
    if (key.kind != raster_cache::resource_kind::card_sheet_faces
        || cache_accounted_bytes <= 0) {
        return 0;
    }

    return static_cast<qint64>(
        static_cast<qreal>(cache_accounted_bytes)
        * widget_scaled_layer_ratio_estimate
    );
}

quint32
raster_cache::consumer_scope_bit(raster_cache::debug_consumer_scope consumer) {
    switch (consumer) {
    case raster_cache::debug_consumer_scope::table_slots:
        return 1U << 0U;
    case raster_cache::debug_consumer_scope::settings_theme_carousel:
        return 1U << 1U;
    case raster_cache::debug_consumer_scope::settings_strategy_preview:
        return 1U << 2U;
    case raster_cache::debug_consumer_scope::image_cacher:
        return 1U << 3U;
    case raster_cache::debug_consumer_scope::unknown:
    default:
        return 1U << 4U;
    }
}

QVector<raster_cache::debug_consumer_scope>
raster_cache::all_consumer_scopes() {
    return {
        raster_cache::debug_consumer_scope::table_slots,
        raster_cache::debug_consumer_scope::settings_theme_carousel,
        raster_cache::debug_consumer_scope::settings_strategy_preview,
        raster_cache::debug_consumer_scope::image_cacher,
        raster_cache::debug_consumer_scope::unknown,
    };
}

int raster_cache::subsystem_id_for(
    cache_namespace name_space, resource_kind kind
) {
    return (static_cast<int>(name_space) << 16) | static_cast<int>(kind);
}

bool raster_cache::compare_subsystem_summaries(
    const debug_snapshot::debug_subsystem_summary& lhs,
    const debug_snapshot::debug_subsystem_summary& rhs
) {
    if (lhs.ready_bytes != rhs.ready_bytes) {
        return lhs.ready_bytes > rhs.ready_bytes;
    }
    if (lhs.request_samples != rhs.request_samples) {
        return lhs.request_samples > rhs.request_samples;
    }
    if (lhs.timing_max_elapsed_ms != rhs.timing_max_elapsed_ms) {
        return lhs.timing_max_elapsed_ms > rhs.timing_max_elapsed_ms;
    }
    if (lhs.name_space != rhs.name_space) {
        return static_cast<int>(lhs.name_space)
            < static_cast<int>(rhs.name_space);
    }
    return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
}

bool raster_cache::compare_consumer_summaries(
    const debug_snapshot::debug_consumer_summary& lhs,
    const debug_snapshot::debug_consumer_summary& rhs
) {
    if (lhs.displayed_recent_widget_local_bytes_estimated
        != rhs.displayed_recent_widget_local_bytes_estimated) {
        return lhs.displayed_recent_widget_local_bytes_estimated
            > rhs.displayed_recent_widget_local_bytes_estimated;
    }
    if (lhs.displayed_recent_ready_bytes != rhs.displayed_recent_ready_bytes) {
        return lhs.displayed_recent_ready_bytes
            > rhs.displayed_recent_ready_bytes;
    }
    return static_cast<int>(lhs.consumer) < static_cast<int>(rhs.consumer);
}

bool raster_cache::compare_size_buckets(
    const debug_snapshot::debug_size_bucket& lhs,
    const debug_snapshot::debug_size_bucket& rhs
) {
    return lhs.target_bucket_px < rhs.target_bucket_px;
}

bool raster_cache::compare_largest_entries(
    const debug_snapshot::debug_largest_entry& lhs,
    const debug_snapshot::debug_largest_entry& rhs
) {
    return lhs.estimated_bytes > rhs.estimated_bytes;
}

bool raster_cache::compare_requested_entries(
    const debug_snapshot::debug_requested_entry& lhs,
    const debug_snapshot::debug_requested_entry& rhs
) {
    if (lhs.request_count != rhs.request_count) {
        return lhs.request_count > rhs.request_count;
    }
    return lhs.target_bucket_px < rhs.target_bucket_px;
}

bool raster_cache::compare_expensive_tasks(
    const debug_snapshot::debug_expensive_task& lhs,
    const debug_snapshot::debug_expensive_task& rhs
) {
    if (lhs.max_elapsed_ms != rhs.max_elapsed_ms) {
        return lhs.max_elapsed_ms > rhs.max_elapsed_ms;
    }
    return lhs.avg_elapsed_ms > rhs.avg_elapsed_ms;
}

qint64 raster_cache::estimate_result_bytes(const result& value) {
    qint64 bytes = static_cast<qint64>(value.single_image.sizeInBytes());
    for (const QImage& image : value.face_images) {
        bytes += static_cast<qint64>(image.sizeInBytes());
    }
    return bytes;
}

int raster_cache::count_result_images(const result& value) {
    return (value.single_image.isNull() ? 0 : 1)
        + num_helpers::to_int(value.face_images.size());
}

bool raster_cache::entry_key::operator==(const entry_key& other) const {
    return name_space == other.name_space && kind == other.kind
        && source_id == other.source_id && render_scope == other.render_scope
        && target_bucket_px == other.target_bucket_px;
}

bool raster_cache::family_key::operator==(const family_key& other) const {
    return name_space == other.name_space && kind == other.kind
        && source_id == other.source_id && render_scope == other.render_scope;
}

bool raster_cache::result::is_ready() const {
    return !single_image.isNull() || !face_images.isEmpty();
}

bool raster_cache::debug_task_timing_key::operator==(
    const debug_task_timing_key& other
) const {
    return stage == other.stage && entry == other.entry;
}

raster_cache::raster_cache(QObject* parent)
    : QObject(parent)
    , ready_results()
    , ready_entry_order()
    , namespace_entry_limits(
          {
              { cache_namespace::main, -1 },
              { cache_namespace::settings, 3 },
          }
      )
    , families()
    , ready_bytes(0)
    , ready_images(0)
    , widget_local_always_rasterized_bytes_estimated(0) { }

std::optional<raster_cache::result>
raster_cache::get_if_ready(const entry_key& key) const {
    const auto it = ready_results.constFind(key);
    if (it == ready_results.cend()) {
        return std::nullopt;
    }

    if (!it.value().is_ready()) {
        return std::nullopt;
    }

    return it.value();
}

std::optional<raster_cache::result>
raster_cache::get_if_ready_with_namespace_fallback(const entry_key& key) const {
    const std::optional<result> direct = get_if_ready(key);
    if (direct.has_value()) {
        return direct;
    }

    if (key.name_space != cache_namespace::settings) {
        return std::nullopt;
    }

    entry_key main_key = key;
    main_key.name_space = cache_namespace::main;
    return get_if_ready(main_key);
}

void raster_cache::insert_or_update_result(const result& new_result) {
    std::optional<debug_entry_accounting> old_accounting;
    const auto old_accounting_it
        = debug_entry_accounting_cache.constFind(new_result.key);
    if (old_accounting_it != debug_entry_accounting_cache.cend()) {
        old_accounting = old_accounting_it.value();
    }

    const auto existing = ready_results.constFind(new_result.key);
    if (existing != ready_results.cend()) {
        const debug_entry_accounting effective_old_accounting
            = old_accounting.has_value()
            ? *old_accounting
            : make_debug_entry_accounting(new_result.key, existing.value());
        const qint64 existing_bytes
            = effective_old_accounting.cache_accounted_bytes;
        const int existing_images = effective_old_accounting.image_count;
        ready_bytes -= existing_bytes;
        ready_images -= existing_images;
        lifetime_deltas.bytes_removed += existing_bytes;
        lifetime_deltas.images_removed += existing_images;
        interval_deltas.bytes_removed += existing_bytes;
        interval_deltas.images_removed += existing_images;
        apply_debug_entry_accounting_remove(
            new_result.key, effective_old_accounting
        );
    } else if (old_accounting.has_value()) {
        apply_debug_entry_accounting_remove(new_result.key, *old_accounting);
    }

    if (!ready_results.contains(new_result.key)) {
        ready_entry_order[new_result.key.name_space].enqueue(new_result.key);
        lifetime_deltas.entries_added += 1;
        interval_deltas.entries_added += 1;
    }

    ready_results.insert(new_result.key, new_result);
    const debug_entry_accounting new_accounting
        = make_debug_entry_accounting(new_result.key, new_result);
    debug_entry_accounting_cache.insert(new_result.key, new_accounting);
    apply_debug_entry_accounting_add(new_result.key, new_accounting);
    if (!new_result.is_ready()) {
        displayed_entry_observations.remove(new_result.key);
    }
    const qint64 added_bytes = new_accounting.cache_accounted_bytes;
    const int added_images = new_accounting.image_count;
    ready_bytes += added_bytes;
    ready_images += added_images;
    lifetime_deltas.bytes_added += added_bytes;
    lifetime_deltas.images_added += added_images;
    interval_deltas.bytes_added += added_bytes;
    interval_deltas.images_added += added_images;

    enforce_namespace_limit(new_result.key.name_space);
    update_high_water_marks();
    emit_debug_snapshot();
    emit result_updated(new_result.key);
}

raster_cache::submit_outcome raster_cache::submit_request(const request& req) {
    const entry_key entry = make_entry_key(req);
    const family_key family = make_family_key(req);
    request_counts[entry] += 1;

    const std::optional<result> ready = get_if_ready(entry);
    if (ready.has_value()) {
        add_task_timing_sample(
            entry, debug_snapshot::timing_stage::coalesced_wait, 0
        );
        emit_debug_snapshot();
        return submit_outcome {
            .state = request_state::cache_hit,
            .key = entry,
            .ready_result = ready,
        };
    }

    family_state& state = families[family];
    if (!state.in_flight) {
        const qint64 submitted_at = now_ms();
        state.in_flight = true;
        state.active_entry = entry;
        state.active_started_ms = submitted_at;
        state.active_deadline_budget_ms = deadline_budget_ms_for_request(req);

        if (state.has_pending) {
            add_timing_sample(
                coalesced_wait_accumulator,
                std::max<qint64>(0, submitted_at - state.pending_submitted_ms)
            );
            state.has_pending = false;
            state.pending_submitted_ms = 0;
            state.pending_deadline_budget_ms = 0;
        }

        emit_debug_snapshot();
        return submit_outcome {
            .state = request_state::start_async,
            .key = entry,
            .ready_result = std::nullopt,
        };
    }

    if (state.active_entry == entry) {
        emit_debug_snapshot();
        return submit_outcome {
            .state = request_state::already_in_flight,
            .key = entry,
            .ready_result = std::nullopt,
        };
    }

    state.has_pending = true;
    state.pending_entry = entry;
    state.pending_submitted_ms = now_ms();
    state.pending_deadline_budget_ms = deadline_budget_ms_for_request(req);
    emit_debug_snapshot();
    return submit_outcome {
        .state = request_state::pending_coalesced,
        .key = entry,
        .ready_result = std::nullopt,
    };
}

raster_cache::finish_outcome raster_cache::finish_active_request(
    const family_key& key, const entry_key& completed_entry
) {
    const auto it = families.find(key);
    if (it == families.end() || !it->in_flight) {
        return finish_outcome {
            .accepted_completion = false,
            .next_entry_to_start = std::nullopt,
        };
    }

    if (!(it->active_entry == completed_entry)) {
        return finish_outcome {
            .accepted_completion = false,
            .next_entry_to_start = std::nullopt,
        };
    }

    if (it->active_started_ms > 0) {
        const qint64 elapsed_ms
            = std::max<qint64>(0, now_ms() - it->active_started_ms);
        add_timing_sample(raster_timing_accumulator, elapsed_ms);
        add_deadline_sample(
            deadline_counters, elapsed_ms, it->active_deadline_budget_ms
        );
        add_task_timing_sample(
            completed_entry, debug_snapshot::timing_stage::raster_lifecycle,
            elapsed_ms
        );
    }

    it->in_flight = false;
    it->active_started_ms = 0;
    if (!it->has_pending) {
        families.erase(it);
        emit_debug_snapshot();
        return finish_outcome {
            .accepted_completion = true,
            .next_entry_to_start = std::nullopt,
        };
    }

    const qint64 promoted_at = now_ms();
    const qint64 coalesced_wait_ms
        = std::max<qint64>(0, promoted_at - it->pending_submitted_ms);
    add_timing_sample(coalesced_wait_accumulator, coalesced_wait_ms);
    add_task_timing_sample(
        it->pending_entry, debug_snapshot::timing_stage::coalesced_wait,
        coalesced_wait_ms
    );

    const entry_key next_entry = it->pending_entry;
    it->has_pending = false;
    it->pending_submitted_ms = 0;
    it->active_entry = next_entry;
    it->in_flight = true;
    it->active_started_ms = promoted_at;
    it->active_deadline_budget_ms = it->pending_deadline_budget_ms;
    it->pending_deadline_budget_ms = 0;
    emit_debug_snapshot();
    return finish_outcome {
        .accepted_completion = true,
        .next_entry_to_start = next_entry,
    };
}

bool raster_cache::is_in_flight(const family_key& key) const {
    const auto it = families.constFind(key);
    if (it == families.cend()) {
        return false;
    }

    return it.value().in_flight;
}

void raster_cache::mark_in_flight(
    const family_key& key, const entry_key& active_key
) {
    family_state& state = families[key];
    const qint64 marked_at = now_ms();
    state.in_flight = true;
    state.active_entry = active_key;
    state.active_started_ms = marked_at;
    state.active_deadline_budget_ms = 0;

    if (state.has_pending) {
        const qint64 coalesced_wait_ms
            = std::max<qint64>(0, marked_at - state.pending_submitted_ms);
        add_timing_sample(coalesced_wait_accumulator, coalesced_wait_ms);
        add_task_timing_sample(
            state.pending_entry, debug_snapshot::timing_stage::coalesced_wait,
            coalesced_wait_ms
        );
        state.has_pending = false;
        state.pending_submitted_ms = 0;
        state.pending_deadline_budget_ms = 0;
    }

    emit_debug_snapshot();
}

void raster_cache::clear_in_flight(const family_key& key) {
    const auto it = families.find(key);
    if (it == families.end()) {
        return;
    }

    it->in_flight = false;
    it->active_started_ms = 0;
    it->active_deadline_budget_ms = 0;
    if (!it->has_pending) {
        families.erase(it);
    }

    emit_debug_snapshot();
}

void raster_cache::set_pending_latest(
    const family_key& key, const entry_key& pending_key
) {
    family_state& state = families[key];
    state.has_pending = true;
    state.pending_entry = pending_key;
    state.pending_submitted_ms = now_ms();
    state.pending_deadline_budget_ms = 0;
    emit_debug_snapshot();
}

std::optional<raster_cache::entry_key>
raster_cache::take_pending_latest(const family_key& key) {
    const auto it = families.find(key);
    if (it == families.end() || !it->has_pending) {
        return std::nullopt;
    }

    const entry_key pending = it->pending_entry;
    const qint64 coalesced_wait_ms
        = std::max<qint64>(0, now_ms() - it->pending_submitted_ms);
    add_timing_sample(coalesced_wait_accumulator, coalesced_wait_ms);
    add_task_timing_sample(
        pending, debug_snapshot::timing_stage::coalesced_wait, coalesced_wait_ms
    );
    it->has_pending = false;
    it->pending_submitted_ms = 0;
    it->pending_deadline_budget_ms = 0;
    if (!it->in_flight) {
        families.erase(it);
    }

    emit_debug_snapshot();

    return pending;
}

int raster_cache::ready_entry_count() const {
    return static_cast<int>(ready_results.size());
}

int raster_cache::ready_entry_count(cache_namespace name_space) const {
    int count = 0;
    for (auto it = ready_results.cbegin(); it != ready_results.cend(); ++it) {
        if (it.key().name_space == name_space) {
            ++count;
        }
    }
    return count;
}

raster_cache::entry_key raster_cache::make_entry_key(const request& req) {
    return entry_key {
        .name_space = req.name_space,
        .kind = req.kind,
        .source_id = req.source_id,
        .render_scope = normalize_render_scope(req.render_scope),
        .target_bucket_px = req.target_bucket_px,
    };
}

raster_cache::family_key raster_cache::make_family_key(const request& req) {
    return family_key {
        .name_space = req.name_space,
        .kind = req.kind,
        .source_id = req.source_id,
        .render_scope = normalize_render_scope(req.render_scope),
    };
}

int raster_cache::in_flight_count() const {
    int count = 0;
    for (auto it = families.cbegin(); it != families.cend(); ++it) {
        if (it->in_flight) {
            ++count;
        }
    }
    return count;
}

void raster_cache::note_entry_displayed(
    const entry_key& key, debug_consumer_scope consumer
) {
    const auto it = ready_results.constFind(key);
    if (it == ready_results.cend() || !it.value().is_ready()) {
        return;
    }

    displayed_entry_observation& observation
        = displayed_entry_observations[key];
    observation.last_seen_ms = now_ms();
    observation.consumer_mask |= consumer_scope_bit(consumer);
    emit_debug_snapshot();
}

void raster_cache::note_entry_no_longer_displayed(
    const entry_key& key, debug_consumer_scope consumer
) {
    auto it = displayed_entry_observations.find(key);
    if (it == displayed_entry_observations.end()) {
        return;
    }

    const quint32 bit = consumer_scope_bit(consumer);
    if ((it->consumer_mask & bit) == 0U) {
        return;
    }

    it->consumer_mask &= ~bit;
    if (it->consumer_mask == 0U) {
        displayed_entry_observations.erase(it);
    }
    emit_debug_snapshot();
}

raster_cache::debug_snapshot raster_cache::get_debug_snapshot() const {
    struct subsystem_rollup {
        cache_namespace name_space = cache_namespace::main;
        resource_kind kind = resource_kind::single_svg;
        int ready_entries = 0;
        qint64 ready_bytes = 0;
        int request_samples = 0;
        int timing_samples = 0;
        qint64 timing_max_elapsed_ms = 0;
    };

    struct consumer_rollup {
        debug_consumer_scope consumer = debug_consumer_scope::unknown;
        int displayed_recent_entries = 0;
        int displayed_recent_images = 0;
        qint64 displayed_recent_ready_bytes = 0;
        qint64 displayed_recent_widget_local_bytes_estimated = 0;
    };

    QHash<int, debug_snapshot::debug_size_bucket> size_bucket_map;
    QHash<int, subsystem_rollup> subsystem_rollups;
    QHash<quint32, consumer_rollup> consumer_rollups;
    QVector<debug_snapshot::debug_largest_entry> largest_entries;
    QVector<debug_snapshot::debug_requested_entry> top_requested_entries;
    QVector<debug_snapshot::debug_expensive_task> top_expensive_tasks;
    QVector<debug_snapshot::debug_subsystem_summary> subsystem_summaries;
    QVector<debug_snapshot::debug_consumer_summary> consumer_summaries;
    largest_entries.reserve(3);

    int displayed_ready_entries = 0;
    int cached_only_ready_entries = 0;
    int displayed_ready_images = 0;
    int cached_only_ready_images = 0;
    qint64 widget_local_rasterized_bytes_estimated
        = widget_local_always_rasterized_bytes_estimated;
    qint64 widget_local_scaled_bytes_estimated = 0;
    int fallback_active_theme_keys_ready = 0;
    int fallback_default_theme_keys_ready = 0;
    int fallback_placeholder_keys_ready = 0;

    const qint64 snapshot_now_ms = now_ms();

    for (auto it = ready_results.cbegin(); it != ready_results.cend(); ++it) {
        const debug_entry_accounting accounting
            = debug_entry_accounting_cache.value(
                it.key(), make_debug_entry_accounting(it.key(), it.value())
            );
        const qint64 result_bytes = accounting.cache_accounted_bytes;
        const int bucket_px = it.key().target_bucket_px;
        const int result_images = accounting.image_count;
        const int subsystem_id
            = subsystem_id_for(it.key().name_space, it.key().kind);
        fallback_active_theme_keys_ready
            += it.value().fallback_usage.active_theme_keys;
        fallback_default_theme_keys_ready
            += it.value().fallback_usage.default_theme_keys;
        fallback_placeholder_keys_ready
            += it.value().fallback_usage.placeholder_keys;

        subsystem_rollup& subsystem = subsystem_rollups[subsystem_id];
        subsystem.name_space = it.key().name_space;
        subsystem.kind = it.key().kind;
        subsystem.ready_entries += 1;
        subsystem.ready_bytes += result_bytes;

        debug_snapshot::debug_size_bucket& bucket = size_bucket_map[bucket_px];
        bucket.target_bucket_px = bucket_px;
        bucket.entry_count += 1;
        bucket.total_bytes += result_bytes;

        largest_entries.push_back(
            debug_snapshot::debug_largest_entry {
                .name_space = it.key().name_space,
                .kind = it.key().kind,
                .source_id = it.key().source_id,
                .render_scope = it.key().render_scope,
                .target_bucket_px = bucket_px,
                .estimated_bytes = result_bytes,
            }
        );

        const displayed_entry_observation observation
            = displayed_entry_observations.value(it.key());
        const qint64 displayed_at_ms = observation.last_seen_ms;
        qint64 widget_local_estimated_for_entry
            = it.key().kind == resource_kind::single_svg
            ? accounting.widget_local_display_bytes_estimated
            : 0;

        if (displayed_at_ms > 0
            && (snapshot_now_ms - displayed_at_ms)
                <= displayed_entry_window_ms) {
            displayed_ready_entries += 1;
            displayed_ready_images += result_images;
            if (it.key().kind == resource_kind::card_sheet_faces) {
                widget_local_rasterized_bytes_estimated
                    += accounting.widget_local_rasterized_bytes_estimated;
                widget_local_scaled_bytes_estimated
                    += accounting.widget_local_scaled_bytes_estimated;
                widget_local_estimated_for_entry
                    = accounting.widget_local_display_bytes_estimated;
            }

            const quint32 consumers = observation.consumer_mask == 0
                ? consumer_scope_bit(debug_consumer_scope::unknown)
                : observation.consumer_mask;
            for (debug_consumer_scope scope : all_consumer_scopes()) {
                const quint32 bit = consumer_scope_bit(scope);
                if ((consumers & bit) == 0U) {
                    continue;
                }

                consumer_rollup& rollup = consumer_rollups[bit];
                rollup.consumer = scope;
                rollup.displayed_recent_entries += 1;
                rollup.displayed_recent_images += result_images;
                rollup.displayed_recent_ready_bytes += result_bytes;
                rollup.displayed_recent_widget_local_bytes_estimated
                    += widget_local_estimated_for_entry;
            }
        } else {
            cached_only_ready_entries += 1;
            cached_only_ready_images += result_images;
        }
    }

    for (auto it = request_counts.cbegin(); it != request_counts.cend(); ++it) {
        const int subsystem_id
            = subsystem_id_for(it.key().name_space, it.key().kind);
        subsystem_rollup& subsystem = subsystem_rollups[subsystem_id];
        subsystem.name_space = it.key().name_space;
        subsystem.kind = it.key().kind;
        subsystem.request_samples += it.value();

        top_requested_entries.push_back(
            debug_snapshot::debug_requested_entry {
                .name_space = it.key().name_space,
                .kind = it.key().kind,
                .source_id = it.key().source_id,
                .render_scope = it.key().render_scope,
                .target_bucket_px = it.key().target_bucket_px,
                .request_count = it.value(),
            }
        );
    }

    for (auto it = task_timing.cbegin(); it != task_timing.cend(); ++it) {
        const debug_task_timing_aggregate& aggregate = it.value();
        const qint64 average_elapsed_ms = aggregate.completed_samples > 0
            ? (aggregate.total_elapsed_ms / aggregate.completed_samples)
            : 0;
        const int subsystem_id
            = subsystem_id_for(it.key().entry.name_space, it.key().entry.kind);
        subsystem_rollup& subsystem = subsystem_rollups[subsystem_id];
        subsystem.name_space = it.key().entry.name_space;
        subsystem.kind = it.key().entry.kind;
        subsystem.timing_samples += aggregate.completed_samples;
        subsystem.timing_max_elapsed_ms = std::max(
            subsystem.timing_max_elapsed_ms, aggregate.max_elapsed_ms
        );

        top_expensive_tasks.push_back(
            debug_snapshot::debug_expensive_task {
                .stage = it.key().stage,
                .name_space = it.key().entry.name_space,
                .kind = it.key().entry.kind,
                .source_id = it.key().entry.source_id,
                .render_scope = it.key().entry.render_scope,
                .target_bucket_px = it.key().entry.target_bucket_px,
                .completed_samples = aggregate.completed_samples,
                .avg_elapsed_ms = average_elapsed_ms,
                .max_elapsed_ms = aggregate.max_elapsed_ms,
            }
        );
    }

    subsystem_summaries.reserve(subsystem_rollups.size());
    for (auto it = subsystem_rollups.cbegin(); it != subsystem_rollups.cend();
         ++it) {
        subsystem_summaries.push_back(
            debug_snapshot::debug_subsystem_summary {
                .name_space = it.value().name_space,
                .kind = it.value().kind,
                .ready_entries = it.value().ready_entries,
                .ready_bytes = it.value().ready_bytes,
                .request_samples = it.value().request_samples,
                .timing_samples = it.value().timing_samples,
                .timing_max_elapsed_ms = it.value().timing_max_elapsed_ms,
            }
        );
    }

    std::sort(
        subsystem_summaries.begin(), subsystem_summaries.end(),
        compare_subsystem_summaries
    );

    consumer_summaries.reserve(consumer_rollups.size());
    for (auto it = consumer_rollups.cbegin(); it != consumer_rollups.cend();
         ++it) {
        consumer_summaries.push_back(
            debug_snapshot::debug_consumer_summary {
                .consumer = it.value().consumer,
                .displayed_recent_entries = it.value().displayed_recent_entries,
                .displayed_recent_images = it.value().displayed_recent_images,
                .displayed_recent_ready_bytes
                = it.value().displayed_recent_ready_bytes,
                .displayed_recent_widget_local_bytes_estimated
                = it.value().displayed_recent_widget_local_bytes_estimated,
            }
        );
    }

    std::sort(
        consumer_summaries.begin(), consumer_summaries.end(),
        compare_consumer_summaries
    );

    QVector<debug_snapshot::debug_size_bucket> size_buckets
        = size_bucket_map.values();
    std::sort(size_buckets.begin(), size_buckets.end(), compare_size_buckets);

    std::sort(
        largest_entries.begin(), largest_entries.end(), compare_largest_entries
    );

    if (largest_entries.size() > 3) {
        largest_entries.resize(3);
    }

    std::sort(
        top_requested_entries.begin(), top_requested_entries.end(),
        compare_requested_entries
    );

    if (top_requested_entries.size() > 3) {
        top_requested_entries.resize(3);
    }

    std::sort(
        top_expensive_tasks.begin(), top_expensive_tasks.end(),
        compare_expensive_tasks
    );

    if (top_expensive_tasks.size() > 3) {
        top_expensive_tasks.resize(3);
    }

    int pending_count = 0;
    for (auto it = families.cbegin(); it != families.cend(); ++it) {
        if (it->has_pending) {
            ++pending_count;
        }
    }

    const int ready_entries_now = ready_entry_count();
    const int displayed_entry_coverage_percent = ready_entries_now > 0
        ? ((displayed_ready_entries * 100) / ready_entries_now)
        : 0;

    return debug_snapshot {
        .snapshot_sequence = snapshot_sequence,
        .timestamp_ms = now_ms(),
        .ready_entries = ready_entries_now,
        .ready_bytes = ready_bytes,
        .ready_images = ready_images,
        .in_flight_families = in_flight_count(),
        .pending_families = pending_count,
        .displayed_ready_entries = displayed_ready_entries,
        .cached_only_ready_entries = cached_only_ready_entries,
        .displayed_ready_images = displayed_ready_images,
        .cached_only_ready_images = cached_only_ready_images,
        .widget_local_rasterized_bytes_estimated
        = widget_local_rasterized_bytes_estimated,
        .widget_local_scaled_bytes_estimated
        = widget_local_scaled_bytes_estimated,
        .widget_local_display_bytes_estimated
        = widget_local_rasterized_bytes_estimated
            + widget_local_scaled_bytes_estimated,
        .fallback_active_theme_keys_ready = fallback_active_theme_keys_ready,
        .fallback_default_theme_keys_ready = fallback_default_theme_keys_ready,
        .fallback_placeholder_keys_ready = fallback_placeholder_keys_ready,
        .displayed_entry_window_ms = displayed_entry_window_ms,
        .displayed_entry_coverage_percent = displayed_entry_coverage_percent,
        .high_water_ready_entries = high_water_ready_entries,
        .high_water_ready_bytes = high_water_ready_bytes,
        .high_water_ready_images = high_water_ready_images,
        .lifetime_deltas = lifetime_deltas,
        .interval_deltas = interval_deltas,
        .raster_timing_samples = raster_timing_accumulator.samples,
        .raster_timing_avg_ms = average_timing_ms(raster_timing_accumulator),
        .raster_timing_max_ms = raster_timing_accumulator.max_ms,
        .coalesced_wait_samples = coalesced_wait_accumulator.samples,
        .coalesced_wait_avg_ms = average_timing_ms(coalesced_wait_accumulator),
        .coalesced_wait_max_ms = coalesced_wait_accumulator.max_ms,
        .deadline_readiness_samples = deadline_counters.samples,
        .deadline_ready_early = deadline_counters.ready_early,
        .deadline_ready_on_time = deadline_counters.ready_on_time,
        .deadline_ready_late = deadline_counters.ready_late,
        .unique_size_buckets = num_helpers::to_int(size_buckets.size()),
        .size_buckets = size_buckets,
        .largest_entries = largest_entries,
        .top_requested_entries = top_requested_entries,
        .top_expensive_tasks = top_expensive_tasks,
        .subsystem_summaries = subsystem_summaries,
        .consumer_summaries = consumer_summaries,
    };
}

raster_cache::debug_delta_counters raster_cache::take_interval_deltas() {
    const debug_delta_counters current = interval_deltas;
    interval_deltas = {};
    return current;
}

void raster_cache::set_namespace_entry_limit(
    cache_namespace name_space, int limit
) {
    namespace_entry_limits.insert(name_space, limit);
    enforce_namespace_limit(name_space);
}

bool raster_cache::erase_result(const entry_key& key) {
    const auto it = ready_results.constFind(key);
    if (it == ready_results.cend()) {
        return false;
    }

    const auto accounting_it = debug_entry_accounting_cache.constFind(key);
    const debug_entry_accounting accounting
        = accounting_it != debug_entry_accounting_cache.cend()
        ? accounting_it.value()
        : make_debug_entry_accounting(key, it.value());
    const qint64 removed_bytes = accounting.cache_accounted_bytes;
    const int removed_images = accounting.image_count;
    ready_bytes -= removed_bytes;
    ready_images -= removed_images;
    lifetime_deltas.entries_removed += 1;
    lifetime_deltas.bytes_removed += removed_bytes;
    lifetime_deltas.images_removed += removed_images;
    interval_deltas.entries_removed += 1;
    interval_deltas.bytes_removed += removed_bytes;
    interval_deltas.images_removed += removed_images;
    ready_results.remove(key);
    displayed_entry_observations.remove(key);
    debug_entry_accounting_cache.remove(key);
    apply_debug_entry_accounting_remove(key, accounting);
    emit_debug_snapshot();
    return true;
}

void raster_cache::enforce_namespace_limit(cache_namespace name_space) {
    const int limit = namespace_entry_limits.value(name_space, -1);
    if (limit < 0) {
        return;
    }

    QQueue<entry_key>& queue = ready_entry_order[name_space];
    while (ready_entry_count(name_space) > limit && !queue.isEmpty()) {
        const entry_key oldest = queue.dequeue();
        const auto old_it = ready_results.constFind(oldest);
        if (old_it == ready_results.cend()) {
            continue;
        }

        const auto accounting_it
            = debug_entry_accounting_cache.constFind(oldest);
        const debug_entry_accounting accounting
            = accounting_it != debug_entry_accounting_cache.cend()
            ? accounting_it.value()
            : make_debug_entry_accounting(oldest, old_it.value());
        const qint64 removed_bytes = accounting.cache_accounted_bytes;
        const int removed_images = accounting.image_count;
        ready_bytes -= removed_bytes;
        ready_images -= removed_images;
        lifetime_deltas.entries_removed += 1;
        lifetime_deltas.bytes_removed += removed_bytes;
        lifetime_deltas.images_removed += removed_images;
        interval_deltas.entries_removed += 1;
        interval_deltas.bytes_removed += removed_bytes;
        interval_deltas.images_removed += removed_images;
        ready_results.remove(oldest);
        displayed_entry_observations.remove(oldest);
        debug_entry_accounting_cache.remove(oldest);
        apply_debug_entry_accounting_remove(oldest, accounting);
    }
}

void raster_cache::update_high_water_marks() {
    const int entries_now = ready_entry_count();
    high_water_ready_entries = std::max(high_water_ready_entries, entries_now);
    high_water_ready_bytes = std::max(high_water_ready_bytes, ready_bytes);
    high_water_ready_images = std::max(high_water_ready_images, ready_images);
}

qint64 raster_cache::now_ms() { return QDateTime::currentMSecsSinceEpoch(); }

raster_cache::debug_entry_accounting raster_cache::make_debug_entry_accounting(
    const entry_key& key, const result& value
) {
    const qint64 cache_accounted_bytes = estimate_result_bytes(value);
    const int image_count = count_result_images(value);

    qint64 widget_local_rasterized_bytes_estimated = 0;
    qint64 widget_local_scaled_bytes_estimated = 0;
    if (key.kind == resource_kind::single_svg) {
        widget_local_rasterized_bytes_estimated = cache_accounted_bytes;
    } else if (key.kind == resource_kind::card_sheet_faces) {
        widget_local_rasterized_bytes_estimated = cache_accounted_bytes;
        widget_local_scaled_bytes_estimated
            = estimate_widget_local_scaled_bytes(key, cache_accounted_bytes);
    }

    return debug_entry_accounting {
        .cache_accounted_bytes = cache_accounted_bytes,
        .image_count = image_count,
        .widget_local_rasterized_bytes_estimated
        = widget_local_rasterized_bytes_estimated,
        .widget_local_scaled_bytes_estimated
        = widget_local_scaled_bytes_estimated,
        .widget_local_display_bytes_estimated
        = widget_local_rasterized_bytes_estimated
            + widget_local_scaled_bytes_estimated,
    };
}

void raster_cache::apply_debug_entry_accounting_add(
    const entry_key& key, const debug_entry_accounting& accounting
) {
    if (key.kind != resource_kind::single_svg) {
        return;
    }

    widget_local_always_rasterized_bytes_estimated
        += accounting.widget_local_rasterized_bytes_estimated;
}

void raster_cache::apply_debug_entry_accounting_remove(
    const entry_key& key, const debug_entry_accounting& accounting
) {
    if (key.kind != resource_kind::single_svg) {
        return;
    }

    widget_local_always_rasterized_bytes_estimated = std::max<qint64>(
        0,
        widget_local_always_rasterized_bytes_estimated
            - accounting.widget_local_rasterized_bytes_estimated
    );
}

void raster_cache::add_timing_sample(
    debug_timing_accumulator& accumulator, qint64 value_ms
) {
    accumulator.samples += 1;
    accumulator.total_ms += value_ms;
    accumulator.max_ms = std::max(accumulator.max_ms, value_ms);
}

qint64
raster_cache::average_timing_ms(const debug_timing_accumulator& accumulator) {
    if (accumulator.samples <= 0) {
        return 0;
    }

    return accumulator.total_ms / accumulator.samples;
}

qint64 raster_cache::deadline_budget_ms_for_request(const request& req) {
    if (req.interactive) {
        return 120;
    }

    if (req.preview) {
        return 220;
    }

    return 400;
}

void raster_cache::add_deadline_sample(
    debug_deadline_counters& counters, qint64 elapsed_ms, qint64 budget_ms
) {
    if (budget_ms <= 0) {
        return;
    }

    counters.samples += 1;
    if (elapsed_ms <= (budget_ms / 2)) {
        counters.ready_early += 1;
        return;
    }

    if (elapsed_ms <= budget_ms) {
        counters.ready_on_time += 1;
        return;
    }

    counters.ready_late += 1;
}

void raster_cache::add_task_timing_sample(
    const entry_key& entry, debug_snapshot::timing_stage stage, qint64 value_ms
) {
    debug_task_timing_aggregate& aggregate = task_timing[debug_task_timing_key {
        .stage = stage,
        .entry = entry,
    }];
    aggregate.completed_samples += 1;
    aggregate.total_elapsed_ms += value_ms;
    aggregate.max_elapsed_ms = std::max(aggregate.max_elapsed_ms, value_ms);
}

void raster_cache::emit_debug_snapshot() {
    ++snapshot_sequence;
    emit debug_snapshot_updated(get_debug_snapshot());
}

size_t qHash(const raster_cache::entry_key& key, size_t seed) {
    size_t value = seed;
    value = raster_cache_combine_hash(
        value, qHash(static_cast<int>(key.name_space))
    );
    value = raster_cache_combine_hash(value, qHash(static_cast<int>(key.kind)));
    value = raster_cache_combine_hash(value, qHash(key.source_id));
    value = raster_cache_combine_hash(value, qHash(key.render_scope));
    value = raster_cache_combine_hash(value, qHash(key.target_bucket_px));
    return value;
}

size_t qHash(const raster_cache::family_key& key, size_t seed) {
    size_t value = seed;
    value = raster_cache_combine_hash(
        value, qHash(static_cast<int>(key.name_space))
    );
    value = raster_cache_combine_hash(value, qHash(static_cast<int>(key.kind)));
    value = raster_cache_combine_hash(value, qHash(key.source_id));
    value = raster_cache_combine_hash(value, qHash(key.render_scope));
    return value;
}

size_t qHash(const raster_cache::debug_task_timing_key& key, size_t seed) {
    size_t value = seed;
    value
        = raster_cache_combine_hash(value, qHash(static_cast<int>(key.stage)));
    value = raster_cache_combine_hash(value, qHash(key.entry));
    return value;
}
