#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSize>
#include <utility>

struct card_sheet_fallback_resolution {
    int active_theme_keys = 0;
    int default_theme_keys = 0;
    int placeholder_keys = 0;
};

QString card_sheet_source_path();
QString default_card_sheet_source_path();
void set_card_sheet_source_path(const QString& source_path);
bool preload_card_sheet();
std::pair<int, int> card_sheet_ratio();
QString card_label_from_index(int index);
QString card_element_id_from_index(int index);
const QStringList& card_element_ids();
QString card_back_element_id();
QStringList required_card_element_ids_with_back();
card_sheet_fallback_resolution
resolve_required_card_face_sources(const QString& preferred_source_path);
QVector<QImage> rasterize_required_card_faces_with_fallback(
    const QString& preferred_source_path, const QSize& raster_size,
    card_sheet_fallback_resolution* resolution = nullptr
);

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
