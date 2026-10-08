#include "MoveTransition.h"
#include <algorithm>
#include <cmath>

MoveScene moveSuperSourceScene(const SuperSourceLayout& layout,double aspect,
    const std::function<QString(int)>& identity,const QHash<QString,int>& overrides) {
    MoveScene result;
    MoveElement background; background.identity="color:"+layout.background;
    background.instance=layout.id+":background";background.order=-1;
    if(layout.background!="transparent")result.append(background);
    for(int i=0;i<layout.boxes.size();++i) {
        const auto& box=layout.boxes[i];MoveElement e;
        e.identity=box.kind=="input"?identity(overrides.value(box.id,box.input)):
            box.image.isEmpty()?QString():"image:"+box.image;
        if(e.identity.isEmpty())continue;
        e.instance=box.id;e.fill=superSourceFill(box,aspect);e.clip=box.rect;e.crop=box.crop;e.perspective=superSourcePerspective(box);
        e.volume=box.kind=="input"&&box.audio?1:0;e.order=i+1;result.append(e);
    }
    return result;
}
MovePlan planMove(const MoveScene& from,const MoveScene& to) {
    MovePlan plan;QList<int> matches;matches.fill(-1,from.size());
    QList<bool> used;used.fill(false,to.size());
    // Stable box IDs disambiguate repeated producers in related layouts.
    for(int i=0;i<from.size();++i) {
        if(from[i].identity.isEmpty()||from[i].instance.isEmpty())continue;
        int candidate=-1,count=0,origins=0;
        for(const auto& e:from)if(e.identity==from[i].identity&&e.instance==from[i].instance)++origins;
        for(int j=0;j<to.size();++j)if(!used[j]&&to[j].identity==from[i].identity&&to[j].instance==from[i].instance){candidate=j;++count;}
        if(origins==1&&count==1){matches[i]=candidate;used[candidate]=true;}
    }
    // Match only unique remaining identities; never guess between duplicates.
    for(int i=0;i<from.size();++i)if(matches[i]<0&&!from[i].identity.isEmpty()) {
        int origins=0,count=0,candidate=-1;
        for(int k=0;k<from.size();++k)if(matches[k]<0&&from[k].identity==from[i].identity)++origins;
        for(int j=0;j<to.size();++j)if(!used[j]&&to[j].identity==from[i].identity){candidate=j;++count;}
        if(origins==1&&count==1){matches[i]=candidate;used[candidate]=true;}
    }
    for(int i=0;i<from.size();++i) {
        auto end=matches[i]>=0?to[matches[i]]:from[i];
        if(matches[i]<0){end.opacity=0;end.volume=0;}
        plan.append({from[i],end,matches[i]>=0});
    }
    for(int j=0;j<to.size();++j)if(!used[j]){auto begin=to[j];begin.opacity=0;begin.volume=0;plan.append({begin,to[j],false});}
    return plan;
}
MoveScene sampleMove(const MovePlan& plan,double position) {
    // This is an absolute sample, not a delta: reverse and T-bar scrubbing are safe.
    const double p=std::isfinite(position)?std::clamp(position,0.,1.):0.;
    const auto value=[p](double a,double b){return a+(b-a)*p;};
    const auto rect=[&](QRectF a,QRectF b){return QRectF(value(a.x(),b.x()),value(a.y(),b.y()),value(a.width(),b.width()),value(a.height(),b.height()));};
    MoveScene result;
    for(const auto& track:plan){auto e=p==1?track.to:track.from;e.fill=rect(track.from.fill,track.to.fill);e.clip=rect(track.from.clip,track.to.clip);e.crop=rect(track.from.crop,track.to.crop);e.opacity=value(track.from.opacity,track.to.opacity);e.volume=value(track.from.volume,track.to.volume);e.order=value(track.from.order,track.to.order);for(int i=0;i<4;++i)e.perspective[i]={value(track.from.perspective[i].x(),track.to.perspective[i].x()),value(track.from.perspective[i].y(),track.to.perspective[i].y())};if(e.opacity>0||e.volume>0)result.append(e);}
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.order<b.order;});
    return result;
}
