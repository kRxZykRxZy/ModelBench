#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace ModelBench {

class SkillsManager;

// Injects enabled skills into a conversation context.
// The produced block wraps untrusted skill content in clear delimiters and
// respects a token budget so oversized skills do not swamp the prompt.
class SkillInjector : public QObject {
    Q_OBJECT
public:
    explicit SkillInjector(QObject *parent = nullptr);

    void setSkillsManager(SkillsManager *mgr);
    SkillsManager *skillsManager() const { return m_skills; }

    void setTokenBudget(int maxTokens);
    int tokenBudget() const { return m_tokenBudget; }

    // Rough token estimate: ceil(characters / 4).
    static int estimateTokens(const QString &text);

    // Build a context block from all enabled skills (respecting the token
    // budget) or from an explicit list of skill ids.
    QString buildContextBlock() const;
    QString buildContextBlock(const QStringList &skillIds) const;

    static QString contextStartDelimiter();
    static QString contextEndDelimiter();
    static QString skillStartDelimiter(const QString &skillId);
    static QString skillEndDelimiter(const QString &skillId);

private:
    QString buildBlock(const QStringList &skillIds, int &tokensUsed) const;

    SkillsManager *m_skills = nullptr;
    int m_tokenBudget = 4000; // rough tokens
};

} // namespace ModelBench
