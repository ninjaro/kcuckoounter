#ifndef KCUCKOOUNTER_TABLE_CARD_WIDGET_HPP
#define KCUCKOOUNTER_TABLE_CARD_WIDGET_HPP

#include "arch/random_generator.hpp"
#include "arch/widget_helpers.hpp"
#include "image/card_images.hpp"
#include "image/image_cacher.hpp"
#include "settings/preferences.hpp"
#include "table/gameplay_session.hpp"
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <deque>
#include <memory>
#include <optional>

class QPaintEvent;
class QResizeEvent;
class QColor;
class QPainter;

class card_widget : public BaseWidget {
    Q_OBJECT
    friend class card_widget_tests;
    friend class table;

public:
    explicit card_widget(BaseWidget* parent = nullptr);
    ~card_widget() override;

    // Target-mode projection only. The GUI-thread session must outlive this
    // widget at a stable address. Bind an empty classic widget once; its deck
    // identity stays fixed through Swap. Host refreshes after accepted domain
    // changes; no shoe/count copy, autonomous dealing or classic recovery is
    // used.
    [[nodiscard]] bool
    bind_gameplay_deck(const gameplay::session& owner, gameplay::deck_id id);
    void refresh_gameplay_deck();
    // Explicit teardown before owner replacement; resets only presentation.
    void unbind_gameplay_deck();

    void set_swap_selected(bool selected);
    [[nodiscard]] bool swap_selected() const;

    void set_slot_rotated(bool rotated);
    // Bound target presentation only. A transient 0..90-degree angle preserves
    // jitter/marking through resize frames; nullopt restores endpoint painting.
    void set_gameplay_layout_rotation(std::optional<qreal> degrees);
    void set_frame_style(slot_frame_style style);
    // 1-based table position shown by framed presentations; 0 hides it.
    void set_slot_number(int number);

    [[nodiscard]] int displayed_slot_number() const { return slot_number; }

    // Felt piping glows in the correction colour while feedback is shown.
    void set_verdict_glow(bool enabled);
    // Under gold piping, the band above the bottom edge that holds the frame
    // line and its legends; overlays stay above it. 0 for other frames.
    [[nodiscard]] int bottom_legend_clearance() const;
    void set_show_card_indexing(bool enabled);
    void set_show_strategy_name(bool enabled);
    void set_strategy_name(const QString& name);
    void set_table_marking_source(const QString& source);
    void set_hide_cards(bool hide);
    [[nodiscard]] bool has_cards() const;
    [[nodiscard]] bool has_current_card() const;
    [[nodiscard]] bool is_deck_exhausted() const;
    // Trainer checkpoint accessors. A session-bound card has no separate shoe;
    // its host reads typed counters and recovery state from the session.
    void trigger_highlight(int duration_ms);
    void tick_highlight(int delta_ms);
    void prepare_card_faces();
    [[nodiscard]] int card_face_target_short_px() const;
    void set_presented_card_images(const QHash<QString, QImage>& images);
    void clear_presented_card_images();
    [[nodiscard]] bool has_presented_card_images() const;
    void set_presented_image_mode(bool enabled);
    void sync_card_sheet_source();

signals:
    void rasterization_busy_changed(bool busy);
    void image_demand_changed();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void reset_presentation();
    const gameplay::session* gameplay_owner = nullptr;
    gameplay::deck_id gameplay_id { 0 };
    std::uint64_t presented_physical_cards = 0;
    bool swap_selected_flag;
    random_generator random_gen;
    qreal card_rotation_deg;
    QPointF card_offset;
    bool slot_rotated;
    std::optional<qreal> gameplay_layout_rotation_deg;
    // Table's temporary paint layer borrows this renderer, never its images or
    // domain state. Native controls/accessibility remain on the bound widget.
    bool gameplay_paint_external = false;
    slot_frame_style frame_style = slot_frame_style::classic;
    int slot_number = 0;
    bool verdict_glow = false;
    bool show_card_indexing_flag;
    bool show_strategy_name_flag;
    QString strategy_name;
    std::unique_ptr<QTimer> selection_timer;
    qreal selection_phase;

    struct discard_card {
        qreal rotation_deg {};
        QPointF offset;
    };

    std::deque<discard_card> discard_history;
    int highlight_duration_ms;
    int highlight_remaining_ms;
    bool highlight_active;
    bool hide_cards_flag;
    image_cacher table_marking;
    QString card_sheet_source;
    std::unique_ptr<card_image_consumer> card_images;
    QPixmap selected_card_face;
    int selected_card_face_index;
    QSize card_face_size;
    QHash<QString, QImage> displayed_card_images;
    bool preparation_queued = false;
    bool rasterizing;
    bool presented_image_mode;

    // One slot-local geometry for paint, jitter and raster demand. The packer
    // still owns the widget rectangle, independently of presentation choices.
    struct paint_geometry {
        QRectF frame;
        QRectF card;
        qreal slot_rotation = 0.0;
        qreal min_dim = 0.0;
        qreal jitter_limit = 0.0;
        [[nodiscard]] QPointF bounded_offset(const QPointF& offset) const;
    };

    [[nodiscard]] paint_geometry layout_geometry() const;
    [[nodiscard]] paint_geometry layout_geometry(const QSizeF& size) const;
    void
    paint_slot_frame(QPainter& painter, const paint_geometry& geometry) const;
    void paint_deck_content(QPainter& painter, const paint_geometry& geometry);
    void paint_piping_frame(
        QPainter& painter, const paint_geometry& geometry, const QColor& fill,
        const QColor& border
    ) const;

    void update_card_jitter();
    void update_accessible_description();
    [[nodiscard]] const gameplay::deck_state* gameplay_deck() const;
    [[nodiscard]] int display_card_index() const;
    [[nodiscard]] bool display_running() const;
    [[nodiscard]] bool display_hidden() const;
    [[nodiscard]] QStringList gameplay_extra_lines() const;
    void update_table_marking();
    [[nodiscard]] QSize card_face_target_size() const;
    void update_card_faces(const QSize& target_size);
    void schedule_card_preparation();
    [[nodiscard]] QString displayed_card_image_id() const;
    void invalidate_selected_card_face();
    const QPixmap& card_face_pixmap(int index);
    void record_discard();
    [[nodiscard]] qreal highlight_strength() const;
    void update_selection_pulse();
    void apply_presented_images(const QHash<QString, QImage>& images);
    void set_rasterizing(bool active);
    void on_card_images_changed();
    static void apply_card_transform(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset
    );
    void draw_table_marking(
        QPainter& painter, const QRectF& slot_frame_rect, qreal min_dim,
        qreal scale = 1.0
    ) const;
    void draw_card_index(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, const QPointF& offset
    ) const;
    // Felt only: soft drop shadow and, under piping, the kit's paper mat.
    void draw_card_mat(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
        bool top_card
    ) const;
    static void draw_card_shape(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
        const QColor& fill, const QColor& border
    );
    static void draw_card_pixmap(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
        const QPixmap& pixmap
    );
    static void draw_card_text(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
        const QString& text
    );
    static void draw_card_center_text(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, qreal rotation_deg, const QPointF& offset,
        const QString& text
    );
    void draw_card_extra_lines(
        QPainter& painter, const QRectF& oriented_card_rect,
        qreal slot_rotation_deg, const QPointF& offset,
        const QStringList& extra_lines
    ) const;
    [[nodiscard]] QString current_index_text() const;
    static qreal compute_font_point_size(const QRectF& card_rect);
    static QString weight_text_for_value(std::int64_t weight);
    static int rank_index_from_card_index(int card_index);
    static int blend_color_channel(int from, int to, qreal strength);
    static QColor
    blend_color(const QColor& from, const QColor& to, qreal strength);
};

#endif // KCUCKOOUNTER_TABLE_CARD_WIDGET_HPP
