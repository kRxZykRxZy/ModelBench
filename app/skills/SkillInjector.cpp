#include "SkillInjector.h"
#include "SkillsManager.h"

#include <QFile>
#include <QDateTime>
#include <QTextStream>

namespace ModelFit {

SkillInjector::SkillInjector(QObject *parent) : QObject(parent) {}

void SkillInjector::setSkillsManager(SkillsManager *mgr) { m_skills = mgr; }

void SkillInjector::setTokenBudget(int maxTokens) {
    if (maxTokens > 0) m_tokenBudget = maxTokens;
}

int SkillInjector::estimateTokens(const QString &text) {
    // Conservative heuristic: 1 token ≈ 4 characters.
    return (text.size() + 3) / 4;
}

QString SkillInjector::contextStartDelimiter() {
    return "<<<BEGIN SKILL CONTEXT (untrusted content)>>>";
}

QString SkillInjector::contextEndDelimiter() {
    return "<<<END SKILL CONTEXT>>>";
}

QString SkillInjector::skillStartDelimiter(const QString &skillId) {
    return "<<<BEGIN UNTRUSTED SKILL: " + skillId + ">>>";
}

QString SkillInjector::skillEndDelimiter(const QString &skillId) {
    return "<<<END UNTRUSTED SKILL: " + skillId + ">>>";
}

QString SkillInjector::buildContextBlock(const QStringList &skillIds) const {
    int tokensUsed = 0;
    return buildBlock(skillIds, tokensUsed);
}

QString SkillInjector::buildBlock(const QStringList &skillIds,
                                  int &tokensUsed) const {
    tokensUsed = 0;
    if (!m_skills) return QString();

    QString block = contextStartDelimiter() + "\n";
    tokensUsed += estimateTokens(block);

    bool any = false;
    for (const QString &id : skillIds) {
        if (id.isEmpty()) continue;

        Skill skill = m_skills->skill(id);
        if (!skill.installed) continue;

        QString skillPath = skill.path + "/SKILL.md";
        QString content;
        QFile file(skillPath);
        if (file.open(QIODevice::ReadOnly)) {
            content = QString::fromUtf8(file.readAll());
        } else {
            content = QString("[SKILL.md not readable for skill %1]").arg(id);
        }

        QString header = skillStartDelimiter(id) +
                         QString(" name=\"%1\" version=\"%2\"")
                             .arg(skill.name, skill.version) + "\n";
        QString footer = skillEndDelimiter(id) + "\n";

        QString skillBlock = header + content + "\n" + footer;
        int skillTokens = estimateTokens(skillBlock);

        if (tokensUsed + skillTokens > m_tokenBudget) {
            // Try to fit a truncated copy of the content.
            int available =
                m_tokenBudget - tokensUsed - estimateTokens(header + footer) -
                estimateTokens("\n[...truncated to fit token budget...]\n");
            if (available > 20) {
                int chars = available * 4;
                QString truncated =
                    content.left(chars) +
                    "\n[...truncated to fit token budget...]";
                skillBlock = header + truncated + "\n" + footer;
                skillTokens = estimateTokens(skillBlock);
                block += skillBlock;
                tokensUsed += skillTokens;
            }
            break; // budget exhausted
        }

        block += skillBlock;
        tokensUsed += skillTokens;
        any = true;
    }

    block += contextEndDelimiter() + "\n";
    tokensUsed += estimateTokens(block);
    Q_UNUSED(any)
    return block;
}

QString SkillInjector::buildContextBlock() const {
    if (!m_skills) return QString();
    QStringList ids;
    for (const Skill &s : m_skills->installed()) {
        if (s.enabled) ids << s.id;
    }
    return buildContextBlock(ids);
}

} // namespace ModelFit
