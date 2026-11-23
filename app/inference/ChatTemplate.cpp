#include "ChatTemplate.h"

#include <QByteArray>

#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
#include <llama.h>
#endif

namespace ModelBench {

QString ChatTemplate::modelTemplate(const llama_model *model, const QString &name)
{
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    if (!model) {
        return {};
    }
    const QByteArray nameUtf8 = name.toUtf8();
    const char *tmpl = llama_model_chat_template(model, nameUtf8.isEmpty() ? nullptr : nameUtf8.constData());
    return tmpl ? QString::fromUtf8(tmpl) : QString();
#else
    Q_UNUSED(model);
    Q_UNUSED(name);
    return {};
#endif
}

QString ChatTemplate::apply(const llama_model *model,
                            const QList<ChatTurn> &messages,
                            bool addAssistant,
                            const QString &tmplOverride)
{
    QString tmpl = tmplOverride;
    if (tmpl.isEmpty()) {
        tmpl = modelTemplate(model);
    }
    if (tmpl.isEmpty()) {
        tmpl = QStringLiteral("chatml");
    }
    return applyTemplate(tmpl, messages, addAssistant);
}

QString ChatTemplate::applyTemplate(const QString &tmpl,
                                    const QList<ChatTurn> &messages,
                                    bool addAssistant)
{
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    if (!tmpl.isEmpty() && !messages.isEmpty()) {
        QList<QByteArray> roles;
        QList<QByteArray> contents;
        QList<llama_chat_message> chat;
        roles.reserve(messages.size());
        contents.reserve(messages.size());
        chat.reserve(messages.size());

        for (const ChatTurn &msg : messages) {
            roles.append(msg.role.toUtf8());
            contents.append(msg.content.toUtf8());
        }
        for (int i = 0; i < messages.size(); ++i) {
            chat.append(llama_chat_message{roles.at(i).constData(), contents.at(i).constData()});
        }

        const QByteArray tmplUtf8 = tmpl.toUtf8();
        const int32_t needed = llama_chat_apply_template(
            tmplUtf8.constData(), chat.constData(), static_cast<size_t>(chat.size()),
            addAssistant, nullptr, 0);
        if (needed > 0) {
            QByteArray out;
            out.resize(needed + 1);
            const int32_t written = llama_chat_apply_template(
                tmplUtf8.constData(), chat.constData(), static_cast<size_t>(chat.size()),
                addAssistant, out.data(), static_cast<int32_t>(out.size()));
            if (written > 0) {
                return QString::fromUtf8(out.constData(), written);
            }
        }
    }
#else
    Q_UNUSED(tmpl);
#endif
    return fallback(messages, addAssistant);
}

QString ChatTemplate::fallback(const QList<ChatTurn> &messages, bool addAssistant)
{
    QString out;
    for (const ChatTurn &msg : messages) {
        out += QStringLiteral("<|im_start|>");
        out += msg.role;
        out += QLatin1Char('\n');
        out += msg.content;
        out += QStringLiteral("<|im_end|>\n");
    }
    if (addAssistant) {
        out += QStringLiteral("<|im_start|>assistant\n");
    }
    return out;
}

QString ChatTemplate::renderPrompt(const QList<ChatTurn> &messages, bool addAssistant)
{
    return fallback(messages, addAssistant);
}

} // namespace ModelBench
