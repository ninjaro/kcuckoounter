#ifndef KCUCKOOUNTER_IMAGE_CARD_IMAGES_HPP
#define KCUCKOOUNTER_IMAGE_CARD_IMAGES_HPP

#include "card_helpers/card_sheet.hpp"
#include "image/image_sources.hpp"
#include <QHash>

struct card_image_demand {
    QString logical_id;
    QSize physical_size;
    bool required = true;
    int priority = 0;
    std::optional<std::int64_t> deadline_us = std::nullopt;
    bool operator==(const card_image_demand&) const = default;
};

// Game-specific mapping and presentation acceptance. No renderer, worker queue
// or pixel cache: every image is borrowed from the application Rasterflow
// store.
class card_image_consumer final : public QObject {
    Q_OBJECT
public:
    explicit card_image_consumer(QObject* parent = nullptr);
    void set_source(
        const QString& preferred,
        const QString& fallback = default_card_sheet_source_path()
    );
    void replace_desired(std::vector<card_image_demand> desired);
    void clear();
    [[nodiscard]] QImage image_for(const QString& logical_id) const;
    [[nodiscard]] bool is_pending() const;
    [[nodiscard]] bool is_pending_required() const;
    [[nodiscard]] bool preferred_source_failed() const;
    [[nodiscard]] card_sheet_fallback_resolution source_resolution() const;

signals:
    void images_changed();

private:
    rasterflow::qt::image_consumer* consumer;
    QString preferred_path;
    QString fallback_path;
    image_source_request preferred_source;
    image_source_request fallback_source;
    bool preferred_loaded = false;
    bool fallback_loaded = false;
    bool unresolved = false;
    bool unresolved_required = false;
    std::vector<card_image_demand> demands;
    QHash<QString, rasterflow::qt::image_request> requests;
    std::vector<rasterflow::qt::metadata_request> submitted_inspections;
    card_sheet_fallback_resolution resolution;
    void resolve_requests(bool retry_failed = false);
};

#endif
