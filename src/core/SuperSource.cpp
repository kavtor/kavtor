#include "SuperSource.h"
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <cmath>
namespace {
QJsonArray geometry(QRectF r) { return {r.x(), r.y(), r.width(), r.height()}; }
bool finiteValue(double v) { return std::isfinite(v); }
bool validRect(QRectF r) {
    return finiteValue(r.x()) && finiteValue(r.y()) && finiteValue(r.width()) && finiteValue(r.height()) &&
           r.x() >= 0 && r.y() >= 0 && r.width() >= .001 && r.height() >= .001 && r.right() <= 1.000001 &&
           r.bottom() <= 1.000001;
}
QString quote(QString value) {
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    return '"' + value + '"';
}
QString number(double value) { return QString::number(value, 'f', 6); }
QString rectangle(QRectF r) {
    return number(r.x()) + ' ' + number(r.y()) + ' ' + number(r.width()) + ' ' + number(r.height());
}
} // namespace
QJsonArray superSourcesJson(const QList<SuperSourceLayout> &layouts) {
    QJsonArray result;
    for (const auto &l : layouts) {
        QJsonArray boxes;
        for (const auto &b : l.boxes) {
            QJsonArray perspective;for(const auto& point:b.corners){perspective.append(point.x());perspective.append(point.y());}
            boxes.append(QJsonObject{{"id", b.id},
                                     {"name", b.name},
                                     {"kind", b.kind},
                                     {"input", b.input},
                                     {"image", b.image},
                                     {"rect", geometry(b.rect)},
                                     {"crop", geometry(b.crop)},
                                     {"zoom", b.zoom},
                                     {"centerX", b.centerX},
                                     {"centerY", b.centerY},
                                     {"imageAspect", b.imageAspect},
                                     {"cover", b.cover},
                                     {"audio", b.audio},
                                     {"keepAspect", b.keepAspect},
                                     {"keyButton", b.keyButton},{"corners",perspective}});
        }
        result.append(
            QJsonObject{{"id", l.id}, {"name", l.name}, {"background", l.background}, {"boxes", boxes}});
    }
    return result;
}
QString validateSuperSources(const QList<SuperSourceLayout> &layouts) {
    if (layouts.size() > 24)
        return "At most 24 SuperSource layouts are supported.";
    QSet<QString> ids;
    for (const auto &l : layouts) {
        if (l.id.isEmpty() || l.id.size() > 80 || ids.contains(l.id) || l.name.trimmed().isEmpty())
            return "SuperSource layouts need unique IDs and names.";
        ids.insert(l.id);
        if (l.background != "transparent" &&
            (l.background.size() != 7 || !l.background.startsWith('#') ||
             l.background.mid(1).contains(QRegularExpression("[^0-9a-fA-F]"))))
            return "Use #RRGGBB or transparent for the background.";
        if (l.boxes.size() > 32)
            return "At most 32 boxes per SuperSource are supported.";
        QSet<QString> boxes;
        QSet<int> buttons;
        for (const auto &b : l.boxes) {
            if(!validSuperSourceCorners(b.corners))return "Perspective corners must form a finite convex quadrilateral.";
            if (b.id.isEmpty() || b.id.size() > 80 || boxes.contains(b.id))
                return "Boxes need stable unique IDs.";
            boxes.insert(b.id);
            if (b.keyButton < -1 || b.keyButton > 23 || (b.kind == "image" && b.keyButton >= 0) ||
                (b.keyButton >= 0 && buttons.contains(b.keyButton)))
                return "KEY bus buttons must be unique within each SuperSource.";
            if (b.keyButton >= 0)
                buttons.insert(b.keyButton);
            if (b.kind != "input" && b.kind != "image")
                return "Unknown SuperSource box type.";
            if (!validRect(b.rect) || !validRect(b.crop))
                return "Box and crop rectangles must fit inside the frame.";
            if (!finiteValue(b.zoom) || b.zoom < .1 || b.zoom > 10 || !finiteValue(b.centerX) ||
                !finiteValue(b.centerY) || b.centerX < 0 || b.centerX > 1 || b.centerY < 0 || b.centerY > 1 ||
                !finiteValue(b.imageAspect) || b.imageAspect < .01 || b.imageAspect > 100)
                return "Invalid box zoom, centre or image aspect.";
            if (b.kind == "input" && (b.input < -1 || b.input > 23 || b.input == 11 || b.input == 23))
                return "Use an input or an explicit M/E input, not a fixed cascade position.";
            if (b.image.contains('\r') || b.image.contains('\n') || b.image.contains(QChar(0)))
                return "Image paths cannot contain command separators.";
        }
    }
    return {};
}
bool parseSuperSources(const QJsonArray &values, QList<SuperSourceLayout> *out, QString *error) {
    QList<SuperSourceLayout> result;
    auto rect = [](QJsonValue value, QRectF *r) {
        auto a = value.toArray();
        if (a.size() != 4)
            return false;
        for (auto v : a)
            if (!v.isDouble())
                return false;
        *r = {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
        return true;
    };
    for (auto value : values) {
        if (!value.isObject()) {
            if (error)
                *error = "Invalid SuperSource layout.";
            return false;
        }
        auto o = value.toObject();
        SuperSourceLayout l;
        l.id = o.value("id").toString();
        l.name = o.value("name").toString();
        l.background = o.value("background").toString("#000000");
        if (!o.value("boxes").isArray()) {
            if (error)
                *error = "Invalid box list.";
            return false;
        }
        for (auto item : o.value("boxes").toArray()) {
            if (!item.isObject()) {
                if (error)
                    *error = "Invalid box object.";
                return false;
            }
            auto x = item.toObject();
            for (auto key : {"zoom", "centerX", "centerY", "imageAspect", "input", "keyButton"})
                if (x.contains(key) && !x.value(key).isDouble()) {
                    if (error)
                        *error = "Box numeric fields must contain numbers.";
                    return false;
                }
            for (auto key : {"cover", "audio", "keepAspect"})
                if (x.contains(key) && !x.value(key).isBool()) {
                    if (error)
                        *error = "Box flags must contain booleans.";
                    return false;
                }
            if (x.contains("input") &&
                x.value("input").toDouble() != std::floor(x.value("input").toDouble())) {
                if (error)
                    *error = "Input IDs must be integers.";
                return false;
            }
            SuperSourceBox b;
            b.id = x.value("id").toString();
            b.name = x.value("name").toString();
            b.kind = x.value("kind").toString("input");
            b.input = x.value("input").toInt(-1);
            b.image = x.value("image").toString();
            if (!rect(x.value("rect"), &b.rect) || (x.contains("crop") && !rect(x.value("crop"), &b.crop))) {
                if (error)
                    *error = "Invalid box geometry.";
                return false;
            }
            if(x.contains("corners")){
                if(!x.value("corners").isArray()||x.value("corners").toArray().size()!=8){if(error)*error="Invalid perspective corners";return false;}
                const auto points=x.value("corners").toArray();for(int i=0;i<4;++i){if(!points[2*i].isDouble()||!points[2*i+1].isDouble()){if(error)*error="Perspective corners must be numeric";return false;}b.corners[i]={points[2*i].toDouble(),points[2*i+1].toDouble()};}
            }
            b.zoom = x.value("zoom").toDouble(1);
            b.centerX = x.value("centerX").toDouble(.5);
            b.centerY = x.value("centerY").toDouble(.5);
            b.imageAspect = x.value("imageAspect").toDouble(16. / 9);
            b.cover = x.value("cover").toBool(true);
            b.audio = x.value("audio").toBool(false);
            b.keepAspect = x.value("keepAspect").toBool(false);
            b.keyButton = x.value("keyButton").toInt(-1);
            l.boxes.append(b);
        }
        result.append(l);
    }
    auto message = validateSuperSources(result);
    if (!message.isEmpty()) {
        if (error)
            *error = message;
        return false;
    }
    *out = result;
    return true;
}
QRectF superSourceFill(const SuperSourceBox &b, double rasterAspect) {
    const double relativeAspect = b.kind == "image" ? b.imageAspect / rasterAspect : 1.;
    const double height = (b.cover ? qMax(b.rect.width() / relativeAspect, b.rect.height())
                                   : qMin(b.rect.width() / relativeAspect, b.rect.height())) *
                          b.zoom;
    const double width = height * relativeAspect;
    return {b.rect.center().x() - width * b.centerX, b.rect.center().y() - height * b.centerY, width, height};
}
QStringList superSourceCommands(const SuperSourceLayout &l, int channel, double aspect,
                                const std::function<QString(int)> &route,
                                const QHash<QString, int> &overrides) {
    QStringList result{QString("CLEAR %1").arg(channel)};
    result.append(
        QString("PLAY %1-0 %2").arg(channel).arg(l.background == "transparent" ? "#00000000" : l.background));
    for (int i = 0; i < l.boxes.size(); ++i) {
        const auto &b = l.boxes[i];
        const auto target = QString("%1-%2").arg(channel).arg(i + 1);
        QString producer;
        if (b.kind == "input") {
            int input = overrides.value(b.id, b.input);
            if (input >= 0)
                producer = route(input);
        } else if (!b.image.isEmpty())
            producer = quote(b.image) + " SCALE_MODE STRETCH";
        if (producer.isEmpty())
            continue;
        result.append("PLAY " + target + ' ' + producer);
        result.append("MIXER " + target + " FILL " + rectangle(superSourceFill(b, aspect)));
        result.append("MIXER " + target + " CLIP " + rectangle(b.rect));
        result.append("MIXER " + target + " CROP " + number(b.crop.x()) + ' ' + number(b.crop.y()) + ' ' +
                      number(b.crop.right()) + ' ' + number(b.crop.bottom()));
        const auto perspective=superSourcePerspective(b);QStringList coordinates;for(auto point:perspective){coordinates<<number(point.x())<<number(point.y());}
        result.append("MIXER "+target+" PERSPECTIVE "+coordinates.join(' '));
        result.append("MIXER " + target + " VOLUME " + QString(b.kind == "input" && b.audio ? "1" : "0"));
    }
    return result;
}

bool validSuperSourceCorners(const std::array<QPointF,4>& corners){
    for(int i=0;i<4;++i){auto p=corners[i];if(!std::isfinite(p.x())||!std::isfinite(p.y())||p.x() < -2||p.x()>3||p.y() < -2||p.y()>3)return false;
        auto a=corners[(i+1)%4]-p,b=corners[(i+2)%4]-corners[(i+1)%4];if(a.x()*b.y()-a.y()*b.x()<1e-6)return false;}
    return true;
}
std::array<QPointF,4> superSourcePerspective(const SuperSourceBox& box){
    const std::array<QPointF,4> identity{{{0,0},{1,0},{1,1},{0,1}}};if(box.corners==identity)return identity;
    std::array<QPointF,4> global;for(int i=0;i<4;++i)global[i]={box.rect.x()+box.rect.width()*box.corners[i].x(),box.rect.y()+box.rect.height()*box.corners[i].y()};
    const std::array<QPointF,4> frame{{{0,0},{1,0},{1,1},{0,1}}};std::array<QPointF,4> result;
    for(int i=0;i<4;++i){double u=(frame[i].x()-box.rect.x())/box.rect.width(),v=(frame[i].y()-box.rect.y())/box.rect.height();result[i]=global[0]*((1-u)*(1-v))+global[1]*(u*(1-v))+global[2]*(u*v)+global[3]*((1-u)*v);}
    return result;
}
