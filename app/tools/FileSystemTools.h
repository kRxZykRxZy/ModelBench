#pragma once

#include "ToolRegistry.h"
#include "FileEditHistory.h"

namespace ModelBench {

class FileEditHistory;

// Common base for built-in file tools. When a sandbox root is configured,
// every resolved path must stay inside it (symlinks are canonicalized).
class FileToolBase : public Tool {
    Q_OBJECT
public:
    explicit FileToolBase(const QString &sandboxRoot,
                          QObject *parent = nullptr);
    QString sandboxRoot() const { return m_root; }

    // Optional (non-owning) undo journal shared by write/edit tools.
    FileEditHistory *history() const { return m_history; }
    void setHistory(FileEditHistory *history) { m_history = history; }

protected:
    QString resolvePath(const QString &input, QString *error) const;

private:
    QString m_root;
    FileEditHistory *m_history = nullptr;
};

class ReadFileTool : public FileToolBase {
    Q_OBJECT
public:
    explicit ReadFileTool(const QString &sandboxRoot, QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("read_file"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::ReadOnly; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

class WriteFileTool : public FileToolBase {
    Q_OBJECT
public:
    explicit WriteFileTool(const QString &sandboxRoot, QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("write_file"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::Write; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

class EditFileTool : public FileToolBase {
    Q_OBJECT
public:
    explicit EditFileTool(const QString &sandboxRoot, QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("edit_file"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::Write; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

class ListDirTool : public FileToolBase {
    Q_OBJECT
public:
    explicit ListDirTool(const QString &sandboxRoot, QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("list_dir"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::ReadOnly; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

class SearchCodeTool : public FileToolBase {
    Q_OBJECT
public:
    explicit SearchCodeTool(const QString &sandboxRoot, QObject *parent = nullptr);
    QString name() const override { return QStringLiteral("search_code"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::ReadOnly; }
    void execute(qint64 callId, const QJsonObject &args) override;
};

namespace FileSystemTools {

// Creates all five tools as children of `registry` (via registerTool).
void registerTools(ToolRegistry &registry, const QString &sandboxRoot = {},
                   FileEditHistory *history = nullptr);

} // namespace FileSystemTools
} // namespace ModelBench
