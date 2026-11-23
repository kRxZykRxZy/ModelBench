#pragma once

#include "ToolRegistry.h"

namespace ModelBench {

// Base for tools that shell out to `git` in a fixed repository root. Commands
// are built from structured arguments and always passed as an argv list, so
// user text can never be interpreted by a shell.
class GitToolBase : public Tool {
    Q_OBJECT
public:
    explicit GitToolBase(const QString &repoRoot, QObject *parent = nullptr);
    QString repositoryRoot() const { return m_root; }

protected:
    QString resolveRepoRoot(QString *error) const;
    ToolResult runGit(const QStringList &arguments,
                      int timeoutMs = 30000) const;

private:
    QString m_root;
};

class GitStatusTool : public GitToolBase {
    Q_OBJECT
public:
    using GitToolBase::GitToolBase;
    QString name() const override { return QStringLiteral("git_status"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    void execute(qint64 callId, const QJsonObject &args) override;
};

class GitDiffTool : public GitToolBase {
    Q_OBJECT
public:
    using GitToolBase::GitToolBase;
    QString name() const override { return QStringLiteral("git_diff"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    void execute(qint64 callId, const QJsonObject &args) override;
};

class GitCommitTool : public GitToolBase {
    Q_OBJECT
public:
    using GitToolBase::GitToolBase;
    QString name() const override { return QStringLiteral("git_commit"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::Write; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

class GitBranchTool : public GitToolBase {
    Q_OBJECT
public:
    using GitToolBase::GitToolBase;
    QString name() const override { return QStringLiteral("git_branch"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::ReadOnly; }
    ApprovalLevel approvalLevelFor(const QJsonObject &args) const override;
    void execute(qint64 callId, const QJsonObject &args) override;
};

class GitLogTool : public GitToolBase {
    Q_OBJECT
public:
    using GitToolBase::GitToolBase;
    QString name() const override { return QStringLiteral("git_log"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    void execute(qint64 callId, const QJsonObject &args) override;
};

class GitPushTool : public GitToolBase {
    Q_OBJECT
public:
    using GitToolBase::GitToolBase;
    QString name() const override { return QStringLiteral("git_push"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::Network; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

class GitPullTool : public GitToolBase {
    Q_OBJECT
public:
    using GitToolBase::GitToolBase;
    QString name() const override { return QStringLiteral("git_pull"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::Network; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

namespace GitTools {

void registerTools(ToolRegistry &registry, const QString &repoRoot);

} // namespace GitTools
} // namespace ModelBench
