#ifndef KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
#define KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <rasterflow/qt/image_service.hpp>
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
std::pair<int, int> card_sheet_ratio();
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
    bool metadata_published = false;
    QString selected_path;
    rasterflow::qt::source_handle source;
    std::pair<int, int> current_ratio { 88, 63 };
};

card_sheet_metadata& current_card_sheet_metadata();

#endif // KCUCKOOUNTER_CARD_HELPERS_CARD_SHEET_HPP
