#ifndef KCUCKOOUNTER_SHELL_GAMEPLAY_RESERVATION_HPP
#define KCUCKOOUNTER_SHELL_GAMEPLAY_RESERVATION_HPP

#include "settings/gameplay_recovery.hpp"

#include <QPointer>

class table;

/// One shell-owned durable identity, bound to a native scene, not its
/// allocation address alone. Keep it through Finish/retries; active work is
/// never cleared to undo attachment. Pending cleanup follows result admission.
/// Keep its address stable and retain it until completion is acknowledged.
class gameplay_reservation {
public:
    gameplay_reservation() = default;
    ~gameplay_reservation();
    gameplay_reservation(const gameplay_reservation&) = delete;
    gameplay_reservation& operator=(const gameplay_reservation&) = delete;
    enum class recovery_outcome { absent, paused, completed, failed };
    [[nodiscard]] bool start(
        table& view, gameplay_recovery_service& recovery,
        std::uint64_t dealing_seed, QString* error = nullptr,
        const gameplay::generation_policy& resources = {}
    );
    /// Explicit startup dispatch for an unbound host. Active records install
    /// Paused; pending results retry exact admission without changing the
    /// table.
    [[nodiscard]] recovery_outcome recover(
        table& view, gameplay_recovery_service& recovery,
        QString* error = nullptr,
        const gameplay::generation_policy& resources = {}
    );
    [[nodiscard]] std::optional<std::uint64_t> sequence() const;
    [[nodiscard]] bool owns(const table& view) const;
    /// Captured once before terminal presentation; survives owner retirement.
    [[nodiscard]] const std::optional<gameplay_result_record>&
    completion() const;
    /// Retry the retained record, including failed cleanup, without a live
    /// table.
    [[nodiscard]] bool
    complete(gameplay_recovery_service& recovery, QString* error = nullptr);
    /// True only after successful result admission AND matching recovery
    /// cleanup.
    [[nodiscard]] bool acknowledged() const;

private:
    void attach_result();
    void detach_result();
    void capture_result(const gameplay::session_result& result);
    QPointer<table> owner;
    std::uint64_t scene = 0;
    std::optional<std::uint64_t> identity;
    bool busy = false;
    std::optional<gameplay_result_record> terminal;
    bool admitted = false;
};

#endif
