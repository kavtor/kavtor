#include "NativeMultiview.h"
#include "config/Configuration.h"
#include <QtCore/QJsonArray>
#include <QtCore/QRectF>
#include <cmath>

namespace {
struct Painter {
    QJsonArray nodes;
    void rect(QRectF r, QString color, QJsonObject extra = {}) {
        extra.insert("type", "rect");
        geometry(extra, r);
        extra.insert("color", color);
        nodes.append(extra);
    }
    void text(QRectF r, QString value, int size, QString color = "#ffffff", QString align = "left",
              QJsonObject extra = {}) {
        extra.insert("type", "text");
        geometry(extra, r);
        extra.insert("color", color);
        extra.insert("text", value);
        extra.insert("size", size);
        extra.insert("align", align);
        nodes.append(extra);
    }
    void geometry(QJsonObject &n, QRectF r) {
        n.insert("x", r.x());
        n.insert("y", r.y());
        n.insert("w", r.width());
        n.insert("h", r.height());
    }
    void border(QRectF r, QString color, double width = 2, QJsonObject extra = {}) {
        rect({r.x(), r.y(), r.width(), width}, color, extra);
        rect({r.x(), r.bottom() - width, r.width(), width}, color, extra);
        rect({r.x(), r.y(), width, r.height()}, color, extra);
        rect({r.right() - width, r.y(), width, r.height()}, color, extra);
    }
    void dashed(QRectF r, QString color) {
        for (double x = r.left(); x < r.right(); x += 14) {
            double w = qMin(7., r.right() - x);
            rect({x, r.top(), w, 1}, color);
            rect({x, r.bottom() - 1, w, 1}, color);
        }
        for (double y = r.top(); y < r.bottom(); y += 14) {
            double h = qMin(7., r.bottom() - y);
            rect({r.left(), y, 1, h}, color);
            rect({r.right() - 1, y, 1, h}, color);
        }
    }
    void bar(QRectF r, double value, QString color, QString id, bool vertical = false,
             QJsonObject extra = {}) {
        QJsonObject n{{"type", "bar"},      {"value", qBound(0., value, 1.)},
                      {"id", id},           {"vertical", vertical},
                      {"hideOnLost", true}, {"color", color}};
        for (auto i = extra.begin(); i != extra.end(); ++i)
            n.insert(i.key(), i.value());
        geometry(n, r);
        nodes.append(n);
    }
    void label(QRectF r, QString value, int size, QString color, QString edge, QString align) {
        // A bounded label box avoids title jitter when text changes.
        double y = edge == "top" ? r.top() + 8 : r.bottom() - size - 18;
        QRectF box(r.x() + 18, y, r.width() - 36, size + 12);
        text(box, value, size, "#ffffff", align,
             {{"background", color}, {"padding", 10}, {"bold", true}, {"spacing", 1}, {"valign", "center"}});
    }
};
bool same(int a, int b) { return a == b || ((a == 11 || a == 23) && (b == 11 || b == 23)); }
double ratio(QString value) {
    auto parts = value.split(':');
    return parts.size() == 2 && parts[1].toDouble() > 0 ? parts[0].toDouble() / parts[1].toDouble() : 16. / 9;
}
} // namespace
QJsonObject NativeMultiview::scene(const Configuration &config) const {
    Painter p;
    const auto layout = config.multiviewLayoutJson();
    QString edge = layout.value("nameEdge").toString("bottom"),
            align = layout.value("nameAlign").toString("center");
    auto levels = [&](QRectF r, int slot) {
        if (!config.meters())
            return;
        const double y = r.y() + r.height() * .16, h = r.height() * .62;
        for (int side = 0; side < 2; ++side) {
            QRectF track(r.right() - 24 + side * 9, y, 6, h);
            p.rect(track, "#00000099");
            double value =
                slot < meters.size() && side < meters[slot].size() ? meters[slot][side] / 1000. : 0;
            // Segmented green/yellow/red scale with the same attack/release on each band.
            for (int band = 0; band < 3; ++band) {
                double low = band == 0   ? 0
                             : band == 1 ? .7
                                         : .85,
                       high = band == 0   ? .7
                              : band == 1 ? .85
                                          : 1.;
                QRectF part(track.x(), track.y() + h * (1 - high), 6, h * (high - low));
                p.bar(part, value,
                      band == 0   ? "#1f9d3a"
                      : band == 1 ? "#e2b31a"
                                  : "#d02323",
                      QString("meter-%1-%2-%3").arg(slot).arg(side).arg(band), true,
                      {{"minimum", low},
                       {"maximum", high},
                       {"group", QString("meter-%1-%2").arg(slot).arg(side)}});
            }
        }
    };
    auto safe = [&](QRectF r, bool enabled, QString aspect) {
        if (!enabled)
            return;
        double a = ratio(aspect), frame = r.width() / r.height();
        double w = qMin(r.width(), r.height() * a), h = qMin(r.height(), r.width() / a);
        Q_UNUSED(frame);
        QRectF box(r.center().x() - w / 2, r.center().y() - h / 2, w, h);
        bool legacy = config.safePreset() == "legacy";
        double action = legacy ? .05 : .035, title = legacy ? .1 : .05;
        p.border(box.adjusted(w * action, h * action, -w * action, -h * action), "#ffffff66", 1);
        p.dashed(box.adjusted(w * title, h * title, -w * title, -h * title), "#ffffff55");
    };
    QRectF pgm(config.programLeft() ? 384 : 1152, 0, 768, 432),
        pvw(config.programLeft() ? 1152 : 384, 0, 768, 432);
    p.border(pgm, "#ffffff");
    p.border(pvw, "#ffffff");
    safe(pgm, config.safeProgram(), config.safeProgramAspect());
    safe(pvw, config.safePreview(), config.safePreviewAspect());
    p.label(pgm, "M/E 1 PROGRAM", 20, "#c01818", edge, align);
    p.label(pvw, "M/E 1 PREVIEW", 20, "#1a8a2a", edge, align);
    levels(pvw, 24);
    levels(pgm, 25);
    for (int m = 2; m <= 4; ++m)
        for (int bus = 0; bus < 2; ++bus) {
            QRectF r(bus ? 1920 : 0, (m - 2) * 216, 384, 216);
            p.border(r, "#ffffff");
            p.label(r, QString("M/E %1 %2").arg(m).arg(bus ? "PROGRAM" : "PREVIEW"), 16,
                    bus ? "#c01818" : "#1a8a2a", edge, align);
            levels(r, 26 + (m - 2) * 2 + bus);
        }
    for (int i = 0; i < 12; ++i) {
        int id = bank * 12 + i;
        QRectF r(384 + (i % 4) * 384, 432 + (i / 4) * 216, 384, 216);
        QString tally = "#ffffff", background = "#000000b3";
        int border = 2;
        bool pg = same(id, program) || (both && same(id, preview)), pv = same(id, preview);
        bool empty = id < vacant.size() && vacant[id];
        if (id == armed || id == cued) {
            tally = "#eab308";
            background = tally;
            border = 5;
        }
        if (pv) {
            tally = "#1aaf3c";
            background = tally;
            border = 4;
        }
        if (pg) {
            tally = "#df2424";
            background = tally;
            border = 4;
        }
        p.border(r, tally, border, id == cued && !pv && !pg ? QJsonObject{{"blink", 300}} : QJsonObject{});
        p.label(r, id < names.size() ? names[id] : QString("%1 - SRC%1").arg(id + 1), 16,
                empty ? "#282828d9" : background, edge, align);
        if (empty) {
            QRectF box(r.center().x() - 95, r.center().y() - 21, 190, 42);
            p.rect(box, "#000000c7");
            p.border(box, "#ffffff59", 1);
            p.text(box.adjusted(10, 0, -10, 0), "NO SOURCE", 18, "#f3f3f3", "center",
                   {{"bold", true}, {"spacing", 2}, {"valign", "center"}});
        }
        levels(r, id);
        if (id < clocks.size() && clocks[id].duration > .05) {
            const auto &c = clocks[id];
            const int transportWidth=transportIcons?28:0;
            bool bottom = layout.value("clockEdge").toString() == "bottom";
            auto alignment = layout.value("clockAlign").toString("center");
            double x = alignment == "left"    ? r.left() + 8
                       : alignment == "right" ? r.right() - 208 - transportWidth
                                              : r.center().x() - 100 - transportWidth/2;
            QRectF box(x, bottom ? r.bottom() - 35 : r.top() + 8, 200 + transportWidth, 27);
            p.rect(box, "#000000b8");
            if(transportIcons&&c.playbackKnown){
                const bool scrub=c.scrubDirection!=0&&c.scrubUntilMs>clipMonotonicMs();
                if(c.paused&&!scrub){p.rect({x+9,box.y()+7,4,13},"#ffffff");p.rect({x+17,box.y()+7,4,13},"#ffffff");}
                else {const QString direction=scrub&&c.scrubDirection<0?"left":"right";const int count=scrub?2:1;
                    for(int i=0;i<count;++i){QJsonObject node{{"type","triangle"},{"x",x+8+i*10},{"y",box.y()+7},{"w",9},{"h",13},{"color","#ffffff"},{"direction",direction}};p.nodes.append(node);}}
            }
            const QJsonObject style{{"font", "mono"}, {"bold", true}, {"spacing", 1}};
            p.text({x + 8 + transportWidth, box.y() + 2, 86, 23}, formatClipClock(c.elapsed), 15, "#ffffff", "left", style);
            p.text({x + 102 + transportWidth, box.y() + 2, 94, 23}, "-" + formatClipClock(c.duration - c.elapsed), 15,
                   "#ffd56a", "left", style);
        }
    }
    auto info = [&](QRectF r, QString heading) {
        p.rect(r, "#101820");
        p.border(r, "#344454");
        p.text({r.x() + 18, r.y() + 16, r.width() - 36, 28}, heading, 16, "#93a7bb");
    };
    QRectF time(0, 648, 384, 216), delegation(0, 864, 384, 216), output(1920, 648, 384, 216),
        stats(1920, 864, 384, 216);
    info(time, "LOCAL TIME");
    QJsonObject clockNode{{"type", "clock"},  {"x", 18},    {"y", 729},       {"w", 348},
                          {"h", 80},          {"size", 68}, {"font", "mono"}, {"color", "#ffffff"},
                          {"align", "center"}};
    p.nodes.append(clockNode);
    info(delegation, "CONTROL DELEGATION");
    p.text({18, 934, 348, 80}, QString("M/E %1").arg(me), 68, "#ffffff", "center", {{"font", "mono"}});
    p.text({18, 1018, 348, 35}, QString("SOURCE BANK %1").arg(bank + 1), 24, bank ? "#ff4545" : "#dde6ef",
           "center");
    info(output, "OUTPUT PREPARATION");
    p.text({1938, 707, 348, 52}, ready ? "PREPARED" : "WAITING", 40, "#ffffff", "left", {{"font", "mono"}});
    p.text({1938, 766, 348, 30}, "Command acceptance only", 18, "#dde6ef");
    info(stats, "SYSTEM - KAVTOR HOST");
    const auto gpus = system.value("gpus").toArray();
    double y = 910;
    int count = qMax(1, int(gpus.size()));
    double row = qMin(30., 150. / (2 + count * 3));
    auto metric = [&](QString caption, QJsonValue value, QJsonObject memory = {}) {
        double percent = value.isDouble() ? value.toDouble() : -1;
        QString number = percent >= 0 ? QString::number(qRound(percent)) + "%" : "--";
        if (!memory.isEmpty()) {
            double total = memory.value("totalMiB").toDouble(), used = memory.value("usedMiB").toDouble();
            percent = total > 0 ? 100 * used / total : -1;
            number = total > 0
                         ? QString("%1 / %2 GiB").arg(used / 1024., 0, 'f', 1).arg(total / 1024., 0, 'f', 1)
                         : "--";
        }
        p.text({1938, y, 110, row}, caption, 14, "#93a7bb");
        p.text({2034, y, 252, row}, number, 14, "#ffffff", "right");
        p.rect({1938, y + row - 7, 348, 5}, "#243442");
        if (percent >= 0)
            p.rect({1938, y + row - 7, 348 * qBound(0., percent, 100.) / 100, 5}, percent >= 95 ? "#ff6e76"
                                                                                  : percent >= 80
                                                                                      ? "#ffce67"
                                                                                      : "#4ce4be");
        y += row;
    };
    metric("CPU", system.value("cpu"));
    metric("RAM", {}, system.value("ram").toObject());
    if (gpus.isEmpty()) {
        metric("GPU", {});
        metric("VRAM", {});
    } else
        for (const auto &g : gpus) {
            auto gpu = g.toObject();
            p.text({1938, y, 348, row}, gpu.value("name").toString() + " - " + gpu.value("pci").toString(),
                   qBound(8, int(row) - 6, 12), "#93a7bb");
            y += row;
            metric("GPU", gpu.value("busy"));
            metric("VRAM", {}, gpu);
        }
    p.rect({0, 0, 2304, 1080}, "#050a10b8", {{"lostOnly", true}});
    p.rect({420, 435, 1464, 180}, "#0c1218eb", {{"lostOnly", true}});
    p.border({420, 435, 1464, 180}, "#ff4545", 5, {{"lostOnly", true}});
    p.text({450, 459, 1404, 140}, "SERVER LOST", 130, "#ff5555", "center", {{"lostOnly", true}});
    QJsonArray meterData;
    for (const auto &pair : meters) {
        QJsonArray values;
        for (int value : pair)
            values.append(value);
        meterData.append(values);
    }
    QJsonArray labelData;
    for (const auto &name : names)
        labelData.append(name);
    return {{"width", 2304},       {"height", 1080},      {"timeout", 5},       {"nodes", p.nodes},
            {"labels", labelData}, {"meters", meterData}, {"armed", armed},     {"cued", cued},
            {"bank", bank},        {"preview", preview},  {"program", program}, {"both", both}};
}
