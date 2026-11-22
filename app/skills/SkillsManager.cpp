#include "SkillsManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace ModelFit {

static const int kMaxScanFileSize = 2 * 1024 * 1024; // 2 MiB

SkillsManager::SkillsManager(QObject *parent) : QObject(parent) {
    qRegisterMetaType<SecurityWarning>("ModelFit::SecurityWarning");

    QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty())
        appData = QCoreApplication::applicationDirPath();
    m_skillsDir = appData + "/skills";

    loadState();
}

void SkillsManager::setSkillsDirectory(const QString &dir) {
    m_skillsDir = dir;
    loadState();
}

bool SkillsManager::copyTree(const QString &src, const QString &dst) {
    QDir s(src);
    if (!s.exists()) return false;

    QDir d;
    if (!d.mkpath(dst)) return false;

    for (const QFileInfo &fi :
         s.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString dest = dst + "/" + fi.fileName();
        if (fi.isDir()) {
            if (!copyTree(fi.absoluteFilePath(), dest)) return false;
        } else {
            if (!QFile::copy(fi.absoluteFilePath(), dest)) return false;
        }
    }
    return true;
}

bool SkillsManager::isTextFile(const QString &filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray head = file.read(4096);
    return !head.contains('\0');
}

QList<SecurityWarning> SkillsManager::scanPath(const QString &dir) {
    QList<SecurityWarning> warnings;
    QDirIterator it(dir, QDirIterator::Subdirectories);

    static const struct {
        QString name;
        QRegularExpression re;
    } kPatterns[] = {
        {"curl/wget",
         QRegularExpression("\\b(curl|wget)\\b",
                            QRegularExpression::CaseInsensitiveOption)},
        {"rm -rf",
         QRegularExpression("rm\\s+(-rf|-fr)",
                            QRegularExpression::CaseInsensitiveOption)},
        {"eval()",
         QRegularExpression("\\beval\\s*\\(",
                            QRegularExpression::CaseInsensitiveOption)},
        {"base64 decode",
         QRegularExpression("\\bbase64\\b.*\\b(-d|-D|--decode)\\b",
                            QRegularExpression::CaseInsensitiveOption)},
    };

    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        if (!fi.isFile()) continue;
        if (!fi.isReadable()) continue;

        QString rel = QDir(dir).relativeFilePath(fi.absoluteFilePath());

        // Executable scripts are flagged regardless of content.
        QString ext = fi.suffix().toLower();
        if (ext == "sh" || ext == "py" || ext == "js" || ext == "bat" ||
            ext == "ps1") {
            warnings.append({rel, "warning",
                             "Executable script present (*." + ext +
                                 "); review before enabling the skill"});
        }

        // Suspicious patterns in small text files.
        if (fi.size() > kMaxScanFileSize) continue;
        if (!isTextFile(fi.absoluteFilePath())) continue;

        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray data = file.readAll();

        for (const auto &pat : kPatterns) {
            QRegularExpressionMatch match = pat.re.match(data);
            if (match.hasMatch()) {
                warnings.append({rel, "error",
                                 QString("Suspicious pattern (%1) detected in %2")
                                     .arg(pat.name, rel)});
                break; // one warning per file is enough
            }
        }
    }
    return warnings;
}

void SkillsManager::installSkill(const QString &sourcePath, const QString &id) {
    QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists() || !srcInfo.isDir()) {
        emit skillError(id.isEmpty() ? sourcePath : id,
                        "Source path is not a directory");
        return;
    }

    QString realId = id;
    if (realId.isEmpty()) realId = srcInfo.fileName();
    if (realId.isEmpty() || realId == "." || realId == "..") {
        emit skillError(sourcePath, "Invalid skill id");
        return;
    }
    // Prevent path traversal via the id.
    if (realId.contains('/') || realId.contains('\\') ||
        realId.contains("..")) {
        emit skillError(sourcePath, "Skill id contains illegal characters");
        return;
    }

    QString target = m_skillsDir + "/" + realId;
    QDir().mkpath(m_skillsDir);
    QDir(target).removeRecursively(); // replace any previous installation

    if (!copyTree(sourcePath, target)) {
        emit skillError(realId, "Failed to copy skill directory tree");
        return;
    }

    // Parse SKILL.md frontmatter (+ supplementary file inventory).
    Skill s;
    s.id = realId;
    s.path = target;
    s.enabled = false; // disabled by default: explicit opt-in
    s.installed = true;
    s.installedAt = QDateTime::currentDateTimeUtc();

    QString readme = target + "/SKILL.md";
    if (QFile::exists(readme)) {
        QFile file(readme);
        if (file.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(file.readAll());
            QJsonObject fm = parseSkillMd(content);
            s.frontmatter = fm;
            s.name = fm.value("name").toString(srcInfo.fileName());
            s.description = fm.value("description").toString();
            s.version = fm.value("version").toString();
            if (fm.contains("id") && fm["id"].isString() &&
                !fm["id"].toString().isEmpty()) {
                // Frontmatter id is descriptive only; canonical id is the dir name.
            }
        }
    }
    if (s.name.isEmpty()) s.name = srcInfo.fileName();

    QDir skillDir(target);
    for (const QFileInfo &fi :
         skillDir.entryInfoList(QDir::Files | QDir::Dirs |
                                QDir::NoDotAndDotDot)) {
        s.files << fi.fileName();
    }

    // Replace or append.
    for (int i = 0; i < m_skills.size(); ++i) {
        if (m_skills[i].id == realId) {
            m_skills[i] = s;
            saveState();
            QList<SecurityWarning> warnings = scanPath(target);
            m_warnings[realId] = warnings;
            emit skillScanFinished(realId, warnings);
            emit skillInstalled(realId, warnings);
            return;
        }
    }
    m_skills << s;
    saveState();
    QList<SecurityWarning> warnings = scanPath(target);
    m_warnings[realId] = warnings;
    emit skillScanFinished(realId, warnings);
    emit skillInstalled(realId, warnings);
}

void SkillsManager::uninstallSkill(const QString &id) {
    for (int i = 0; i < m_skills.size(); ++i) {
        if (m_skills[i].id == id) {
            QString path = m_skills[i].path;
            m_skills.removeAt(i);
            if (!path.isEmpty()) QDir(path).removeRecursively();
            m_warnings.remove(id);
            saveState();
            emit skillUninstalled(id);
            return;
        }
    }
    emit skillError(id, "Skill is not installed");
}

void SkillsManager::setEnabled(const QString &id, bool enabled) {
    for (Skill &s : m_skills) {
        if (s.id == id) {
            s.enabled = enabled;
            saveState();
            emit skillEnabledChanged(id, enabled);
            return;
        }
    }
    emit skillError(id, "Skill not found");
}

bool SkillsManager::isEnabled(const QString &id) const {
    for (const Skill &s : m_skills)
        if (s.id == id) return s.enabled;
    return false;
}

Skill SkillsManager::skill(const QString &id) const {
    for (const Skill &s : m_skills)
        if (s.id == id) return s;
    return Skill();
}

QJsonObject SkillsManager::parseSkillMd(const QString &content) {
    QJsonObject obj;
    QRegularExpression re("^---\\s*\\n(.*?)\\n---",
                          QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch m = re.match(content);
    if (m.hasMatch()) {
        QString fm = m.captured(1);
        for (const QString &line : fm.split('\n')) {
            int idx = line.indexOf(':');
            if (idx > 0) {
                QString key = line.left(idx).trimmed();
                QString val = line.mid(idx + 1).trimmed();
                if (val.startsWith('"') && val.endsWith('"') && val.length() > 1)
                    val = val.mid(1, val.length() - 2);
                obj[key] = val;
            }
        }
    }
    return obj;
}

QJsonObject SkillsManager::readSkillFrontmatter(const QString &id) const {
    for (const Skill &s : m_skills)
        if (s.id == id) return s.frontmatter;
    return QJsonObject();
}

QStringList SkillsManager::skillFiles(const QString &id) const {
    for (const Skill &s : m_skills)
        if (s.id == id) return s.files;
    return QStringList();
}

void SkillsManager::scanSecurity(const QString &id) {
    QString path;
    for (const Skill &s : m_skills) {
        if (s.id == id) {
            path = s.path;
            break;
        }
    }
    if (path.isEmpty()) {
        emit skillError(id, "Skill not installed");
        return;
    }
    QList<SecurityWarning> warnings = scanPath(path);
    m_warnings[id] = warnings;
    emit skillScanFinished(id, warnings);
}

QList<SecurityWarning> SkillsManager::securityWarnings(const QString &id) const {
    return m_warnings.value(id);
}

void SkillsManager::loadState() {
    m_skills.clear();
    QDir dir(m_skillsDir);
    if (!dir.exists()) return;

    QFile file(m_skillsDir + "/installed.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return;
    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();
        Skill s;
        s.id = o.value("id").toString();
        s.name = o.value("name").toString();
        s.description = o.value("description").toString();
        s.path = o.value("path").toString();
        s.version = o.value("version").toString();
        s.enabled = o.value("enabled").toBool(false);
        s.installed = o.value("installed").toBool(false);
        s.installedAt =
            QDateTime::fromString(o.value("installedAt").toString(),
                                  Qt::ISODate);
        s.frontmatter = o.value("frontmatter").toObject();
        for (const auto &v : o.value("files").toArray()) s.files << v.toString();
        m_skills << s;
    }
}

void SkillsManager::saveState() {
    QDir dir(m_skillsDir);
    if (!dir.exists()) dir.mkpath(".");

    QJsonArray arr;
    for (const Skill &s : m_skills) {
        QJsonObject o;
        o["id"] = s.id;
        o["name"] = s.name;
        o["description"] = s.description;
        o["path"] = s.path;
        o["version"] = s.version;
        o["enabled"] = s.enabled;
        o["installed"] = s.installed;
        o["installedAt"] = s.installedAt.toString(Qt::ISODate);
        o["frontmatter"] = s.frontmatter;
        o["files"] = QJsonArray::fromStringList(s.files);
        arr.append(o);
    }

    QFile file(m_skillsDir + "/installed.json");
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();
}

QVariantList SkillsManager::getInstalled() const {
    QVariantList out;
    for (const auto &s : m_skills) {
        QVariantMap m;
        m["id"] = s.id;
        m["name"] = s.name;
        m["description"] = s.description;
        m["enabled"] = s.enabled;
        m["path"] = s.path;
        out << m;
    }
    return out;
}

} // namespace ModelFit
