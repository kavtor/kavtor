#pragma once
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QList>
#include <QtCore/QRectF>
#include <QtCore/QStringList>
#include <functional>
#include <array>
#include <QtCore/QPointF>

struct SuperSourceBox {
    QString id;
    QString name;
    QString kind = QStringLiteral("input");
    int input = -1;
    QString image;
    QRectF rect{0, 0, 1, 1};
    QRectF crop{0, 0, 1, 1};
    std::array<QPointF,4> corners{{{0,0},{1,0},{1,1},{0,1}}}; // Box-local UL, UR, LR, LL.
    double zoom = 1, centerX = .5, centerY = .5;
    double imageAspect = 16. / 9;
    bool cover = true, audio = false, keepAspect = false;
    int keyButton = -1; // KEY crosspoint 0..23, or no hardware mapping.
};
struct SuperSourceLayout {
    QString id, name;
    QString background = QStringLiteral("#000000");
    QList<SuperSourceBox> boxes; // Array order is back to front.
};
QJsonArray superSourcesJson(const QList<SuperSourceLayout> &layouts);
bool parseSuperSources(const QJsonArray &, QList<SuperSourceLayout> *, QString *error = nullptr);
QString validateSuperSources(const QList<SuperSourceLayout> &layouts);
bool validSuperSourceCorners(const std::array<QPointF,4>& corners);
std::array<QPointF,4> superSourcePerspective(const SuperSourceBox& box);
QRectF superSourceFill(const SuperSourceBox &, double rasterAspect = 16. / 9);
QStringList superSourceCommands(const SuperSourceLayout &, int channel, double rasterAspect,
                                const std::function<QString(int)> &route,
                                const QHash<QString, int> &overrides = {});
