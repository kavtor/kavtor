#pragma once
#include "OscListener.h"
#include <QtCore/QJsonObject>
class Configuration;
struct NativeMultiview {
    QStringList names;
    QVector<bool> vacant;
    QVector<OscFileTime> clocks;
    QVector<QVector<int>> meters;
    QJsonObject system;
    int bank = 0, preview = -1, program = -1, armed = -1, cued = -1, me = 1;
    bool transportIcons=false;
    bool both = false, ready = false;
    QJsonObject scene(const Configuration &configuration) const;
};
