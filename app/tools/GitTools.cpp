#include "GitTools.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>

namespace ModelBench {
namespace {

QJsonObject prop(const QString &type, const QString &description,
                 const QJsonValue &def = QJsonValue()) {
    QJsonObject o;
    o[QStringLiteral("type")] = type;
    o[QStringLiteral("description")] = description;
    if (!def.isNull()) o[QStringLiteral("default")] = def;
    return o;
}

QJsonObject schema(const QJsonObject &properties, const QStringList &required = {}) {
    QJsonObject s;
    s[QStringLiteral("type")] = QStringLiteral("object");
    s[QStringLiteral("properties")] = properties;
    if (!required.isEmpty()) {
        QJsonArray req;
        for (const QString &r : required) req.append(r);
        s[QStringLiteral("required")] = req;
    }
    return s;
}

ToolResult failed(const QString &name, const QString &message,
                  const QString &code) {
    ToolResult r;
    r.ok = false;
    r.toolName = name;
    r.error = message;
    r.errorCode = code;
    return r;
}

bool isInsideRoot(const QString &root, const QString &candidate) {
    QString a = QDir::fromNativeSeparators(QDir::cleanPath(root));
    QString b = QDir::fromNativeSeparators(QDir::cleanPath(candidate));
#ifdef Q_OS_WIN
    a = a.toLower();
    b = b.toLower();
#endif
    if (a == b) return true;
    if (!a.endsWith(u'/')) a.append(u'/');
    return b.startsWith(a);
}

// Resolve an optional repository-relative file path, rejecting traversal.
bool pathArgument(const QString &root, const QString &input, QString *relative,
                  QString *error) {
    if (input.isEmpty()) return true;
    if (input.contains(QChar('\0'))) {
        *error = QStringLiteral("path contains a NUL byte");
        return false;
    }
    const QFileInfo info(input);
    const QString absolute = info.isAbsolute() ? input : QDir(root).absoluteFilePath(input);
    const QString clean = QDir::cleanPath(absolute);
    if (!isInsideRoot(root, clean)) {
        *error = QStringLiteral("path '%1' is outside the repository").arg(input);
        return false;
    }
    *relative = QDir(root).relativeFilePath(clean);
    return true;
}

QString joinCommand(const QStringList &args) {
    QStringList all{QStringLiteral("git")};
    all << QStringLiteral("-c") << QStringLiteral("color.ui=false")
        << QStringLiteral("--no-pager") << args;
    for (QString &part : all) {
        if (part.contains(u' ')) part = QStringLiteral("\"%1\"").arg(part);
    }
    return all.join(u' ');
}

QJsonObject commandOutput(const QString &command, const QByteArray &out,
                          const QByteArray &err, int exitCode, bool truncated) {
    QJsonObject o;
    o[QStringLiteral("command")] = command;
    o[QStringLiteral("stdout")] = QString::fromUtf8(out);
    o[QStringLiteral("stderr")] = QString::fromUtf8(err);
    o[QStringLiteral("exitCode")] = exitCode;
    o[QStringLiteral("truncated")] = truncated;
    return o;
}

void capBytes(QByteArray *bytes, qint64 cap, bool *truncated) {
    if (bytes->size() > cap) {
        bytes->resize(int(cap));
        if (truncated) *truncated = true;
    }
}

} // namespace

GitToolBase::GitToolBase(const QString &repoRoot, QObject *parent)
    : Tool(parent), m_root(repoRoot) {}

QString GitToolBase::resolveRepoRoot(QString *error) const {
    if (m_root.isEmpty()) {
        if (error) *error = QStringLiteral("repository root is not configured");
        return {};
    }
    const QString root = QDir::cleanPath(QDir().absoluteFilePath(m_root));
    if (!QFileInfo(root).isDir()) {
        if (error) *error = QStringLiteral("repository root does not exist: %1").arg(root);
        return {};
    }
    return root;
}

ToolResult GitToolBase::runGit(const QStringList &arguments, int timeoutMs) const {
    QString error;
    const QString root = resolveRepoRoot(&error);
    ToolResult r;
    r.toolName = name();
    if (root.isEmpty()) {
        r.error = error;
        r.errorCode = QStringLiteral("bad_repo");
        return r;
    }

    const int waitMs = qBound(100, timeoutMs, 600000);
    QStringList full{QStringLiteral("-c"), QStringLiteral("color.ui=false"),
                     QStringLiteral("--no-pager")};
    full.append(arguments);

    QProcess process;
    process.setWorkingDirectory(root);
    process.start(QStringLiteral("git"), full);
    if (!process.waitForStarted(waitMs)) {
        r.error = QStringLiteral("Could not start git (is it installed?)");
        r.errorCode = QStringLiteral("git_unavailable");
        return r;
    }
    if (!process.waitForFinished(waitMs)) {
        process.kill();
        process.waitForFinished(1000);
        r.error = QStringLiteral("git command timed out");
        r.errorCode = QStringLiteral("timeout");
        r.cancelled = true;
        r.output = commandOutput(joinCommand(arguments), process.readAllStandardOutput(),
                                 process.readAllStandardError(), -1, false);
        return r;
    }

    QByteArray out = process.readAllStandardOutput();
    QByteArray err = process.readAllStandardError();
    bool truncated = false;
    constexpr qint64 kCap = 1024 * 1024;
    capBytes(&out, kCap, &truncated);
    capBytes(&err, kCap, &truncated);

    const int exitCode = process.exitCode();
    r.ok = exitCode == 0;
    r.output = commandOutput(joinCommand(arguments), out, err, exitCode, truncated);
    r.truncated = truncated;
    if (!r.ok) {
        const QString message = QString::fromUtf8(err).trimmed();
        r.error = message.isEmpty()
                      ? QStringLiteral("git exited with code %1").arg(exitCode)
                      : message;
        r.errorCode = QStringLiteral("git_error");
    }
    return r;
}

// -------------------------------------------------------------- git_status
QString GitStatusTool::description() const {
    return QStringLiteral("Show the working tree status (git status).");
}

QJsonObject GitStatusTool::inputSchema() const {
    return schema(QJsonObject({
        {QStringLiteral("short"), prop(QStringLiteral("boolean"),
                                       QStringLiteral("Use --short output."), true)},
        {QStringLiteral("branch"), prop(QStringLiteral("boolean"),
                                        QStringLiteral("Include branch information."), false)},
    }));
}

void GitStatusTool::execute(qint64 callId, const QJsonObject &args) {
    QStringList argv{QStringLiteral("status")};
    if (args.value(QStringLiteral("short")).toBool(true)) argv << QStringLiteral("--short");
    if (args.value(QStringLiteral("branch")).toBool(false)) argv << QStringLiteral("--branch");
    emit finished(callId, runGit(argv));
}

// ---------------------------------------------------------------- git_diff
QString GitDiffTool::description() const {
    return QStringLiteral("Show changes (git diff), optionally staged or against a ref.");
}

QJsonObject GitDiffTool::inputSchema() const {
    return schema(QJsonObject({
        {QStringLiteral("path"), prop(QStringLiteral("string"),
                                      QStringLiteral("Limit the diff to a file/directory."))},
        {QStringLiteral("staged"), prop(QStringLiteral("boolean"),
                                        QStringLiteral("Show staged changes."), false)},
        {QStringLiteral("stat"), prop(QStringLiteral("boolean"),
                                      QStringLiteral("Show a diffstat."), false)},
        {QStringLiteral("context"), prop(QStringLiteral("integer"),
                                         QStringLiteral("Lines of context."), 3)},
        {QStringLiteral("base"), prop(QStringLiteral("string"),
                                      QStringLiteral("Compare against this ref."))},
    }));
}

void GitDiffTool::execute(qint64 callId, const QJsonObject &args) {
    QStringList argv{QStringLiteral("diff")};
    if (args.value(QStringLiteral("staged")).toBool(false)) argv << QStringLiteral("--cached");
    if (args.value(QStringLiteral("stat")).toBool(false)) argv << QStringLiteral("--stat");
    if (args.value(QStringLiteral("context")).isDouble())
        argv << QStringLiteral("--unified=%1")
                    .arg(args.value(QStringLiteral("context")).toInt(3));

    QString error;
    const QString root = resolveRepoRoot(&error);
    if (root.isEmpty()) {
        emit finished(callId, failed(name(), error, QStringLiteral("bad_repo")));
        return;
    }
    const QString base = args.value(QStringLiteral("base")).toString();
    if (!base.isEmpty()) {
        if (!base.startsWith(u'-')) argv << base;
        else {
            emit finished(callId, failed(name(), QStringLiteral("Invalid base ref"),
                                         QStringLiteral("bad_ref")));
            return;
        }
    }
    QString relative;
    if (!pathArgument(root, args.value(QStringLiteral("path")).toString(),
                      &relative, &error)) {
        emit finished(callId, failed(name(), error, QStringLiteral("bad_path")));
        return;
    }
    if (!relative.isEmpty()) argv << QStringLiteral("--") << relative;
    emit finished(callId, runGit(argv));
}

// -------------------------------------------------------------- git_commit
QString GitCommitTool::description() const {
    return QStringLiteral("Stage changes (optional) and create a commit.");
}

QJsonObject GitCommitTool::inputSchema() const {
    return schema(
        QJsonObject({
            {QStringLiteral("message"), prop(QStringLiteral("string"),
                                             QStringLiteral("Commit message."))},
            {QStringLiteral("all"), prop(QStringLiteral("boolean"),
                                         QStringLiteral("Stage all changes before committing."), false)},
            {QStringLiteral("amend"), prop(QStringLiteral("boolean"),
                                           QStringLiteral("Amend the previous commit."), false)},
        }),
        {QStringLiteral("message")});
}

void GitCommitTool::execute(qint64 callId, const QJsonObject &args) {
    const QString message = args.value(QStringLiteral("message")).toString();
    if (message.isEmpty()) {
        emit finished(callId, failed(name(), QStringLiteral("message is required"),
                                     QStringLiteral("validation_error")));
        return;
    }

    if (args.value(QStringLiteral("all")).toBool(false)) {
        const ToolResult staged = runGit({QStringLiteral("add"), QStringLiteral("-A")});
        if (!staged.ok) {
            emit finished(callId, staged);
            return;
        }
    }

    QStringList argv{QStringLiteral("commit")};
    if (args.value(QStringLiteral("amend")).toBool(false)) argv << QStringLiteral("--amend");
    argv << QStringLiteral("-m") << message; // message stays a single argv entry
    ToolResult result = runGit(argv);

    if (result.ok) {
        const ToolResult hash = runGit({QStringLiteral("rev-parse"),
                                        QStringLiteral("--short"),
                                        QStringLiteral("HEAD")});
        if (hash.ok) {
            QJsonObject out = result.output;
            out[QStringLiteral("commit")] =
                hash.output.value(QStringLiteral("stdout")).toString().trimmed();
            result.output = out;
        }
    }
    emit finished(callId, result);
}

// -------------------------------------------------------------- git_branch
QString GitBranchTool::description() const {
    return QStringLiteral("List, create, checkout, delete or rename branches.");
}

QJsonObject GitBranchTool::inputSchema() const {
    return schema(QJsonObject({
        {QStringLiteral("action"), prop(QStringLiteral("string"),
                                        QStringLiteral("list|create|checkout|delete|rename"),
                                        QStringLiteral("list"))},
        {QStringLiteral("name"), prop(QStringLiteral("string"),
                                      QStringLiteral("Branch name."))},
        {QStringLiteral("newName"), prop(QStringLiteral("string"),
                                         QStringLiteral("New name for rename."))},
        {QStringLiteral("startPoint"), prop(QStringLiteral("string"),
                                            QStringLiteral("Ref to create from."))},
        {QStringLiteral("all"), prop(QStringLiteral("boolean"),
                                     QStringLiteral("Include remote branches when listing."), false)},
        {QStringLiteral("force"), prop(QStringLiteral("boolean"),
                                       QStringLiteral("Allow forced delete."), false)},
    }));
}

ApprovalLevel GitBranchTool::approvalLevelFor(const QJsonObject &args) const {
    const QString action = args.value(QStringLiteral("action")).toString(QStringLiteral("list"));
    if (action == QLatin1String("list")) return ApprovalLevel::ReadOnly;
    if (action == QLatin1String("delete")) return ApprovalLevel::Dangerous;
    return ApprovalLevel::Write;
}

void GitBranchTool::execute(qint64 callId, const QJsonObject &args) {
    const QString action = args.value(QStringLiteral("action")).toString(QStringLiteral("list"));
    const QString branchName = args.value(QStringLiteral("name")).toString();

    if (action == QLatin1String("list")) {
        QStringList argv{QStringLiteral("branch"), QStringLiteral("--list")};
        if (args.value(QStringLiteral("all")).toBool(false)) argv << QStringLiteral("--all");
        if (!branchName.isEmpty()) argv << branchName;
        emit finished(callId, runGit(argv));
        return;
    }
    if (branchName.isEmpty()) {
        emit finished(callId, failed(name(), QStringLiteral("name is required"),
                                     QStringLiteral("validation_error")));
        return;
    }

    QStringList argv;
    if (action == QLatin1String("create")) {
        argv << QStringLiteral("branch") << branchName;
        const QString start = args.value(QStringLiteral("startPoint")).toString();
        if (!start.isEmpty()) argv << start;
    } else if (action == QLatin1String("checkout")) {
        argv << QStringLiteral("checkout") << branchName;
    } else if (action == QLatin1String("delete")) {
        argv << QStringLiteral("branch")
             << (args.value(QStringLiteral("force")).toBool(false)
                     ? QStringLiteral("-D")
                     : QStringLiteral("-d"))
             << branchName;
    } else if (action == QLatin1String("rename")) {
        const QString newName = args.value(QStringLiteral("newName")).toString();
        if (newName.isEmpty()) {
            emit finished(callId, failed(name(), QStringLiteral("newName is required"),
                                         QStringLiteral("validation_error")));
            return;
        }
        argv << QStringLiteral("branch") << QStringLiteral("-m") << branchName << newName;
    } else {
        emit finished(callId, failed(name(),
                                     QStringLiteral("Unknown branch action: %1").arg(action),
                                     QStringLiteral("validation_error")));
        return;
    }
    emit finished(callId, runGit(argv));
}

// ---------------------------------------------------------------- git_log
QString GitLogTool::description() const {
    return QStringLiteral("Show recent commit history (git log).");
}

QJsonObject GitLogTool::inputSchema() const {
    return schema(QJsonObject({
        {QStringLiteral("count"), prop(QStringLiteral("integer"),
                                       QStringLiteral("Number of commits."), 20)},
        {QStringLiteral("oneline"), prop(QStringLiteral("boolean"),
                                         QStringLiteral("Compact one-line output."), true)},
        {QStringLiteral("decorate"), prop(QStringLiteral("boolean"),
                                          QStringLiteral("Show refs."), true)},
        {QStringLiteral("author"), prop(QStringLiteral("string"),
                                        QStringLiteral("Filter by author."))},
        {QStringLiteral("since"), prop(QStringLiteral("string"),
                                       QStringLiteral("Show commits after this date."))},
        {QStringLiteral("until"), prop(QStringLiteral("string"),
                                       QStringLiteral("Show commits before this date."))},
        {QStringLiteral("path"), prop(QStringLiteral("string"),
                                      QStringLiteral("Limit to a file/directory."))},
    }));
}

void GitLogTool::execute(qint64 callId, const QJsonObject &args) {
    QStringList argv{QStringLiteral("log")};
    if (args.value(QStringLiteral("oneline")).toBool(true)) argv << QStringLiteral("--oneline");
    if (args.value(QStringLiteral("decorate")).toBool(true)) argv << QStringLiteral("--decorate");
    argv << QStringLiteral("-n")
         << QString::number(qBound(1, args.value(QStringLiteral("count")).toInt(20), 500));

    const QString author = args.value(QStringLiteral("author")).toString();
    if (!author.isEmpty()) argv << QStringLiteral("--author=%1").arg(author);
    const QString since = args.value(QStringLiteral("since")).toString();
    if (!since.isEmpty()) argv << QStringLiteral("--since=%1").arg(since);
    const QString until = args.value(QStringLiteral("until")).toString();
    if (!until.isEmpty()) argv << QStringLiteral("--until=%1").arg(until);

    QString error;
    const QString root = resolveRepoRoot(&error);
    if (root.isEmpty()) {
        emit finished(callId, failed(name(), error, QStringLiteral("bad_repo")));
        return;
    }
    QString relative;
    if (!pathArgument(root, args.value(QStringLiteral("path")).toString(),
                      &relative, &error)) {
        emit finished(callId, failed(name(), error, QStringLiteral("bad_path")));
        return;
    }
    if (!relative.isEmpty()) argv << QStringLiteral("--") << relative;
    emit finished(callId, runGit(argv));
}

// --------------------------------------------------------------- git_push
QString GitPushTool::description() const {
    return QStringLiteral("Push commits to a remote (git push).");
}

QJsonObject GitPushTool::inputSchema() const {
    return schema(QJsonObject({
        {QStringLiteral("remote"), prop(QStringLiteral("string"),
                                        QStringLiteral("Remote name."), QStringLiteral("origin"))},
        {QStringLiteral("branch"), prop(QStringLiteral("string"),
                                        QStringLiteral("Branch to push."))},
        {QStringLiteral("setUpstream"), prop(QStringLiteral("boolean"),
                                             QStringLiteral("Add upstream tracking."), false)},
        {QStringLiteral("forceWithLease"), prop(QStringLiteral("boolean"),
                                                QStringLiteral("Use --force-with-lease."), false)},
        {QStringLiteral("dryRun"), prop(QStringLiteral("boolean"),
                                        QStringLiteral("Do not actually push."), false)},
    }));
}

void GitPushTool::execute(qint64 callId, const QJsonObject &args) {
    QStringList argv{QStringLiteral("push")};
    const QString remote = args.value(QStringLiteral("remote")).toString(QStringLiteral("origin"));
    if (remote.startsWith(u'-')) {
        emit finished(callId, failed(name(), QStringLiteral("Invalid remote name"),
                                     QStringLiteral("validation_error")));
        return;
    }
    argv << remote;
    const QString branch = args.value(QStringLiteral("branch")).toString();
    if (!branch.isEmpty()) {
        if (branch.startsWith(u'-')) {
            emit finished(callId, failed(name(), QStringLiteral("Invalid branch name"),
                                         QStringLiteral("validation_error")));
            return;
        }
        argv << branch;
    }
    if (args.value(QStringLiteral("setUpstream")).toBool(false))
        argv << QStringLiteral("--set-upstream");
    if (args.value(QStringLiteral("forceWithLease")).toBool(false))
        argv << QStringLiteral("--force-with-lease");
    if (args.value(QStringLiteral("dryRun")).toBool(false))
        argv << QStringLiteral("--dry-run");
    emit finished(callId, runGit(argv));
}

// --------------------------------------------------------------- git_pull
QString GitPullTool::description() const {
    return QStringLiteral("Fetch and integrate from a remote (git pull).");
}

QJsonObject GitPullTool::inputSchema() const {
    return schema(QJsonObject({
        {QStringLiteral("remote"), prop(QStringLiteral("string"),
                                        QStringLiteral("Remote name."), QStringLiteral("origin"))},
        {QStringLiteral("branch"), prop(QStringLiteral("string"),
                                        QStringLiteral("Branch to pull."))},
        {QStringLiteral("rebase"), prop(QStringLiteral("boolean"),
                                        QStringLiteral("Rebase instead of merge."), false)},
        {QStringLiteral("ffOnly"), prop(QStringLiteral("boolean"),
                                        QStringLiteral("Fast-forward only."), false)},
        {QStringLiteral("dryRun"), prop(QStringLiteral("boolean"),
                                        QStringLiteral("Do not actually pull."), false)},
    }));
}

void GitPullTool::execute(qint64 callId, const QJsonObject &args) {
    QStringList argv{QStringLiteral("pull")};
    if (args.value(QStringLiteral("rebase")).toBool(false)) argv << QStringLiteral("--rebase");
    if (args.value(QStringLiteral("ffOnly")).toBool(false)) argv << QStringLiteral("--ff-only");
    if (args.value(QStringLiteral("dryRun")).toBool(false)) argv << QStringLiteral("--dry-run");
    const QString remote = args.value(QStringLiteral("remote")).toString(QStringLiteral("origin"));
    if (remote.startsWith(u'-')) {
        emit finished(callId, failed(name(), QStringLiteral("Invalid remote name"),
                                     QStringLiteral("validation_error")));
        return;
    }
    argv << remote;
    const QString branch = args.value(QStringLiteral("branch")).toString();
    if (!branch.isEmpty()) {
        if (branch.startsWith(u'-')) {
            emit finished(callId, failed(name(), QStringLiteral("Invalid branch name"),
                                         QStringLiteral("validation_error")));
            return;
        }
        argv << branch;
    }
    emit finished(callId, runGit(argv));
}

namespace GitTools {

void registerTools(ToolRegistry &registry, const QString &repoRoot) {
    registry.registerTool(new GitStatusTool(repoRoot, &registry));
    registry.registerTool(new GitDiffTool(repoRoot, &registry));
    registry.registerTool(new GitCommitTool(repoRoot, &registry));
    registry.registerTool(new GitBranchTool(repoRoot, &registry));
    registry.registerTool(new GitLogTool(repoRoot, &registry));
    registry.registerTool(new GitPushTool(repoRoot, &registry));
    registry.registerTool(new GitPullTool(repoRoot, &registry));
}

} // namespace GitTools
} // namespace ModelBench
