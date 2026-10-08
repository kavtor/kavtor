#pragma once
#include <QJsonObject>
#include <QStringList>
#include <cmath>

// Parameters belong to a logical key, never to its shared input producer.
struct KeyProcessing {
    QString mode = QStringLiteral("linear");
    bool mask = false,invert=false,maskInvert=false;
    double lumaLow=0,lumaHigh=1;
    bool requiresNative() const {return mode=="luma"||invert||(mask&&maskInvert);}
    double left = 0.1, top = 0.1, right = 0.9, bottom = 0.9;
    double hue = 120, width = 0.1, saturation = 0.1, brightness = 0.1;
    double softness = 0.05, spill = 30, spillSaturation = 1;

    QJsonObject toJson() const {
        return {{"mode",mode},{"mask",mask},{"invert",invert},{"maskInvert",maskInvert},{"lumaLow",lumaLow},{"lumaHigh",lumaHigh},{"left",left},{"top",top},
            {"right",right},{"bottom",bottom},{"hue",hue},{"width",width},
            {"saturation",saturation},{"brightness",brightness},{"softness",softness},
            {"spill",spill},{"spillSaturation",spillSaturation}};
    }
    // Apply a patch atomically: malformed or unsupported fields change nothing.
    bool update(const QJsonObject& obj, QString* error = nullptr) {
        KeyProcessing next = *this;
        auto fail = [&] { if(error)*error=QStringLiteral("Invalid or unsupported key processing parameters"); return false; };
        if(obj.isEmpty()) return fail();
        const auto known=toJson();
        for(auto it=obj.begin();it!=obj.end();++it) if(!known.contains(it.key()))return fail();
        if(obj.contains("mode")) {
            if(!obj.value("mode").isString())return fail();
            next.mode=obj.value("mode").toString();
            if(next.mode!="linear"&&next.mode!="chroma"&&next.mode!="luma")return fail();
        }
        if(obj.contains("mask")) {if(!obj.value("mask").isBool())return fail();next.mask=obj.value("mask").toBool();}
        for(const auto& name:QStringList{"invert","maskInvert"})if(obj.contains(name)){if(!obj.value(name).isBool())return fail();(name=="invert"?next.invert:next.maskInvert)=obj.value(name).toBool();}
        auto number=[&](const char* name,double& value,double max) {
            if(!obj.contains(name))return true;
            const auto v=obj.value(name);
            if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<0||v.toDouble()>max)return false;
            value=v.toDouble();return true;
        };
        if(!number("lumaLow",next.lumaLow,1)||!number("lumaHigh",next.lumaHigh,1)||next.lumaLow>=next.lumaHigh||!number("left",next.left,1)||!number("top",next.top,1)||!number("right",next.right,1)
            ||!number("bottom",next.bottom,1)||!number("hue",next.hue,360)||!number("width",next.width,1)
            ||!number("saturation",next.saturation,1)||!number("brightness",next.brightness,1)
            ||!number("softness",next.softness,1)||!number("spill",next.spill,180)
            ||!number("spillSaturation",next.spillSaturation,1)
            ||next.left>=next.right||next.top>=next.bottom)return fail();
        *this=next;return true;
    }
    QStringList commands(int channel,int layer,bool native=false) const {
        const QString target=QStringLiteral("MIXER %1-%2 ").arg(channel).arg(layer);
        QString chroma=target+QStringLiteral("CHROMA 0");
        if(mode=="chroma")chroma=target+QStringLiteral("CHROMA 1 %1 %2 %3 %4 %5 %6 %7 0")
            .arg(hue,0,'f',4).arg(width,0,'f',4).arg(saturation,0,'f',4)
            .arg(brightness,0,'f',4).arg(softness,0,'f',4).arg(spill,0,'f',4).arg(spillSaturation,0,'f',4);
        const bool geometryMask=mask&&!native&&!requiresNative();
        const QString clip=target+QStringLiteral("CLIP %1 %2 %3 %4")
            .arg(geometryMask?left:0,0,'f',4).arg(geometryMask?top:0,0,'f',4)
            .arg(geometryMask?right-left:1,0,'f',4).arg(geometryMask?bottom-top:1,0,'f',4);
        QStringList result{chroma,clip};
        if(native||requiresNative())result.append(target+QString("ALPHAKEY %1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
            .arg(mode=="luma"?"LUMA":mask||invert?"LINEAR":"OFF").arg(lumaLow,0,'f',6).arg(lumaHigh,0,'f',6)
            .arg(invert?1:0).arg(mask?1:0).arg(maskInvert?1:0).arg(left,0,'f',6).arg(top,0,'f',6).arg(right,0,'f',6).arg(bottom,0,'f',6));
        return result;
    }
};
