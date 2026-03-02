#ifndef KCUCKOOUNTER_TABLE_CARD_WIDGET_HPP
#define KCUCKOOUNTER_TABLE_CARD_WIDGET_HPP

#include "arch/random_generator.hpp"
#include "arch/time_interface.hpp"
#include "arch/widget_helpers.hpp"
#include "card_helpers/card_picker.hpp"
#include "image/image_cacher.hpp"
#include <QFutureWatcher>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <QSvgRenderer>
#include <QVector>
#include <deque>
#include <memory>

class QPaintEvent;
class QResizeEvent;

class card_rasterize_watcher : public QFutureWatcher<QVector<QImage>> {
public:
    using QFutureWatcher<QVector<QImage>>::QFutureWatcher;
    void waitForFinished();
};

class card_widget : public BaseWidget {
    Q_OBJECT
    friend class card_widget_tests;

public:
    explicit card_widget(BaseWidget* parent = nullptr);
    ~card_widget() override;

    void set_swap_selected(bool selected);
    bool swap_selected() const;

    void
    start_quiz(int quiz_type_index, int decks_count, bool infinity_enabled);
    void set_infinity(bool enabled);
    void set_running(bool running);
    void set_slot_rotated(bool rotated);
    void set_show_card_indexing(bool enabled);
    void set_show_strategy_name(bool enabled);
    void set_training_mode(bool enabled);
    void set_strategy_name(const QString& name);
    void set_strategy_weights(const QVector<int>& weights);
    void set_table_marking_source(const QString& source);
    void set_hide_cards(bool hide);
    void advance_card();
    bool has_cards() const;
    bool has_current_card() const;
    bool is_deck_exhausted() const;
    void mark_deck_exhausted();
    int current_position() const;
    int current_total_weight() const;
    void clear_quiz();
    void trigger_highlight(int duration_ms);
    void tick_highlight(int delta_ms);
    void prepare_card_faces();
    int card_face_target_short_px() const;
    void set_shared_card_faces(
        const QVector<QImage>& face_images, const QSize& raster_size
    );
    void clear_shared_card_faces();
    bool has_shared_card_faces() const;
    void set_shared_card_faces_mode(bool enabled);
    void sync_card_sheet_source();

signals:
    void rasterization_busy_changed(bool busy);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool running;
    bool swap_selected_flag;
    card_picker picker;
    random_generator random_gen;
    qreal card_rotation_deg;
    QPointF card_offset;
    bool slot_rotated;
    bool show_card_indexing_flag;
    bool show_strategy_name_flag;
    bool training_mode_flag;
    QString strategy_name;
    QVector<int> strategy_weights;
    int cards_per_deck;
    int decks_count;
    bool infinity_enabled;
    std::unique_ptr<time_interface> selection_timer;
    qreal selection_phase;

    struct discard_card {
        qreal rotation_deg;
        QPointF offset;
    };

    std::deque<discard_card> discard_history;
    int highlight_duration_ms;
    int highlight_remaining_ms;
    bool highlight_active;
    bool hide_cards_flag;
    image_cacher table_marking;
    QString card_sheet_source;
    QSvgRenderer card_sheet_renderer;
    QVector<QPixmap> card_faces;
    QSize card_face_size;
    QVector<QPixmap> card_faces_rasterized;
    QSize card_face_raster_size;
    int picks_since_rasterize;
    card_rasterize_watcher rasterize_watcher;
    QSize raster_task_size;
    QString raster_task_source;
    QSize pending_raster_size;
    bool rasterizing;
    bool shared_card_faces_active;
    bool shared_card_faces_mode;

    void update_card_jitter();
    void update_table_marking();
    QSize card_face_target_size() const;
    QSize raster_cache_size(const QSize& target_size) const;
    void update_card_faces(const QSize& target_size);
    void record_discard();
    qreal highlight_strength() const;
    void update_selection_pulse();
    int total_weight_for_picks() const;
    void start_rasterization(const QSize& target_size);
    void apply_rasterized_images(
        const QVector<QImage>& images, const QSize& target_size
    );
    void set_rasterizing(bool active);
    void on_rasterization_finished();
};

#endif // KCUCKOOUNTER_TABLE_CARD_WIDGET_HPP
