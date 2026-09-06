#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <utility>

struct card_theme_option {
    QString label;
    QString source_path;
    bool installed = false;
};

struct card_sheet_fallback_resolution {
    int active_theme_keys = 0;
    int default_theme_keys = 0;
    int placeholder_keys = 0;
};

QString card_sheet_source_path();
QString default_card_sheet_source_path();
const QVector<card_theme_option>& available_card_themes();
void set_card_sheet_source_path(const QString& source_path);
bool preload_card_sheet();
std::pair<int, int> card_sheet_ratio();
QString card_label_from_index(int index);
QString card_element_id_from_index(int index);
const QStringList& card_element_ids();
QString card_back_element_id();
QStringList required_card_ids_with_back();

// Own, use and destroy on one thread. A context retains parsed sources and
// resolved element metadata, but never raster pixels. Current callers keep it
// local to one job; separate concurrent jobs use independent contexts.
class card_sheet_render_context final {
public:
    explicit card_sheet_render_context(
        const QString& preferred_source_path,
        const QString& fallback_source_path = default_card_sheet_source_path()
    );
    ~card_sheet_render_context();
    card_sheet_render_context(const card_sheet_render_context&) = delete;
    card_sheet_render_context& operator=(const card_sheet_render_context&)
        = delete;

    QImage rasterize_face(
        const QString& logical_element_id, const QSize& raster_size,
        card_sheet_fallback_resolution* resolution = nullptr
    );
    QVector<QImage> rasterize_faces(
        const QSize& raster_size,
        card_sheet_fallback_resolution* resolution = nullptr
    );
    card_sheet_fallback_resolution resolve_required_sources();

private:
    class implementation;
    std::unique_ptr<implementation> implementation_;
};

QImage rasterize_card_face_with_fallback(
    const QString& preferred_source_path, const QString& logical_element_id,
    const QSize& raster_size,
    card_sheet_fallback_resolution* resolution = nullptr
);
card_sheet_fallback_resolution
resolve_required_card_face_sources(const QString& preferred_source_path);
QVector<QImage> rasterize_card_faces_with_fallback(
    const QString& preferred_source_path, const QSize& raster_size,
    card_sheet_fallback_resolution* resolution = nullptr
);

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
