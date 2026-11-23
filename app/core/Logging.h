#pragma once

#include <QString>

namespace ModelBench::Logging {

void install(const QString &logDir);
void setLevel(int level);

} // namespace ModelBench::Logging
