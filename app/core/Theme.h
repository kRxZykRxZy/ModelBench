#pragma once

#include <QObject>
#include <QColor>
#include <QString>

namespace ModelFit {

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor bg READ bg CONSTANT)
    Q_PROPERTY(QColor bgElevated READ bgElevated CONSTANT)
    Q_PROPERTY(QColor panel READ panel CONSTANT)
    Q_PROPERTY(QColor panelAlt READ panelAlt CONSTANT)
    Q_PROPERTY(QColor border READ border CONSTANT)
    Q_PROPERTY(QColor borderStrong READ borderStrong CONSTANT)
    Q_PROPERTY(QColor text READ text CONSTANT)
    Q_PROPERTY(QColor textMuted READ textMuted CONSTANT)
    Q_PROPERTY(QColor textSubtle READ textSubtle CONSTANT)
    Q_PROPERTY(QColor accent READ accent CONSTANT)
    Q_PROPERTY(QColor accentHover READ accentHover CONSTANT)
    Q_PROPERTY(QColor accentPressed READ accentPressed CONSTANT)
    Q_PROPERTY(QColor success READ success CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)
    Q_PROPERTY(QColor danger READ danger CONSTANT)
    Q_PROPERTY(QColor info READ info CONSTANT)
    Q_PROPERTY(int sp1 READ sp1 CONSTANT)
    Q_PROPERTY(int sp2 READ sp2 CONSTANT)
    Q_PROPERTY(int sp3 READ sp3 CONSTANT)
    Q_PROPERTY(int sp4 READ sp4 CONSTANT)
    Q_PROPERTY(int sp5 READ sp5 CONSTANT)
    Q_PROPERTY(int sp6 READ sp6 CONSTANT)
    Q_PROPERTY(int sp8 READ sp8 CONSTANT)
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT)
    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT)
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT)
    Q_PROPERTY(int radiusXl READ radiusXl CONSTANT)
    Q_PROPERTY(QString fontSans READ fontSans CONSTANT)
    Q_PROPERTY(QString fontMono READ fontMono CONSTANT)
    Q_PROPERTY(int fontSizeXs READ fontSizeXs CONSTANT)
    Q_PROPERTY(int fontSizeSm READ fontSizeSm CONSTANT)
    Q_PROPERTY(int fontSizeMd READ fontSizeMd CONSTANT)
    Q_PROPERTY(int fontSizeLg READ fontSizeLg CONSTANT)
    Q_PROPERTY(int fontSizeXl READ fontSizeXl CONSTANT)
    Q_PROPERTY(int fontSize2xl READ fontSize2xl CONSTANT)
    Q_PROPERTY(bool dark READ dark CONSTANT)
public:
    explicit Theme(QObject *parent = nullptr);

    QColor bg() const;
    QColor bgElevated() const;
    QColor panel() const;
    QColor panelAlt() const;
    QColor border() const;
    QColor borderStrong() const;
    QColor text() const;
    QColor textMuted() const;
    QColor textSubtle() const;
    QColor accent() const { return QColor("#5B6CFF"); }
    QColor accentHover() const { return QColor("#6E7DFF"); }
    QColor accentPressed() const { return QColor("#4A5AE0"); }
    QColor success() const { return QColor("#3FB950"); }
    QColor warning() const { return QColor("#D29922"); }
    QColor danger() const { return QColor("#F85149"); }
    QColor info() const { return QColor("#58A6FF"); }
    int sp1() const { return 4; }
    int sp2() const { return 8; }
    int sp3() const { return 12; }
    int sp4() const { return 16; }
    int sp5() const { return 20; }
    int sp6() const { return 24; }
    int sp8() const { return 32; }
    int radiusSm() const { return 6; }
    int radiusMd() const { return 10; }
    int radiusLg() const { return 14; }
    int radiusXl() const { return 20; }
    QString fontSans() const { return QStringLiteral("Inter, Segoe UI, SF Pro Text, sans-serif"); }
    QString fontMono() const { return QStringLiteral("JetBrains Mono, Consolas, SF Mono, monospace"); }
    int fontSizeXs() const { return 11; }
    int fontSizeSm() const { return 12; }
    int fontSizeMd() const { return 14; }
    int fontSizeLg() const { return 16; }
    int fontSizeXl() const { return 20; }
    int fontSize2xl() const { return 24; }
    bool dark() const { return true; }
};

} // namespace ModelFit
