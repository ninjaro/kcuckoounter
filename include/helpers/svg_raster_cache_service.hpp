#ifndef KCUCKOOUNTER_HELPERS_SVG_RASTER_CACHE_SERVICE_HPP
#define KCUCKOOUNTER_HELPERS_SVG_RASTER_CACHE_SERVICE_HPP

#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSize>
#include <QString>
#include <QVector>

#include <optional>

class svg_raster_cache_service : public QObject {
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
        entry_key key;
        QSize raster_size;
        int generation;
        qint64 timestamp_ms;
        int use_count;
        QImage single_image;
        QVector<QImage> face_images;

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

    explicit svg_raster_cache_service(QObject* parent = nullptr);

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

signals:
    void result_updated(const svg_raster_cache_service::entry_key& key);

private:
    static entry_key make_entry_key(const request& req);
    static family_key make_family_key(const request& req);

    struct family_state {
        bool in_flight = false;
        entry_key active_entry;
        bool has_pending = false;
        entry_key pending_entry;
    };

    QHash<entry_key, result> ready_results;
    QHash<cache_namespace, QQueue<entry_key>> ready_entry_order;
    QHash<cache_namespace, int> namespace_entry_limits;
    QHash<family_key, family_state> families;

    void enforce_namespace_limit(cache_namespace name_space);
};

size_t qHash(const svg_raster_cache_service::entry_key& key, size_t seed = 0);
size_t qHash(const svg_raster_cache_service::family_key& key, size_t seed = 0);

#endif // KCUCKOOUNTER_HELPERS_SVG_RASTER_CACHE_SERVICE_HPP
