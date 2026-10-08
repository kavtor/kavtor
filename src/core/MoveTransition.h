#pragma once
#include "SuperSource.h"

// Frozen transition geometry. Identity describes the producer, never a display name.
struct MoveElement {
    QString identity, instance;
    QRectF fill{0,0,1,1}, clip{0,0,1,1}, crop{0,0,1,1};
    std::array<QPointF,4> perspective{{{0,0},{1,0},{1,1},{0,1}}};
    double opacity=1, volume=0;
    double order=0;
};
struct MoveTrack {
    MoveElement from, to;
    bool matched=false;
};
using MoveScene = QList<MoveElement>;
using MovePlan = QList<MoveTrack>;
MoveScene moveSuperSourceScene(const SuperSourceLayout&, double rasterAspect,
    const std::function<QString(int)>& identity, const QHash<QString,int>& overrides={});
MovePlan planMove(const MoveScene& from, const MoveScene& to);
MoveScene sampleMove(const MovePlan&, double position);
