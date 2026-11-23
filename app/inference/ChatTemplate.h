#pragma once

#include <QString>
#include <QList>

struct llama_model;

namespace ModelBench {

struct ChatTurn {
    QString role;
    QString content;
};

/**
 * Thin wrapper around llama_chat_apply_template.
 *
 * The helper deliberately does not depend on llama.h in this header so that
 * callers (and unit tests) can format prompts even in builds without
 * llama.cpp. When a model is supplied and the build has HAVE_LLAMA_CPP the
 * model's own chat template is used; otherwise a ChatML fallback is applied.
 */
class ChatTemplate {
public:
    /// Template embedded in the GGUF metadata, or an empty string.
    static QString modelTemplate(const llama_model *model, const QString &name = {});

    /// Apply the model's template (or tmplOverride) to the conversation.
    static QString apply(const llama_model *model,
                         const QList<ChatTurn> &messages,
                         bool addAssistant = true,
                         const QString &tmplOverride = {});

    /// Apply an explicit template string/name.
    static QString applyTemplate(const QString &tmpl,
                                 const QList<ChatTurn> &messages,
                                 bool addAssistant = true);

    /// ChatML fallback that works without llama.cpp.
    static QString fallback(const QList<ChatTurn> &messages, bool addAssistant = true);

    /// Format a conversation without a model (always ChatML).
    static QString renderPrompt(const QList<ChatTurn> &messages, bool addAssistant = true);
};

} // namespace ModelBench
