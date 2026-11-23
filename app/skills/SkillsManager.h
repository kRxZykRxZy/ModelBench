#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QHash>
#include <QDateTime>

namespace ModelBench {

struct SecurityWarning {
    QString file;          // path relative to the skill directory
    QString severity;      // "info" | "warning" | "error"
    QString description;   // human-readable description
};

struct Skill {
    QString id;            // canonical id (directory name under skills dir)
    QString name;
    QString description;
    QString path;          // installed directory
    QString version;
    bool enabled = false;  // skills are disabled by default until explicitly enabled
    bool installed = false;
    QDateTime installedAt;
    QJsonObject frontmatter; // parsed SKILL.md frontmatter
    QStringList files;     // supplementary files in the skill directory
};

class SkillsManager : public QObject {
    Q_OBJECT
public:
    explicit SkillsManager(QObject *parent = nullptr);
    ~SkillsManager() override = default;

    QString skillsDirectory() const { return m_skillsDir; }
    void setSkillsDirectory(const QString &dir);

    // Install a skill by copying a source directory tree recursively into
    // skillsDir/<id>. If id is empty, the source directory name is used.
    Q_INVOKABLE void installSkill(const QString &sourcePath, const QString &id = {});
    Q_INVOKABLE void uninstallSkill(const QString &id);
    Q_INVOKABLE void setSkillEnabled(const QString &id, bool enabled) { setEnabled(id, enabled); }
    void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE QVariantList getInstalled() const;
    bool isEnabled(const QString &id) const;

    QList<Skill> installed() const { return m_skills; }
    Skill skill(const QString &id) const;

    // Parse YAML frontmatter between --- markers from SKILL.md content.
    static QJsonObject parseSkillMd(const QString &content);

    // Read and parse the SKILL.md of an installed skill.
    QJsonObject readSkillFrontmatter(const QString &id) const;
    QStringList skillFiles(const QString &id) const;

    // Security scan: detects scripts (.sh/.py/.js/.bat/.ps1) and suspicious
    // patterns (curl|wget, rm -rf, eval, base64 decode). Runs on install and
    // on demand.
    void scanSecurity(const QString &id);
    QList<SecurityWarning> securityWarnings(const QString &id) const;

signals:
    void skillInstalled(const QString &id,
                        const QList<SecurityWarning> &warnings);
    void skillUninstalled(const QString &id);
    void skillEnabledChanged(const QString &id, bool enabled);
    void skillScanFinished(const QString &id,
                           const QList<SecurityWarning> &warnings);
    void skillError(const QString &id, const QString &error);

private:
    bool copyTree(const QString &src, const QString &dst);
    QList<SecurityWarning> scanPath(const QString &dir);
    bool isTextFile(const QString &filePath) const;
    void loadState();
    void saveState();

    QString m_skillsDir;
    QList<Skill> m_skills;
    QHash<QString, QList<SecurityWarning>> m_warnings;
};

} // namespace ModelBench

Q_DECLARE_METATYPE(ModelBench::SecurityWarning)
