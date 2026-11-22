#include "Theme.h"

namespace ModelFit {

Theme::Theme(QObject *parent) : QObject(parent) {}

QColor Theme::bg() const { return QColor("#0F1115"); }
QColor Theme::bgElevated() const { return QColor("#161922"); }
QColor Theme::panel() const { return QColor("#1C2029"); }
QColor Theme::panelAlt() const { return QColor("#232834"); }
QColor Theme::border() const { return QColor("#2A2F3A"); }
QColor Theme::borderStrong() const { return QColor("#3A4050"); }
QColor Theme::text() const { return QColor("#E6E8EC"); }
QColor Theme::textMuted() const { return QColor("#9AA0AC"); }
QColor Theme::textSubtle() const { return QColor("#6B7180"); }

} // namespace ModelFit
