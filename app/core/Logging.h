#pragma once

#include <QString>

namespace ModelFit::Logging {

void install(const QString &logDir);
void setLevel(int level);

} // namespace ModelFit::Logging
