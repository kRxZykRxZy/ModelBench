#include "Paths.h"

#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>

namespace ModelFit::Paths {

static QString base() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/.modelfit";
    return base;
}

QString dataDirectory() { return base(); }
QString modelsDirectory() { return base() + "/models"; }
QString logsDirectory() { return base() + "/logs"; }
QString tempDirectory() { return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/modelfit"; }
QString databasePath() { return base() + "/modelfit.db"; }
QString configPath() { return base() + "/config.json"; }

} // namespace ModelFit::Paths
