#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP

#include "image/image_sources.hpp"

#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <rasterflow/qt/image_service.hpp>
#include <utility>
#include <vector>

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
// Long:short of the painted face, i.e. after card_face_extent's trim.
std::pair<int, int> card_sheet_ratio();
// The face rastered for an element: a sheet's "base" frame trims art that
// overflows the card, per dimension. An empty base keeps the bounds.
QSizeF card_face_extent(const QSizeF& bounds, const QSizeF& base);
QString card_label_from_index(int index);
QString card_element_id_from_index(int index);
const QStringList& card_element_ids();
QString card_back_element_id();
QStringList required_card_ids_with_back();
QStringList candidate_card_element_ids(const QString& logical_element_id);

// Game-owned selection and geometry only. SVG metadata is prepared by the
// shared Rasterflow service; the initial base ratio remains usable while
// pending.
class card_sheet_metadata final : public QObject {
    Q_OBJECT
public:
    explicit card_sheet_metadata(QObject* parent);
    void refresh();
    [[nodiscard]] bool is_pending() const;
    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] std::pair<int, int> ratio() const;

signals:
    void geometry_changed();

private:
    void publish_metadata();
    rasterflow::qt::image_consumer* consumer;
    std::vector<rasterflow::qt::metadata_request> inspection_requests;
    QString pending_element;
    bool metadata_published = false;
    QString selected_path;
    image_source_request acquisition;
    std::pair<int, int> current_ratio { 88, 63 };
};

card_sheet_metadata& current_card_sheet_metadata();

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
