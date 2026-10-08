#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

enum class TransitionType {
    Cut,
    Mix,
    VFade,
    Dip,
    FadeCut,
    CutFade,
    Wipe,
    Push,
    Slide,
    Smil,
    Move,
    Cube,
    Zoom,
    PageCurl,
    PageRoll,
    SonyDme,
    Nam,
    SuperMix,
    DustMix
};

enum class TransitionDirection {
    FromLeft,
    FromRight,
    FromTop,
    FromBottom
};

enum class WipeDirectionMode {
    Forward,
    Reverse,
    PingPong
};

enum class WipeEdgeMode {
    Hard,
    Soft,
    Border
};

struct WipePattern {
    QString id;
    QString label;
    TransitionType type = TransitionType::Wipe;
    TransitionDirection direction = TransitionDirection::FromLeft;
    TransitionDirection reverseDirection = TransitionDirection::FromRight;
    QString smilType;
    QString smilSubtype;
    int smpte = 0;
    int smpteReverse = 0;
    bool implemented = true;
};

struct WipePatternLookup {
    WipePattern pattern;
    bool reverse = false;
};

QVector<WipePattern> builtinWipePatterns();
WipePattern wipePatternById(const QString& id);
WipePatternLookup lookupWipePattern(const QString& id);
bool lookupWipeBySmpte(int code, WipePattern* pattern, bool* inherentReverse);
bool lookupWipeBySony(int code, WipePattern* pattern, bool* inherentReverse);
bool lookupDmeBySony(int code, QString* effect, QString* direction,bool primitives=false,bool spatial=false,bool planar=false,bool mirror=false,bool frame=false);
QList<int> supportedSonyWipes(bool expanded,bool enhanced = false,bool rotary = false,bool mosaic = false,bool compound = false);
QList<int> pendingSonyWipes();
QList<int> supportedSonyDmes(bool primitives=false,bool spatial=false,bool planar=false,bool mirror=false,bool frame=false);
QList<int> pendingSonyDmes(bool spatial=false,bool planar=false,bool mirror=false,bool frame=false);

QString wipeDirectionModeToString(WipeDirectionMode mode);
bool wipeDirectionModeFromString(const QString& text, WipeDirectionMode* mode);
QString wipeEdgeModeToString(WipeEdgeMode mode);
bool wipeEdgeModeFromString(const QString& text, WipeEdgeMode* mode);

inline QString transitionDirectionToken(TransitionDirection direction)
{
    switch (direction) {
    case TransitionDirection::FromRight:
        return QStringLiteral("FROMRIGHT");
    case TransitionDirection::FromTop:
        return QStringLiteral("FROMTOP");
    case TransitionDirection::FromBottom:
        return QStringLiteral("FROMBOTTOM");
    case TransitionDirection::FromLeft:
    default:
        return QStringLiteral("FROMLEFT");
    }
}

struct Transition {
    TransitionType type = TransitionType::Cut;
    int durationFrames = 0;
    TransitionDirection direction = TransitionDirection::FromLeft;
    QString smilType;
    QString smilSubtype;
    bool reverse = false;
    WipeEdgeMode edge = WipeEdgeMode::Hard;
    int edgeAmount = 0;
    int borderAmount = 0;
    int shadowAmount = 0;
    int multi = 1;
    int aspectW = 1;
    int aspectH = 1;
    int posX = 500;
    int posY = 500;
    int vertices = 5,rounding = 15,tileSize = 10;
    QString borderColor;
    int dmeBackground=-2; // -3 static image, -2 captures preset, -1 BLACK, otherwise input/M/E.
    int sonyDmeCode=0;
    int videoGainA=100,videoGainB=100;
    QString dmeBackgroundImage; // Captured with the take; later preset edits cannot alter it.
    QString dipColor; // Empty: snapshot the configured opaque RGB colour when starting.

    static Transition cut()
    {
        return {TransitionType::Cut, 0, TransitionDirection::FromLeft};
    }

    static Transition mix(int frames)
    {
        return {TransitionType::Mix, frames, TransitionDirection::FromLeft};
    }

    static Transition fromWipePattern(const WipePattern& pattern, int frames, bool reverse = false)
    {
        Transition transition;
        transition.type = pattern.type;
        transition.durationFrames = frames;
        transition.direction = reverse ? pattern.reverseDirection : pattern.direction;
        transition.smilType = pattern.smilType;
        transition.smilSubtype = pattern.smilSubtype;
        transition.reverse = reverse;
        return transition;
    }

    int borderSide=0,innerSoft=-1,outerSoft=-1;
    int dustRatio=50,dustSize=2,dustFlash=0;
    bool alternateMix() const { return type==TransitionType::VFade||type==TransitionType::Dip||type==TransitionType::FadeCut||type==TransitionType::CutFade; }
    static bool namedMix(const QString& mode,int frames,Transition* result) {
        Transition t=Transition::mix(frames);
        if(mode=="dustmix")t.type=TransitionType::DustMix;
        else if(mode=="nam")t.type=TransitionType::Nam;
        else if(mode=="supermix")t.type=TransitionType::SuperMix;
        else if(mode=="dip")t.type=TransitionType::Dip;
        else if(mode=="vfade")t.type=TransitionType::VFade;
        else if(mode=="fadecut")t.type=TransitionType::FadeCut;
        else if(mode=="cutfade")t.type=TransitionType::CutFade;
        else if(mode!="mix")return false;
        *result=t;return true;
    }
    QString typeName() const
    {
        switch (type) {
        case TransitionType::Cut:
            return QStringLiteral("cut");
        case TransitionType::Mix:
            return QStringLiteral("mix");
        case TransitionType::Nam: return QStringLiteral("nam");
        case TransitionType::DustMix: return QStringLiteral("dustmix");
        case TransitionType::SuperMix: return QStringLiteral("supermix");
        case TransitionType::Dip: return QStringLiteral("dip");
        case TransitionType::VFade: return QStringLiteral("vfade");
        case TransitionType::FadeCut: return QStringLiteral("fadecut");
        case TransitionType::CutFade: return QStringLiteral("cutfade");
        case TransitionType::Wipe:
            return QStringLiteral("wipe");
        case TransitionType::Push:
            return QStringLiteral("push");
        case TransitionType::Slide:
            return QStringLiteral("slide");
        case TransitionType::Smil:
            return QStringLiteral("smil");
        }
        return QStringLiteral("cut");
    }

    QString amcpSuffix() const
    {
        if (durationFrames <= 0) {
            return {};
        }
        switch (type) {
        case TransitionType::Mix:
            return QStringLiteral(" MIX %1").arg(durationFrames);
        case TransitionType::Dip:
        case TransitionType::VFade: return QStringLiteral(" VFADE %1").arg(durationFrames);
        case TransitionType::FadeCut: return QStringLiteral(" FADECUT %1").arg(durationFrames);
        case TransitionType::CutFade: return QStringLiteral(" CUTFADE %1").arg(durationFrames);
        case TransitionType::Wipe:
            return QStringLiteral(" WIPE %1 %2").arg(durationFrames).arg(transitionDirectionToken(direction));
        case TransitionType::Push:
            return QStringLiteral(" PUSH %1 %2").arg(durationFrames).arg(transitionDirectionToken(direction));
        case TransitionType::Slide:
            return QStringLiteral(" SLIDE %1 %2").arg(durationFrames).arg(transitionDirectionToken(direction));
        case TransitionType::Move:
        case TransitionType::Cube:
        case TransitionType::Zoom:
        case TransitionType::PageCurl:
        case TransitionType::PageRoll:
        case TransitionType::Cut:
            break;
        }
        return {};
    }
};
