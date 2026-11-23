#include "ChatOrchestrator.h"

namespace ModelBench {

ChatOrchestrator::ChatOrchestrator(QObject *parent) : QObject(parent) {}

void ChatOrchestrator::sendMessage(const QString &conversationId, const QString &text) {
    Q_UNUSED(conversationId); Q_UNUSED(text);
    // Boundary: wired to InferenceManager + MCP tool loop in Phase 2/5.
    emit generationFinished(conversationId);
}

} // namespace ModelBench
