#include "settings/gameplay_recovery_fixture.hpp"

#include <QJsonDocument>
#include <array>

namespace recovery_test {

gameplay_checkpoint
snapshot(const gameplay::session& model, std::uint64_t elapsed) {
    auto state = gameplay::capture_checkpoint(model);
    if (!state)
        qFatal("Could not capture recovery fixture");
    return { std::move(*state), elapsed };
}

gameplay::session restored(const gameplay_checkpoint& checkpoint) {
    auto model = gameplay::restore_checkpoint(
        checkpoint.state, { 1, 64 }, std::array<std::size_t, 3> { 2, 0, 1 }
    );
    if (!model)
        qFatal("Could not restore recovery fixture");
    return std::move(*model);
}

void prime(gameplay::session& model) {
    using namespace gameplay;
    for (int i = 0; i < 12 && model.unresolved_quizzes().empty(); ++i) {
        if (!model.advance_time(17).accepted
            || model.deal_step().status != dealing_step_status::advanced)
            qFatal("Could not prime recovery fixture");
    }
    if (model.unresolved_quizzes().empty())
        qFatal("Recovery fixture did not open a quiz");
    for (const auto id : model.unresolved_quizzes())
        if (!model.edit_quiz_input(id, model.deck(id)->running_count - 1))
            qFatal("Could not edit recovery fixture");
}

void drive(gameplay::session& model, int steps) {
    using namespace gameplay;
    for (int i = 0; i < steps && model.phase() == session_phase::running; ++i) {
        for (const auto id : model.unresolved_quizzes()) {
            const auto* deck = model.deck(id);
            // Training-only wrong answers remain informational; all challenge
            // answers stay correct so finite/infinite continuations can run.
            if (!model.edit_quiz_input(
                    id,
                    deck->running_count - (deck->configuration.training ? 1 : 0)
                )
                || !model.check_quiz(id))
                qFatal("Could not resolve recovery fixture");
        }
        if (model.phase() != session_phase::running)
            break;
        if (!model.advance_time(17).accepted
            || model.deal_step().status != dealing_step_status::advanced)
            qFatal("Could not advance recovery fixture");
    }
}

gameplay_result_record
finish(gameplay::session& model, std::uint64_t sequence) {
    if (model.phase() != gameplay::session_phase::finished && !model.finish())
        qFatal("Could not finish recovery fixture");
    return { sequence, 1'700'000'000'000, *model.result() };
}

QByteArray document(const QJsonObject& root) {
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace recovery_test
