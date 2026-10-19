#ifndef KCUCKOOUNTER_IMAGE_IMAGE_SOURCES_HPP
#define KCUCKOOUNTER_IMAGE_IMAGE_SOURCES_HPP

#include <rasterflow/qt/image_service.hpp>

// The application owns one service. Widgets own subscriptions, never workers or
// raster stores. Loading and decoding game files remains a client concern.
rasterflow::qt::image_service& application_image_service();

class image_source_loader;

struct source_acquisition_observations {
    std::size_t queued_requests {};
    std::size_t running_requests {};
    std::uint64_t requested_loads {};
    std::uint64_t started_requests {};
    std::uint64_t completed_requests {};
    std::uint64_t failed_requests {};
    std::uint64_t retired_completions {};
    std::uint64_t bytes_read {};
    std::uint64_t decoded_bytes {};
    std::uint64_t acquisition_us {};
    std::uint64_t owner_registration_us {};
    std::uint64_t maximum_owner_registration_us {};
};

// Application-thread subscription to file acquisition, not a raster scheduler.
// load replaces current intent even for the same path; clear/destruction retire
// it. Only the current asynchronous outcome emits finished (including errors).
class image_source_request final : public QObject {
    Q_OBJECT
public:
    explicit image_source_request(QObject* parent = nullptr);
    ~image_source_request() override;
    void load(const QString& path);
    void clear();
    [[nodiscard]] bool is_pending() const;
    [[nodiscard]] rasterflow::qt::source_handle source() const;
    [[nodiscard]] QString error() const;

signals:
    void started();
    void finished();

private:
    QPointer<image_source_loader> loader;
    std::uint64_t ticket {};
    bool pending {};
    rasterflow::qt::source_handle snapshot;
    QString acquisition_error;
    friend class image_source_loader;
};

source_acquisition_observations observe_source_acquisition();

#endif
