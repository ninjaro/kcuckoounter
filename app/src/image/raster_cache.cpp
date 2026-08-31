#include "image/raster_cache.hpp"

#include <QStringList>

#include <algorithm>

size_t raster_cache_combine_hash(size_t lhs, size_t rhs) {
    return lhs ^ (rhs + 0x9e3779b9U + (lhs << 6U) + (lhs >> 2U));
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

raster_cache::raster_cache(QObject* parent)
    : QObject(parent)
    , namespace_entry_limits(
          {
              { cache_namespace::main, -1 },
              { cache_namespace::settings, 3 },
          }
      ) { }

std::optional<raster_cache::result>
raster_cache::get_if_ready(const entry_key& key) const {
    const auto it = ready_results.constFind(key);
    if (it == ready_results.cend() || !it.value().is_ready()) {
        return std::nullopt;
    }
    return it.value();
}

std::optional<raster_cache::result>
raster_cache::get_ready_with_namespace_fallback(const entry_key& key) const {
    if (std::optional<result> direct = get_if_ready(key); direct.has_value()) {
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
    if (!ready_results.contains(new_result.key)) {
        ready_entry_order[new_result.key.name_space].enqueue(new_result.key);
    }
    ready_results.insert(new_result.key, new_result);
    enforce_namespace_limit(new_result.key.name_space);
    emit result_updated(new_result.key);
}

raster_cache::submit_outcome raster_cache::submit_request(const request& req) {
    const entry_key entry = make_entry_key(req);
    if (std::optional<result> ready = get_if_ready(entry); ready.has_value()) {
        return submit_outcome {
            .state = request_state::cache_hit,
            .key = entry,
            .ready_result = std::move(ready),
        };
    }

    const family_key family = make_family_key(req);
    family_state& state = families[family];
    if (!state.in_flight) {
        state.in_flight = true;
        state.active_entry = entry;
        state.has_pending = false;
        return submit_outcome {
            .state = request_state::start_async,
            .key = entry,
            .ready_result = std::nullopt,
        };
    }
    if (state.active_entry == entry) {
        return submit_outcome {
            .state = request_state::already_in_flight,
            .key = entry,
            .ready_result = std::nullopt,
        };
    }

    state.has_pending = true;
    state.pending_entry = entry;
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
    if (it == families.end() || !it->in_flight
        || !(it->active_entry == completed_entry)) {
        return {};
    }

    if (!it->has_pending) {
        families.erase(it);
        return finish_outcome {
            .accepted_completion = true,
            .next_entry_to_start = std::nullopt,
        };
    }

    const entry_key next = it->pending_entry;
    it->active_entry = next;
    it->has_pending = false;
    return finish_outcome {
        .accepted_completion = true,
        .next_entry_to_start = next,
    };
}

bool raster_cache::is_in_flight(const family_key& key) const {
    const auto it = families.constFind(key);
    return it != families.cend() && it->in_flight;
}

void raster_cache::mark_in_flight(
    const family_key& key, const entry_key& active_key
) {
    family_state& state = families[key];
    state.in_flight = true;
    state.active_entry = active_key;
    state.has_pending = false;
}

void raster_cache::clear_in_flight(const family_key& key) {
    const auto it = families.find(key);
    if (it == families.end()) {
        return;
    }
    it->in_flight = false;
    if (!it->has_pending) {
        families.erase(it);
    }
}

void raster_cache::set_pending_latest(
    const family_key& key, const entry_key& pending_key
) {
    family_state& state = families[key];
    state.has_pending = true;
    state.pending_entry = pending_key;
}

std::optional<raster_cache::entry_key>
raster_cache::take_pending_latest(const family_key& key) {
    const auto it = families.find(key);
    if (it == families.end() || !it->has_pending) {
        return std::nullopt;
    }
    const entry_key pending = it->pending_entry;
    it->has_pending = false;
    if (!it->in_flight) {
        families.erase(it);
    }
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

int raster_cache::in_flight_count() const {
    int count = 0;
    for (auto it = families.cbegin(); it != families.cend(); ++it) {
        if (it->in_flight) {
            ++count;
        }
    }
    return count;
}

void raster_cache::set_namespace_entry_limit(
    cache_namespace name_space, int limit
) {
    namespace_entry_limits.insert(name_space, limit);
    enforce_namespace_limit(name_space);
}

bool raster_cache::erase_result(const entry_key& key) {
    const bool removed = ready_results.remove(key) > 0;
    auto order = ready_entry_order.find(key.name_space);
    if (order != ready_entry_order.end()) {
        order->removeAll(key);
    }
    return removed;
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

void raster_cache::enforce_namespace_limit(cache_namespace name_space) {
    const int limit = namespace_entry_limits.value(name_space, -1);
    if (limit < 0) {
        return;
    }

    QQueue<entry_key>& order = ready_entry_order[name_space];
    while (ready_entry_count(name_space) > limit && !order.isEmpty()) {
        ready_results.remove(order.dequeue());
    }
}

size_t qHash(const raster_cache::entry_key& key, size_t seed) {
    size_t value = seed;
    value = raster_cache_combine_hash(
        value, qHash(static_cast<int>(key.name_space))
    );
    value = raster_cache_combine_hash(value, qHash(static_cast<int>(key.kind)));
    value = raster_cache_combine_hash(value, qHash(key.source_id));
    value = raster_cache_combine_hash(value, qHash(key.render_scope));
    return raster_cache_combine_hash(value, qHash(key.target_bucket_px));
}

size_t qHash(const raster_cache::family_key& key, size_t seed) {
    size_t value = seed;
    value = raster_cache_combine_hash(
        value, qHash(static_cast<int>(key.name_space))
    );
    value = raster_cache_combine_hash(value, qHash(static_cast<int>(key.kind)));
    value = raster_cache_combine_hash(value, qHash(key.source_id));
    return raster_cache_combine_hash(value, qHash(key.render_scope));
}
