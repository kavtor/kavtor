#include "Transition.h"

#include <QtCore/QHash>
#include <algorithm>

namespace {

struct Alias {
    const char* id;
    const char* family;
    bool reverse = false;
};

WipePattern geometric(const QString& id,
                      const QString& label,
                      TransitionType type,
                      TransitionDirection forward,
                      TransitionDirection reverse)
{
    WipePattern pattern;
    pattern.id = id;
    pattern.label = label;
    pattern.type = type;
    pattern.direction = forward;
    pattern.reverseDirection = reverse;
    return pattern;
}

WipePattern smil(int code, const char* type, const char* subtype, const char* title, int reverseCode = 0)
{
    WipePattern pattern;
    pattern.id = QStringLiteral("smil_%1_%2").arg(QLatin1String(type), QLatin1String(subtype));
    QString titled = QString::fromLatin1(title);
    if (!titled.isEmpty()) {
        titled[0] = titled[0].toUpper();
    }
    if (reverseCode > 0) {
        pattern.label = QStringLiteral("SMPTE %1/%2 — %3").arg(code).arg(reverseCode).arg(titled);
    } else {
        pattern.label = code > 0 ? QStringLiteral("SMPTE %1 — %2").arg(code).arg(titled) : QStringLiteral("kavtor — %1").arg(titled);
    }
    pattern.type = TransitionType::Smil;
    pattern.smilType = QString::fromLatin1(type);
    pattern.smilSubtype = QString::fromLatin1(subtype);
    pattern.smpte = code;
    pattern.smpteReverse = reverseCode;
    return pattern;
}

const Alias* aliases()
{
    static const Alias kAliases[] = {
        {"wipe_from_left", "wipe_horizontal", false},
        {"wipe_from_right", "wipe_horizontal", true},
        {"wipe_from_top", "wipe_vertical", false},
        {"wipe_from_bottom", "wipe_vertical", true},
        {"push_from_left", "push_horizontal", false},
        {"push_from_right", "push_horizontal", true},
        {"push_from_top", "push_vertical", false},
        {"push_from_bottom", "push_vertical", true},
        {"slide_from_left", "slide_horizontal", false},
        {"slide_from_right", "slide_horizontal", true},
        {"slide_from_top", "slide_vertical", false},
        {"slide_from_bottom", "slide_vertical", true},
        {"smil_fourBoxWipe_cornersOut", "smil_fourBoxWipe_cornersIn", true},
        {"smil_boxWipe_bottomCenter", "smil_boxWipe_topCenter", true},
        {"smil_boxWipe_leftCenter", "smil_boxWipe_rightCenter", true},
        {"smil_veeWipe_up", "smil_veeWipe_down", true},
        {"smil_veeWipe_right", "smil_veeWipe_left", true},
        {"smil_barnVeeWipe_up", "smil_barnVeeWipe_down", true},
        {"smil_barnVeeWipe_right", "smil_barnVeeWipe_left", true},
        {"smil_triangleWipe_down", "smil_triangleWipe_up", true},
        {"smil_triangleWipe_left", "smil_triangleWipe_right", true},
        {"smil_arrowHeadWipe_down", "smil_arrowHeadWipe_up", true},
        {"smil_arrowHeadWipe_left", "smil_arrowHeadWipe_right", true},
        {"smil_pentagonWipe_down", "smil_pentagonWipe_up", true},
        {"smil_doubleFanWipe_fanInVertical", "smil_doubleFanWipe_fanOutVertical", true},
        {"smil_doubleFanWipe_fanInHorizontal", "smil_doubleFanWipe_fanOutHorizontal", true},
        {"smil_fanWipe_bottom", "smil_fanWipe_top", true},
        {"smil_fanWipe_left", "smil_fanWipe_right", true},
        {"smil_saloonDoorWipe_bottom", "smil_saloonDoorWipe_top", true},
        {"smil_saloonDoorWipe_right", "smil_saloonDoorWipe_left", true},
        {"smil_snakeWipe_bottomRightDiagonal", "smil_snakeWipe_topLeftDiagonal", true},
        {"smil_snakeWipe_bottomLeftDiagonal", "smil_snakeWipe_topRightDiagonal", true},
        {"smil_spiralWipe_topLeftCounterClockwise", "smil_spiralWipe_topLeftClockwise", true},
        {"smil_spiralWipe_topRightCounterClockwise", "smil_spiralWipe_topRightClockwise", true},
        {"smil_spiralWipe_bottomRightCounterClockwise", "smil_spiralWipe_bottomRightClockwise", true},
        {"smil_spiralWipe_bottomLeftCounterClockwise", "smil_spiralWipe_bottomLeftClockwise", true},
        {"smil_parallelSnakesWipe_verticalBottomSame", "smil_parallelSnakesWipe_verticalTopSame", true},
        {"smil_parallelSnakesWipe_verticalBottomLeftOpposite", "smil_parallelSnakesWipe_verticalTopLeftOpposite", true},
        {"smil_parallelSnakesWipe_horizontalRightSame", "smil_parallelSnakesWipe_horizontalLeftSame", true},
        {"smil_parallelSnakesWipe_horizontalTopRightOpposite", "smil_parallelSnakesWipe_horizontalTopLeftOpposite", true},
        {"smil_parallelSnakesWipe_diagonalTopLeftOpposite", "smil_parallelSnakesWipe_diagonalBottomLeftOpposite", true},
        {"smil_boxSnakesWipe_twoBoxBottom", "smil_boxSnakesWipe_twoBoxTop", true},
        {"smil_boxSnakesWipe_twoBoxRight", "smil_boxSnakesWipe_twoBoxLeft", true},
        {"smil_waterfallWipe_verticalRight", "smil_waterfallWipe_verticalLeft", true},
        {"smil_waterfallWipe_horizontalRight", "smil_waterfallWipe_horizontalLeft", true},
        {nullptr, nullptr, false},
    };
    return kAliases;
}

const Alias* findAlias(const QString& id)
{
    for (const Alias* alias = aliases(); alias->id; ++alias) {
        if (id == QLatin1String(alias->id)) {
            return alias;
        }
    }
    return nullptr;
}

} // namespace

QVector<WipePattern> builtinWipePatterns()
{
    QVector<WipePattern> patterns{
        geometric(QStringLiteral("wipe_horizontal"), QStringLiteral("Wipe horizontal"), TransitionType::Wipe, TransitionDirection::FromLeft, TransitionDirection::FromRight),
        geometric(QStringLiteral("wipe_vertical"), QStringLiteral("Wipe vertical"), TransitionType::Wipe, TransitionDirection::FromTop, TransitionDirection::FromBottom),
        geometric(QStringLiteral("push_horizontal"), QStringLiteral("Push horizontal"), TransitionType::Push, TransitionDirection::FromLeft, TransitionDirection::FromRight),
        geometric(QStringLiteral("push_vertical"), QStringLiteral("Push vertical"), TransitionType::Push, TransitionDirection::FromTop, TransitionDirection::FromBottom),
        geometric(QStringLiteral("slide_horizontal"), QStringLiteral("Slide horizontal"), TransitionType::Slide, TransitionDirection::FromLeft, TransitionDirection::FromRight),
        geometric(QStringLiteral("slide_vertical"), QStringLiteral("Slide vertical"), TransitionType::Slide, TransitionDirection::FromTop, TransitionDirection::FromBottom),

        smil(0, "kavtorWipe", "checker", "checkerboard iris"),
        smil(0, "kavtorWipe", "blinds", "venetian blinds"),
        smil(0, "kavtorWipe", "circles", "circle mosaic"),
        smil(1, "barWipe", "leftToRight", "bar"),
        smil(2, "barWipe", "topToBottom", "bar vertical"),
        smil(3, "boxWipe", "topLeft", "box top left"),
        smil(4, "boxWipe", "topRight", "box top right"),
        smil(5, "boxWipe", "bottomRight", "box bottom right"),
        smil(6, "boxWipe", "bottomLeft", "box bottom left"),
        smil(7, "fourBoxWipe", "cornersIn", "four box", 8),
        smil(21, "barnDoorWipe", "vertical", "barn door vertical"),
        smil(22, "barnDoorWipe", "horizontal", "barn door horizontal"),
        smil(23, "boxWipe", "topCenter", "box from top/bottom", 25),
        smil(24, "boxWipe", "rightCenter", "box from side", 26),
        smil(41, "diagonalWipe", "topLeft", "diagonal"),
        smil(42, "diagonalWipe", "topRight", "diagonal opposite"),
        smil(43, "bowTieWipe", "vertical", "bow tie vertical"),
        smil(44, "bowTieWipe", "horizontal", "bow tie horizontal"),
        smil(45, "barnDoorWipe", "diagonalBottomLeft", "barn door diagonal BL"),
        smil(46, "barnDoorWipe", "diagonalTopLeft", "barn door diagonal TL"),
        smil(47, "miscDiagonalWipe", "doubleBarnDoor", "double barn door"),
        smil(48, "miscDiagonalWipe", "doubleDiamond", "double diamond"),
        smil(61, "veeWipe", "down", "vee vertical", 63),
        smil(62, "veeWipe", "left", "vee horizontal", 64),
        smil(65, "barnVeeWipe", "down", "barn vee vertical", 67),
        smil(66, "barnVeeWipe", "left", "barn vee horizontal", 68),
        smil(71, "zigZagWipe", "leftToRight", "zigzag"),
        smil(72, "zigZagWipe", "topToBottom", "zigzag vertical"),
        smil(73, "barnZigZagWipe", "vertical", "barn zigzag vertical"),
        smil(74, "barnZigZagWipe", "horizontal", "barn zigzag horizontal"),

        smil(0, "sonyWipe", "pattern13", "Sony 13 — Triangle from left"),
        smil(0, "sonyWipe", "pattern14", "Sony 14 — Triangle from right"),
        smil(0, "sonyWipe", "pattern15", "Sony 15 — Triangle from top"),
        smil(0, "sonyWipe", "pattern16", "Sony 16 — Triangle from bottom"),
        smil(0, "sonyWipe", "pattern19", "Sony 19 — Double horizontal triangle"),
        smil(0, "sonyWipe", "pattern20", "Sony 20 — Double vertical triangle"),
        smil(0, "sonyWipe", "pattern26", "Sony 26 — Heart iris"),
        smil(0, "sonyWipe", "pattern27", "Sony 27 — Sharp five-point star"),
        smil(0, "sonyWipe", "pattern29", "Sony 29 — Arrow iris"),
        smil(0, "sonyWipe", "pattern49", "Sony 49 — Regular polygon iris"),
        smil(0, "sonyWipe", "pattern300", "Sony 300 — Rounded top-left box"),
        smil(0, "sonyWipe", "pattern301", "Sony 301 — Rounded top-right box"),
        smil(0, "sonyWipe", "pattern302", "Sony 302 — Rounded bottom-right box"),
        smil(0, "sonyWipe", "pattern303", "Sony 303 — Rounded bottom-left box"),
        smil(0, "sonyWipe", "pattern304", "Sony 304 — Rounded rectangle iris"),
        smil(0, "sonyWipe", "pattern100", "Sony 100 — Corner sweep bottom left"),
        smil(0, "sonyWipe", "pattern101", "Sony 101 — Corner sweep top left"),
        smil(0, "sonyWipe", "pattern102", "Sony 102 — Corner sweep top right"),
        smil(0, "sonyWipe", "pattern103", "Sony 103 — Corner sweep bottom right"),
        smil(0, "sonyWipe", "pattern104", "Sony 104 — Clock from 12"),
        smil(0, "sonyWipe", "pattern105", "Sony 105 — Clock from 3"),
        smil(0, "sonyWipe", "pattern106", "Sony 106 — Clock from 6"),
        smil(0, "sonyWipe", "pattern107", "Sony 107 — Clock from 9"),
        smil(0, "sonyWipe", "pattern150", "Sony 150 — Double fan right pivot"),
        smil(0, "sonyWipe", "pattern151", "Sony 151 — Double fan left pivot"),
        smil(0, "sonyWipe", "pattern156", "Sony 156 — Central fan from 12"),
        smil(0, "sonyWipe", "pattern158", "Sony 158 — Two-blade pinwheel from 12"),
        smil(0, "sonyWipe", "pattern160", "Sony 160 — Two-blade pinwheel from 3"),
        smil(0, "sonyWipe", "pattern162", "Sony 162 — Four-blade pinwheel"),
        smil(0, "sonyWipe", "pattern516", "Sony 516 — Top-centre fan"),
        smil(0, "sonyWipe", "pattern518", "Sony 518 — Bottom-centre fan"),
        smil(0, "sonyWipe", "pattern604", "Sony 604 — Double fan bottom pivot"),
        smil(0, "sonyWipe", "pattern606", "Sony 606 — Double fan top pivot"),
        smil(0, "sonyWipe", "pattern624", "Sony 624 — Central fan from 9"),
        smil(0, "sonyWipe", "pattern661", "Sony 661 — Opposed fans from 9 and 3"),
        smil(0,"sonyWipe","pattern200","Sony 200 — Horizontal snake top left"),
        smil(0,"sonyWipe","pattern201","Sony 201 — Vertical snake top left"),
        smil(0,"sonyWipe","pattern202","Sony 202 — Diagonal snake top left"),
        smil(0,"sonyWipe","pattern203","Sony 203 — Diagonal snake top right"),
        smil(0,"sonyWipe","pattern206","Sony 206 — Spiral clockwise top left"),
        smil(0,"sonyWipe","pattern207","Sony 207 — Spiral clockwise top right"),
        smil(0,"sonyWipe","pattern208","Sony 208 — Spiral clockwise bottom right"),
        smil(0,"sonyWipe","pattern209","Sony 209 — Spiral clockwise bottom left"),
        smil(0,"sonyWipe","pattern210","Sony 210 — Spiral counterclockwise top left"),
        smil(0,"sonyWipe","pattern211","Sony 211 — Spiral counterclockwise top right"),
        smil(0,"sonyWipe","pattern212","Sony 212 — Spiral counterclockwise bottom right"),
        smil(0,"sonyWipe","pattern213","Sony 213 — Spiral counterclockwise bottom left"),
        smil(0,"sonyWipe","pattern250","Sony 250 — Double vertical snake top"),
        smil(0,"sonyWipe","pattern251","Sony 251 — Double vertical snake bottom"),
        smil(0,"sonyWipe","pattern252","Sony 252 — Opposed vertical snake TL/BR"),
        smil(0,"sonyWipe","pattern253","Sony 253 — Opposed vertical snake BL/TR"),
        smil(0,"sonyWipe","pattern254","Sony 254 — Double horizontal snake left"),
        smil(0,"sonyWipe","pattern255","Sony 255 — Double horizontal snake right"),
        smil(0,"sonyWipe","pattern256","Sony 256 — Opposed horizontal snake TL/BR"),
        smil(0,"sonyWipe","pattern257","Sony 257 — Opposed horizontal snake TR/BL"),
        smil(0,"sonyWipe","pattern260","Sony 260 — Double vertical spiral top"),
        smil(0,"sonyWipe","pattern261","Sony 261 — Double vertical spiral bottom"),
        smil(0,"sonyWipe","pattern262","Sony 262 — Double horizontal spiral left"),
        smil(0,"sonyWipe","pattern263","Sony 263 — Double horizontal spiral right"),
        smil(0,"sonyWipe","pattern264","Sony 264 — Four vertical spirals"),
        smil(0,"sonyWipe","pattern265","Sony 265 — Four horizontal spirals"),
        smil(0,"sonyWipe","pattern266","Sony 266 — Vertical waterfall left"),
        smil(0,"sonyWipe","pattern267","Sony 267 — Vertical waterfall right"),
        smil(0,"sonyWipe","pattern268","Sony 268 — Horizontal waterfall top"),
        smil(0,"sonyWipe","pattern269","Sony 269 — Horizontal waterfall bottom"),



        smil(0, "sonyWipe", "cross", "Sony 22 — central cross"),
        smil(101, "irisWipe", "rectangle", "iris rectangle"),
        smil(102, "irisWipe", "diamond", "iris diamond"),
        smil(103, "triangleWipe", "up", "triangle vertical", 105),
        smil(104, "triangleWipe", "right", "triangle horizontal", 106),
        smil(107, "arrowHeadWipe", "up", "arrowhead vertical", 109),
        smil(108, "arrowHeadWipe", "right", "arrowhead horizontal", 110),
        smil(111, "pentagonWipe", "up", "pentagon", 112),
        smil(113, "hexagonWipe", "horizontal", "hexagon horizontal"),
        smil(114, "hexagonWipe", "vertical", "hexagon vertical"),
        smil(119, "ellipseWipe", "circle", "ellipse circle"),
        smil(120, "ellipseWipe", "horizontal", "ellipse horizontal"),
        smil(121, "ellipseWipe", "vertical", "ellipse vertical"),
        smil(122, "eyeWipe", "horizontal", "eye horizontal"),
        smil(123, "eyeWipe", "vertical", "eye vertical"),
        smil(124, "roundRectWipe", "horizontal", "round rect horizontal"),
        smil(125, "roundRectWipe", "vertical", "round rect vertical"),
        smil(127, "starWipe", "fourPoint", "star 4-point"),
        smil(128, "starWipe", "fivePoint", "star 5-point"),
        smil(129, "starWipe", "sixPoint", "star 6-point"),
        smil(130, "miscShapeWipe", "heart", "heart"),
        smil(131, "miscShapeWipe", "keyhole", "keyhole"),

        smil(201, "clockWipe", "clockwiseTwelve", "clock twelve"),
        smil(202, "clockWipe", "clockwiseThree", "clock three"),
        smil(203, "clockWipe", "clockwiseSix", "clock six"),
        smil(204, "clockWipe", "clockwiseNine", "clock nine"),
        smil(205, "pinWheelWipe", "twoBladeVertical", "pinwheel 2-blade vertical"),
        smil(206, "pinWheelWipe", "twoBladeHorizontal", "pinwheel 2-blade horizontal"),
        smil(207, "pinWheelWipe", "fourBlade", "pinwheel 4-blade"),
        smil(211, "fanWipe", "centerTop", "fan center top"),
        smil(212, "fanWipe", "centerRight", "fan center right"),
        smil(213, "doubleFanWipe", "fanOutVertical", "double fan vertical", 235),
        smil(214, "doubleFanWipe", "fanOutHorizontal", "double fan horizontal", 236),
        smil(221, "singleSweepWipe", "clockwiseTop", "single sweep top"),
        smil(222, "singleSweepWipe", "clockwiseRight", "single sweep right"),
        smil(223, "singleSweepWipe", "clockwiseBottom", "single sweep bottom"),
        smil(224, "singleSweepWipe", "clockwiseLeft", "single sweep left"),
        smil(225, "doubleSweepWipe", "parallelVertical", "double sweep parallel vertical"),
        smil(226, "doubleSweepWipe", "parallelDiagonal", "double sweep parallel diagonal"),
        smil(227, "doubleSweepWipe", "oppositeVertical", "double sweep opposite vertical"),
        smil(228, "doubleSweepWipe", "oppositeHorizontal", "double sweep opposite horizontal"),
        smil(231, "fanWipe", "top", "fan top/bottom", 233),
        smil(232, "fanWipe", "right", "fan side", 234),
        smil(241, "singleSweepWipe", "clockwiseTopLeft", "single sweep top left"),
        smil(242, "singleSweepWipe", "counterClockwiseBottomLeft", "single sweep CCW bottom left"),
        smil(243, "singleSweepWipe", "clockwiseBottomRight", "single sweep bottom right"),
        smil(244, "singleSweepWipe", "counterClockwiseTopRight", "single sweep CCW top right"),
        smil(245, "doubleSweepWipe", "parallelDiagonalTopLeft", "double sweep diagonal TL"),
        smil(246, "doubleSweepWipe", "parallelDiagonalBottomLeft", "double sweep diagonal BL"),
        smil(251, "saloonDoorWipe", "top", "saloon door vertical", 253),
        smil(252, "saloonDoorWipe", "left", "saloon door horizontal", 254),
        smil(261, "windshieldWipe", "right", "windshield right"),
        smil(262, "windshieldWipe", "up", "windshield up"),
        smil(263, "windshieldWipe", "vertical", "windshield vertical"),
        smil(264, "windshieldWipe", "horizontal", "windshield horizontal"),

        smil(301, "snakeWipe", "topLeftHorizontal", "snake horizontal"),
        smil(302, "snakeWipe", "topLeftVertical", "snake vertical"),
        smil(303, "snakeWipe", "topLeftDiagonal", "snake diagonal", 305),
        smil(304, "snakeWipe", "topRightDiagonal", "snake opposite diagonal", 306),
        smil(310, "spiralWipe", "topLeftClockwise", "spiral TL", 314),
        smil(311, "spiralWipe", "topRightClockwise", "spiral TR", 315),
        smil(312, "spiralWipe", "bottomRightClockwise", "spiral BR", 316),
        smil(313, "spiralWipe", "bottomLeftClockwise", "spiral BL", 317),
        smil(320, "parallelSnakesWipe", "verticalTopSame", "parallel snakes vertical", 321),
        smil(322, "parallelSnakesWipe", "verticalTopLeftOpposite", "parallel snakes vertical opposite", 323),
        smil(324, "parallelSnakesWipe", "horizontalLeftSame", "parallel snakes horizontal", 325),
        smil(326, "parallelSnakesWipe", "horizontalTopLeftOpposite", "parallel snakes horizontal opposite", 327),
        smil(328, "parallelSnakesWipe", "diagonalBottomLeftOpposite", "parallel snakes diagonal opposite", 329),
        smil(340, "boxSnakesWipe", "twoBoxTop", "box snakes two horizontal", 341),
        smil(342, "boxSnakesWipe", "twoBoxLeft", "box snakes two vertical", 343),
        smil(344, "boxSnakesWipe", "fourBoxVertical", "box snakes four vertical"),
        smil(345, "boxSnakesWipe", "fourBoxHorizontal", "box snakes four horizontal"),
        smil(350, "waterfallWipe", "verticalLeft", "waterfall vertical", 351),
        smil(352, "waterfallWipe", "horizontalLeft", "waterfall horizontal", 353),
    };
    for(int code:pendingSonyWipes()){
        auto pattern=smil(0,"sonyWipe",QString("pattern%1").arg(code).toUtf8().constData(),QString("Sony %1 — Pending implementation").arg(code).toUtf8().constData());
        pattern.label=QString("Sony %1 — Pending implementation").arg(code);pattern.implemented=false;patterns.append(pattern);
    }
    return patterns;
}

WipePatternLookup lookupWipePattern(const QString& id)
{
    const QVector<WipePattern> patterns = builtinWipePatterns();
    for (const WipePattern& pattern : patterns) {
        if (pattern.id == id) {
            return {pattern, false};
        }
    }
    if (const Alias* alias = findAlias(id)) {
        for (const WipePattern& pattern : patterns) {
            if (pattern.id == QLatin1String(alias->family)) {
                return {pattern, alias->reverse};
            }
        }
    }
    return {patterns.first(), false};
}

WipePattern wipePatternById(const QString& id)
{
    return lookupWipePattern(id).pattern;
}

bool lookupWipeBySmpte(int code, WipePattern* pattern, bool* inherentReverse)
{
    if (code == 0) {
        if (pattern) {
            *pattern = wipePatternById(QStringLiteral("wipe_horizontal"));
        }
        if (inherentReverse) {
            *inherentReverse = false;
        }
        return true;
    }
    if (code < 0) {
        return false;
    }
    for (const WipePattern& candidate : builtinWipePatterns()) {
        if (candidate.smpte == code) {
            if (pattern) {
                *pattern = candidate;
            }
            if (inherentReverse) {
                *inherentReverse = false;
            }
            return true;
        }
        if (candidate.smpteReverse == code) {
            if (pattern) {
                *pattern = candidate;
            }
            if (inherentReverse) {
                *inherentReverse = true;
            }
            return true;
        }
    }
    return false;
}

QString wipeDirectionModeToString(WipeDirectionMode mode)
{
    switch (mode) {
    case WipeDirectionMode::Reverse:
        return QStringLiteral("rev");
    case WipeDirectionMode::PingPong:
        return QStringLiteral("pingpong");
    case WipeDirectionMode::Forward:
    default:
        return QStringLiteral("fwd");
    }
}

bool wipeDirectionModeFromString(const QString& text, WipeDirectionMode* mode)
{
    if (text == QLatin1String("rev") || text == QLatin1String("reverse")) {
        if (mode) {
            *mode = WipeDirectionMode::Reverse;
        }
        return true;
    }
    if (text == QLatin1String("pingpong") || text == QLatin1String("ping-pong") || text == QLatin1String("ping")) {
        if (mode) {
            *mode = WipeDirectionMode::PingPong;
        }
        return true;
    }
    if (text == QLatin1String("fwd") || text == QLatin1String("forward")) {
        if (mode) {
            *mode = WipeDirectionMode::Forward;
        }
        return true;
    }
    return false;
}

QString wipeEdgeModeToString(WipeEdgeMode mode)
{
    switch (mode) {
    case WipeEdgeMode::Soft:
        return QStringLiteral("soft");
    case WipeEdgeMode::Border:
        return QStringLiteral("border");
    case WipeEdgeMode::Hard:
    default:
        return QStringLiteral("hard");
    }
}

bool wipeEdgeModeFromString(const QString& text, WipeEdgeMode* mode)
{
    if (text == QLatin1String("soft") || text == QLatin1String("feather")) {
        if (mode) {
            *mode = WipeEdgeMode::Soft;
        }
        return true;
    }
    if (text == QLatin1String("border") || text == QLatin1String("color")) {
        if (mode) {
            *mode = WipeEdgeMode::Border;
        }
        return true;
    }
    if (text == QLatin1String("hard") || text == QLatin1String("none")) {
        if (mode) {
            *mode = WipeEdgeMode::Hard;
        }
        return true;
    }
    return false;
}

// Sony codes confirmed from the panel pictograms by the operator. Keep this
// namespace separate from SMPTE; do not guess unverified Sony catalogue IDs.
bool lookupWipeBySony(int code,WipePattern* pattern,bool* inherentReverse)
{
    if(QList<int>{13,14,15,16,19,20,26,27,29,49,300,301,302,303,304,100,101,102,103,104,105,106,107,150,151,156,158,160,162,516,518,604,606,624,661,200,201,202,203,206,207,208,209,210,211,212,213,250,251,252,253,254,255,256,257,260,261,262,263,264,265,266,267,268,269}.contains(code)){if(pattern)*pattern=wipePatternById(QString("smil_sonyWipe_pattern%1").arg(code));if(inherentReverse)*inherentReverse=false;return true;}
    int smpte=-1;bool reverse=false;
    switch(code) {
    case 1:smpte=1;break;
    case 2:smpte=1;reverse=true;break;
    case 3:smpte=2;break;
    case 4:smpte=2;reverse=true;break;
    case 5:smpte=3;break;
    case 6:smpte=4;break;
    case 7:smpte=5;break;
    case 8:smpte=6;break;
    case 9:smpte=41;break;
    case 10:smpte=42;break;
    case 11:smpte=41;reverse=true;break;
    case 12:smpte=42;reverse=true;break;
    case 17:smpte=21;reverse=true;break;
    case 18:smpte=22;reverse=true;break;
    case 21:smpte=101;break;
    case 22:if(pattern)*pattern=wipePatternById("smil_sonyWipe_cross");if(inherentReverse)*inherentReverse=false;return true;
    case 23:smpte=102;break;
    case 24:smpte=119;break;
    default:return false;
    }
    bool ignored=false;
    if(!lookupWipeBySmpte(smpte,pattern,&ignored))return false;
    if(inherentReverse)*inherentReverse=reverse;
    return true;
}

QList<int> supportedSonyWipes(bool expanded,bool enhanced,bool rotary,bool mosaic,bool compound) {
    if(compound){auto codes=supportedSonyWipes(expanded,enhanced,rotary,mosaic,false);codes.append(QList<int>{250,251,252,253,254,255,256,257,260,261,262,263,264,265,266,267,268,269});return codes;}
    if(mosaic){auto codes=supportedSonyWipes(expanded,enhanced,rotary,false);codes.append(QList<int>{200,201,202,203,206,207,208,209,210,211,212,213});return codes;}
    if(rotary){auto codes=supportedSonyWipes(expanded,enhanced,false);codes.append(QList<int>{150,151,156,158,160,162,516,518,604,606,624,661});return codes;}

    if(enhanced)return {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,26,27,29,49,100,101,102,103,104,105,106,107,300,301,302,303,304};
    return expanded ? QList<int>{1,2,3,4,5,6,7,8,9,10,11,12,17,18,21,22,23,24}
                    : QList<int>{1,3,5,6,9,17,18,21,23,24};
}
QList<int> supportedSonyDmes(bool primitives,bool spatial,bool planar,bool mirror,bool frame) {
    if(!primitives)return {1001,1002,1003,1004,2601,2602,2603,2604};
    QList<int> codes;for(auto range:QList<QPair<int,int>>{{1001,1008},{1011,1013},{1021,1031},{1041,1044},{1384,1385},{2601,2608},{2621,2628}})for(int code=range.first;code<=range.second;++code)codes.append(code);if(spatial)codes.append(QList<int>{1045,1046,1047,1048,1101,1102,1103,1104,1121,1122});if(planar){for(int code=1051;code<=1058;++code)codes.append(code);for(int code=1061;code<=1064;++code)codes.append(code);codes.append(1068);}if(mirror)for(int code=1355;code<=1358;++code)codes.append(code);if(frame)codes.append(1201);std::sort(codes.begin(),codes.end());return codes;
}
bool lookupDmeBySony(int code, QString* effect, QString* direction,bool primitives,bool spatial,bool planar,bool mirror,bool frame) {
    if (!supportedSonyDmes(primitives,spatial,planar,mirror,frame).contains(code)) return false;
    if(primitives){if(effect)*effect="sony";if(direction)*direction=QString::number(code);return true;}
    if (effect) *effect = code < 2000 ? QStringLiteral("slide") : QStringLiteral("push");
    const QStringList entries = {"left","right","top","bottom"};
    if (direction) *direction = entries[(code % 100) - 1];
    return true;
}

QList<int> pendingSonyWipes(){
    // TODO: 220-223 require karaoke row phasing; 224-247 need decoded paths;
    // 270-272 need identified preset masks; 273 needs volatility, 274 flash rate.
    QList<int> codes;for(int code=220;code<=247;++code)codes.append(code);
    for(int code=270;code<=274;++code)codes.append(code);return codes;
}

QList<int> pendingSonyDmes(bool spatial,bool planar,bool mirror,bool frame){
    // TODO: individually reviewed reasons live in docs/sony-dme-inventory.json.
    // IDs are reserved, not advertised as executable presets.
    QList<int> codes{1032,1033,1045,1046,1047,1048,1051,1052,1053,1054,1055,1056,1057,1058,1061,1062,1063,1064,1068,1071,1072,1074,1076,1077,1088,1091,1092,1093,1094,1101,1102,1103,1104,1109,1110,1121,1122,1124,1131,1132,1133,1135,1201,1202,1203,1204,1205,1206,1207,1208,1209,1221,1222,1223,1224,1225,1251,1301,1302,1303,1304,1305,1306,1307,1308,1309,1310,1311,1312,1313,1315,1316,1317,1318,1321,1322,1323,1324,1325,1326,1327,1328,1329,1330,1331,1332,1333,1335,1336,1337,1338,1341,1342,1343,1344,1345,1346,1347,1348,1349,1350,1355,1356,1357,1358,1365,1371,1372,1378,1379,1381,1386,1387,1388,1389,1391,1393,1394,1396,1398,1399,1701,1702,2631,2632,2633,2634,2642,2644,2651,2652,2661,2662,2701,2702,2703,2704,2705,2706,2707,2708,2709,2710,2711,2712,2713,2715,2716,2717,2718,2721,2722,2723,2724,2725,2726,2727,2728,2729,2730,2731,2732,2733,2735,2736,2737,2738,2741,2742,2743,2744,2745,2746,2747,2748,2749,2750,2801,2802,2803,2804,2811,2812,2813,2814,2851,2852,2853,2854,2861,2862,2863,2864,3601,7001,7002,7003,7004,7005,7006,7007,7008,7021,7022,7023,7024,7025,7026,7027,7028,7029,7030,7031,7201,7202,7203,7204,7205,7206,7207,7208,7221,7222,7223,7224};
    if(spatial)for(int code:QList<int>{1045,1046,1047,1048,1101,1102,1103,1104,1121,1122})codes.removeAll(code);
    if(planar){for(int code=1051;code<=1058;++code)codes.removeAll(code);for(int code=1061;code<=1064;++code)codes.removeAll(code);codes.removeAll(1068);}
    if(mirror)for(int code=1355;code<=1358;++code)codes.removeAll(code);
    if(frame)codes.removeAll(1201);
    return codes;
}
