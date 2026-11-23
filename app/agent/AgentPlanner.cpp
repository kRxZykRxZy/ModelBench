#include "AgentPlanner.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

namespace ModelBench {
namespace {

QString tailAfter(const QString &text, const QStringList &keywords) {
    for (const QString &keyword : keywords) {
        const QRegularExpression re(
            QStringLiteral("(?:^|\\b)%1\\b[\\s:]+(.+)$")
                .arg(QRegularExpression::escape(keyword)),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch()) return match.captured(1).trimmed();
    }
    return {};
}

QString quotedText(const QString &text) {
    const QRegularExpression re(QStringLiteral("[\"']([^\"']{1,200})[\"']"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString firstPath(const QString &text) {
    static const QRegularExpression re(
        QStringLiteral("([./A-Za-z0-9_~+@-]*[/\\\\][^\\s\"'()<>]+|"
                       "[A-Za-z0-9_-]+\\.[A-Za-z0-9]{1,8})"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

QJsonArray stringArray(const QStringList &values) {
    QJsonArray array;
    for (const QString &value : values) array.append(value);
    return array;
}

} // namespace

QJsonObject PlanStep::toJson() const {
    QJsonObject o;
    o[QStringLiteral("index")] = index;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("tool")] = toolName;
    o[QStringLiteral("args")] = args;
    o[QStringLiteral("requiresTool")] = requiresTool;
    o[QStringLiteral("status")] = statusName(status);
    o[QStringLiteral("observation")] = observation;
    return o;
}

PlanStep PlanStep::fromJson(const QJsonObject &json) {
    PlanStep step;
    step.index = json.value(QStringLiteral("index")).toInt();
    step.description = json.value(QStringLiteral("description")).toString();
    step.toolName = json.value(QStringLiteral("tool")).toString();
    step.args = json.value(QStringLiteral("args")).toObject();
    step.requiresTool = json.value(QStringLiteral("requiresTool")).toBool(
        !step.toolName.isEmpty());
    step.observation = json.value(QStringLiteral("observation")).toString();

    const QString status = json.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("running")) step.status = Status::Running;
    else if (status == QLatin1String("done")) step.status = Status::Done;
    else if (status == QLatin1String("failed")) step.status = Status::Failed;
    else if (status == QLatin1String("skipped")) step.status = Status::Skipped;
    else step.status = Status::Pending;
    return step;
}

QString PlanStep::statusName(Status status) {
    switch (status) {
    case Status::Pending: return QStringLiteral("pending");
    case Status::Running: return QStringLiteral("running");
    case Status::Done: return QStringLiteral("done");
    case Status::Failed: return QStringLiteral("failed");
    case Status::Skipped: return QStringLiteral("skipped");
    }
    return QStringLiteral("pending");
}

AgentPlanner::AgentPlanner(QObject *parent) : QObject(parent) {}
AgentPlanner::~AgentPlanner() = default;

void AgentPlanner::setMaxSteps(int count) {
    if (count > 0) m_maxSteps = count;
}

void AgentPlanner::clear() {
    if (m_steps.isEmpty()) return;
    m_steps.clear();
    emit planCleared();
}

void AgentPlanner::appendStep(const PlanStep &step) {
    if (m_steps.size() >= m_maxSteps) return;
    PlanStep copy = step;
    copy.index = m_steps.size();
    m_steps.append(copy);
}

void AgentPlanner::setPlan(const QJsonArray &stepsJson) {
    m_steps.clear();
    for (const QJsonValue &value : stepsJson)
        appendStep(PlanStep::fromJson(value.toObject()));
    emit planCreated(toJson());
}

QJsonArray AgentPlanner::plan(const QString &goal, const QString &context) {
    const QString trimmed = goal.trimmed();

    // Explicit JSON plan supplied by an LLM or the UI.
    if (trimmed.startsWith(u'[')) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
            m_steps.clear();
            for (const QJsonValue &value : doc.array())
                appendStep(PlanStep::fromJson(value.toObject()));
            emit planCreated(toJson());
            return toJson();
        }
    }

    clear();
    QStringList preamble;
    preamble << QStringLiteral("Goal: %1").arg(trimmed.left(400));
    if (!context.trimmed().isEmpty())
        preamble << QStringLiteral("Known context:\n%1").arg(context.left(1200));

    PlanStep reasoning;
    reasoning.description = preamble.join(u'\n');
    appendStep(reasoning);

    auto add = [this](const QString &description, const QString &tool,
                      const QJsonObject &args) {
        if (m_steps.size() >= m_maxSteps) return;
        PlanStep step;
        step.description = description;
        step.toolName = tool;
        step.requiresTool = !tool.isEmpty();
        step.args = args;
        appendStep(step);
    };

    const QString lower = trimmed.toLower();
    const QString path = firstPath(trimmed);

    // ---- git intents ---------------------------------------------------
    if (lower.contains(QStringLiteral("status")) ||
        lower.contains(QStringLiteral("working tree"))) {
        add(QStringLiteral("Inspect repository status"), QStringLiteral("git_status"),
            QJsonObject({{QStringLiteral("short"), true},
                         {QStringLiteral("branch"), true}}));
    }
    if (lower.contains(QStringLiteral("diff"))) {
        add(QStringLiteral("Inspect current changes"), QStringLiteral("git_diff"),
            QJsonObject());
    }
    if (lower.contains(QStringLiteral("commit"))) {
        QString message = quotedText(trimmed);
        if (message.isEmpty()) message = tailAfter(trimmed, {QStringLiteral("commit")});
        if (message.isEmpty()) message = QStringLiteral("WIP: %1").arg(trimmed.left(80));
        add(QStringLiteral("Create a commit"), QStringLiteral("git_commit"),
            QJsonObject({{QStringLiteral("message"), message}}));
    }
    if (lower.contains(QStringLiteral("branch")) ||
        lower.contains(QStringLiteral("checkout"))) {
        const QString branch = tailAfter(
            trimmed, {QStringLiteral("checkout"), QStringLiteral("switch to"),
                      QStringLiteral("branch")});
        add(QStringLiteral("Inspect branches"), QStringLiteral("git_branch"),
            QJsonObject({{QStringLiteral("action"), QStringLiteral("list")}}));
        if (!branch.isEmpty())
            add(QStringLiteral("Switch branch"), QStringLiteral("git_branch"),
                QJsonObject({{QStringLiteral("action"), QStringLiteral("checkout")},
                             {QStringLiteral("name"), branch}}));
    }
    if (lower.contains(QStringLiteral(" log")) || lower.startsWith(QStringLiteral("log"))) {
        add(QStringLiteral("Show recent history"), QStringLiteral("git_log"),
            QJsonObject({{QStringLiteral("count"), 20}}));
    }
    if (lower.contains(QStringLiteral("push"))) {
        add(QStringLiteral("Push commits"), QStringLiteral("git_push"), QJsonObject());
    }
    if (lower.contains(QStringLiteral("pull"))) {
        add(QStringLiteral("Pull updates"), QStringLiteral("git_pull"), QJsonObject());
    }

    // ---- file intents --------------------------------------------------
    if (lower.contains(QStringLiteral("read")) ||
        lower.contains(QStringLiteral("show file")) ||
        lower.contains(QStringLiteral("open "))) {
        QJsonObject args;
        if (!path.isEmpty()) args[QStringLiteral("path")] = path;
        add(QStringLiteral("Read the referenced file"), QStringLiteral("read_file"), args);
    }
    if (lower.contains(QStringLiteral("list")) || lower.contains(QStringLiteral("ls "))) {
        QJsonObject args;
        if (!path.isEmpty()) args[QStringLiteral("path")] = path;
        else args[QStringLiteral("path")] = QStringLiteral(".");
        add(QStringLiteral("List directory contents"), QStringLiteral("list_dir"), args);
    }
    if (lower.contains(QStringLiteral("write")) || lower.contains(QStringLiteral("create"))) {
        QJsonObject args;
        if (!path.isEmpty()) args[QStringLiteral("path")] = path;
        const QString content = quotedText(trimmed);
        args[QStringLiteral("content")] =
            content.isEmpty() ? trimmed.left(500) : content;
        add(QStringLiteral("Write the requested file"), QStringLiteral("write_file"), args);
    }
    if (lower.contains(QStringLiteral("search")) || lower.contains(QStringLiteral("find")) ||
        lower.contains(QStringLiteral("grep"))) {
        QString query = tailAfter(
            trimmed, {QStringLiteral("search for"), QStringLiteral("search"),
                      QStringLiteral("find"), QStringLiteral("grep")});
        if (query.isEmpty()) query = trimmed;
        add(QStringLiteral("Search the codebase"), QStringLiteral("search_code"),
            QJsonObject({{QStringLiteral("query"), query}}));
    }

    // ---- terminal intents ---------------------------------------------
    if (lower.contains(QStringLiteral("run ")) || lower.contains(QStringLiteral("execute")) ||
        lower.contains(QStringLiteral("build")) || lower.contains(QStringLiteral("compile")) ||
        lower.contains(QStringLiteral("test"))) {
        const QString command = tailAfter(
            trimmed, {QStringLiteral("run"), QStringLiteral("execute"),
                      QStringLiteral("build"), QStringLiteral("compile"),
                      QStringLiteral("test")});
        if (!command.isEmpty()) {
            QJsonObject args;
            static const QRegularExpression meta(QStringLiteral("[|&;<>$`*?]"));
            if (command.contains(meta)) {
                args[QStringLiteral("command")] = command;
                args[QStringLiteral("shell")] = true;
            } else {
                const QStringList parts = command.split(u' ', Qt::SkipEmptyParts);
                args[QStringLiteral("command")] = parts.value(0);
                if (parts.size() > 1)
                    args[QStringLiteral("args")] = stringArray(parts.mid(1));
            }
            add(QStringLiteral("Run the requested command"), QStringLiteral("run_terminal"), args);
        }
    }

    if (m_steps.size() == 1) {
        add(QStringLiteral("Summarize findings and suggest next steps"), QString(), {});
    }

    emit planCreated(toJson());
    return toJson();
}

PlanStep AgentPlanner::step(int index) const {
    if (index < 0 || index >= m_steps.size()) return PlanStep();
    return m_steps.at(index);
}

int AgentPlanner::nextPendingIndex() const {
    for (int i = 0; i < m_steps.size(); ++i) {
        if (m_steps.at(i).status == PlanStep::Status::Pending) return i;
    }
    return -1;
}

void AgentPlanner::markStep(int index, PlanStep::Status status,
                            const QString &observation) {
    if (index < 0 || index >= m_steps.size()) return;
    m_steps[index].status = status;
    if (!observation.isEmpty()) m_steps[index].observation = observation;
    emit stepUpdated(index, status, observation);
}

QJsonArray AgentPlanner::toJson() const {
    QJsonArray array;
    for (const PlanStep &step : m_steps) array.append(step.toJson());
    return array;
}

} // namespace ModelBench
