#include "Logging.h"

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QtGlobal>
#include <QRegularExpression>

static QString redact(const QString &msg) {
    QString out = msg;
    // Redact common secret patterns (tokens, keys, bearer headers)
    QRegularExpression re("(?i)(token|api[_-]?key|secret|password|bearer|authorization)\\s*[:=]\\s*[^\\s,;]+");
    out.replace(re, "\\1: [REDACTED]");
    // Redact hf_ tokens and sk- keys
    out.replace(QRegularExpression("hf_[A-Za-z0-9]{10,}"), "hf_[REDACTED]");
    out.replace(QRegularExpression("sk-[A-Za-z0-9]{10,}"), "sk-[REDACTED]");
    return out;
}

namespace ModelBench::Logging {

static QFile *logFile = nullptr;

static void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
    QString level;
    switch (type) {
        case QtDebugMsg: level = "DEBUG"; break;
        case QtInfoMsg: level = "INFO"; break;
        case QtWarningMsg: level = "WARN"; break;
        case QtCriticalMsg: level = "CRIT"; break;
        case QtFatalMsg: level = "FATAL"; break;
    }
    QString line = QString("[%1] [%2] %3").arg(QDateTime::currentDateTime().toString(Qt::ISODate), level, redact(msg));
    if (logFile && logFile->isOpen()) {
        QTextStream ts(logFile);
        ts << line << "\n";
        ts.flush();
    }
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    if (type == QtFatalMsg) abort();
}

void install(const QString &logDir) {
    QDir().mkpath(logDir);
    logFile = new QFile(logDir + "/modelfit.log");
    logFile->open(QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(handler);
}

void setLevel(int level) { Q_UNUSED(level); }

} // namespace ModelBench::Logging
