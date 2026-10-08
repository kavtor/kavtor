#include "Configuration.h"
#include <algorithm>
#include <QtCore/QSet>
#include <QtCore/QSignalBlocker>
#include "core/Transition.h"

#include <QtCore/QStandardPaths>
#include <QtCore/QtGlobal>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRectF>
#include <QtGui/QImageReader>
#include <QtCore/QRegularExpression>

namespace {

QString quoteArgument(const QString& value)
{
    if (value.isEmpty()) {
        return value;
    }
    const bool needsQuotes = value.contains(QLatin1Char(' '))
        || value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char('/'))
        || value.contains(QLatin1Char('\\'))
        || value.contains(QLatin1Char('?'))
        || value.contains(QLatin1Char('&'));
    if (!needsQuotes) {
        return value;
    }
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

Source sourceFromLegacyAmcp(int id, const QString& name, const QString& amcp, bool enabled)
{
    Source src;
    src.id = id;
    src.name = name;
    src.enabled = enabled;
    src.casparChannel = id + 1;

    const QString trimmed = amcp.trimmed();
    if (trimmed.startsWith(QLatin1String("DEVICE"), Qt::CaseInsensitive)) {
        src.type = SourceType::Decklink;
        src.argument = trimmed.section(QLatin1Char(' '), 1, 1);
    } else if (trimmed.startsWith(QLatin1String("NDI"), Qt::CaseInsensitive)) {
        src.type = SourceType::Ndi;
        src.argument = trimmed.section(QLatin1Char(' '), 1).trimmed();
        src.argument.remove(QLatin1Char('"'));
    } else if (trimmed.startsWith(QLatin1String("MOVIE"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("FILE"), Qt::CaseInsensitive)) {
        src.type = SourceType::File;
        src.argument = trimmed.section(QLatin1Char(' '), 1).trimmed();
        src.argument.remove(QLatin1Char('"'));
    } else if (trimmed.contains(QLatin1String("[html]"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("HTML"), Qt::CaseInsensitive)) {
        src.type = SourceType::Html;
        src.argument = trimmed.section(QLatin1Char(' '), 1).trimmed();
    } else if (trimmed.startsWith(QLatin1String("ffmpeg"), Qt::CaseInsensitive)) {
        src.type = SourceType::Ffmpeg;
        src.argument = trimmed.section(QLatin1String("://"), 1);
        if (src.argument.isEmpty()) {
            src.argument = trimmed.section(QLatin1Char(' '), 1);
        }
    } else if (trimmed.startsWith(QLatin1String("lavfi"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1Char('#'))
        || trimmed.startsWith(QLatin1String("BARS"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("COLOR"), Qt::CaseInsensitive)) {
        src.type = SourceType::ColorBars;
        src.argument = trimmed;
        if (src.argument.startsWith(QLatin1String("BARS"), Qt::CaseInsensitive)
            || src.argument.startsWith(QLatin1String("COLOR"), Qt::CaseInsensitive)) {
            src.argument = trimmed.section(QLatin1Char(' '), 1).trimmed();
        }
    } else {
        src.type = SourceType::File;
        src.argument = trimmed;
    }
    return src;
}

} // namespace

QString sourceTypeToString(SourceType type)
{
    switch (type) {
    case SourceType::V4l2: return QStringLiteral("v4l2");
    case SourceType::Matte: return QStringLiteral("matte");
    case SourceType::SuperSource: return QStringLiteral("supersource");
    case SourceType::MeProgram: return QStringLiteral("me-program");
    case SourceType::Decklink:
        return QStringLiteral("decklink");
    case SourceType::Ndi:
        return QStringLiteral("ndi");
    case SourceType::File:
        return QStringLiteral("file");
    case SourceType::Html:
        return QStringLiteral("html");
    case SourceType::Ffmpeg:
        return QStringLiteral("ffmpeg");
    case SourceType::Still:
        return QStringLiteral("still");
    case SourceType::ColorBars:
        return QStringLiteral("bars");
    }
    return QStringLiteral("file");
}

bool sourceTypeFromString(const QString& text, SourceType* type)
{
    if(text.trimmed().compare("supersource",Qt::CaseInsensitive)==0){*type=SourceType::SuperSource;return true;}
    if (text.trimmed().compare(QLatin1String("me-program"),Qt::CaseInsensitive) == 0) { *type = SourceType::MeProgram; return true; }
    const QString normalized = text.trimmed().toLower();
    if (normalized == QLatin1String("v4l2")) {
        *type = SourceType::V4l2;
        return true;
    }
    if (normalized == QLatin1String("matte") || normalized == QLatin1String("solid")) {
        *type = SourceType::Matte;
        return true;
    }
    if (normalized == QLatin1String("decklink") || normalized == QLatin1String("sdi")) {
        *type = SourceType::Decklink;
        return true;
    }
    if (normalized == QLatin1String("ndi")) {
        *type = SourceType::Ndi;
        return true;
    }
    if (normalized == QLatin1String("file") || normalized == QLatin1String("movie")) {
        *type = SourceType::File;
        return true;
    }
    if (normalized == QLatin1String("html")) {
        *type = SourceType::Html;
        return true;
    }
    if (normalized == QLatin1String("ffmpeg")) {
        *type = SourceType::Ffmpeg;
        return true;
    }
    if (normalized == QLatin1String("still") || normalized == QLatin1String("image")
        || normalized == QLatin1String("dsk")) {
        *type = SourceType::Still;
        return true;
    }
    if (normalized == QLatin1String("bars") || normalized == QLatin1String("colorbars")
        || normalized == QLatin1String("color") || normalized == QLatin1String("colour")) {
        *type = SourceType::ColorBars;
        return true;
    }
    return false;
}

QString staticDmeImageProducer(const QString& path) {
    static const QRegularExpression extension("\\.(png|jpe?g|webp|bmp|tiff?)$",QRegularExpression::CaseInsensitiveOption);
    if(path.trimmed().isEmpty()||path.contains('\r')||path.contains('\n')||path.contains(QChar(0))||path.contains("://")||!extension.match(path).hasMatch())return {};
    QString escaped=path;escaped.replace('\\',"\\\\");escaped.replace('"',"\\\"");
    QString command="\""+escaped+"\" SCALE_MODE FILL";
    return command.toUtf8().size()<=4096?command:QString();
}
bool Configuration::setDmeBackgroundImage(const QString& effect,const QString& path) {
    const int code=effect.section('_',1).toInt();
    const bool preset=effect==QString("sony_%1").arg(code)&&supportedSonyDmes(true,true,true,true,true).contains(code);
    if((!preset&&!QStringList{"global","move","cube","zoom","page_curl","page_roll"}.contains(effect))||staticDmeImageProducer(path).isEmpty())return false;
    m_dmeBackgrounds.insert(effect,QJsonObject{{"image",path}});if(effect!="global")m_dmeBackgroundScopes.insert(effect,true);emit configurationChanged();return true;
}

QString opaqueMatteColor(const QString& value)
{
    static const QRegularExpression rgb(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    const QString trimmed = value.trimmed();
    return rgb.match(trimmed).hasMatch() ? trimmed.toUpper() : QString();
}

QRectF letterboxFill(double sourceAspect, double destAspect)
{
    if (sourceAspect <= 0.0 || destAspect <= 0.0) {
        return QRectF(0, 0, 1, 1);
    }
    if (sourceAspect > destAspect + 1e-6) {
        const double height = destAspect / sourceAspect;
        return QRectF(0, (1.0 - height) / 2.0, 1.0, height);
    }
    if (sourceAspect < destAspect - 1e-6) {
        const double width = sourceAspect / destAspect;
        return QRectF((1.0 - width) / 2.0, 0, width, 1.0);
    }
    return QRectF(0, 0, 1, 1);
}

namespace {

QString letterboxVideoFilter(int width, int height)
{
    return QStringLiteral(
        "scale=iw*sar:ih,setsar=1,scale=%1:%2:force_original_aspect_ratio=decrease,"
        "pad=%3:%4:(ow-iw)/2:(oh-ih)/2:black")
        .arg(width)
        .arg(height)
        .arg(width)
        .arg(height);
}

bool isNamedColor(const QString& text)
{
    static const QStringList names = {
        QStringLiteral("empty"), QStringLiteral("black"), QStringLiteral("white"),
        QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"),
        QStringLiteral("orange"), QStringLiteral("yellow"), QStringLiteral("brown"),
        QStringLiteral("gray"), QStringLiteral("grey"), QStringLiteral("teal")
    };
    return names.contains(text, Qt::CaseInsensitive);
}

QString colorBarsCommand(const QString& argument, int width, int height)
{
    const QString trimmed = argument.trimmed();
    const QString lower = trimmed.toLower();
    if (trimmed.startsWith(QLatin1Char('#')) || isNamedColor(trimmed)) {
        if (lower == QLatin1String("grey")) {
            return QStringLiteral("GRAY");
        }
        return trimmed.startsWith(QLatin1Char('#')) ? trimmed.toUpper() : lower.toUpper();
    }
    Q_UNUSED(width);
    Q_UNUSED(height);
    const QString pattern = colorBarsPattern(argument);
    return pattern.isEmpty() ? QString() : QStringLiteral("COLORBARS %1").arg(pattern);
}

} // namespace

QString colorBarsPattern(const QString& argument)
{
    const QString key = argument.trimmed().toLower();
    if (key.isEmpty() || key == QLatin1String("ebu75") || key == QLatin1String("pal") || key == QLatin1String("ebu"))
        return QStringLiteral("EBU75");
    if (key == QLatin1String("ebu100") || key == QLatin1String("pal100")) return QStringLiteral("EBU100");
    if (key == QLatin1String("smptesd") || key == QLatin1String("smptebars")) return QStringLiteral("SMPTESD");
    if (key == QLatin1String("smptehd") || key == QLatin1String("hd") || key == QLatin1String("smpte"))
        return QStringLiteral("SMPTEHD");
    return {};
}

bool parseNdiDisplayName(const QString& name, QString* machine, QString* source)
{
    const int open = name.lastIndexOf(QLatin1String(" ("));
    if (open <= 0 || !name.endsWith(QLatin1Char(')'))) {
        return false;
    }
    const QString host = name.left(open);
    const QString src = name.mid(open + 2, name.size() - open - 3);
    if (host.isEmpty() || src.isEmpty()) {
        return false;
    }
    if (machine) {
        *machine = host;
    }
    if (source) {
        *source = src;
    }
    return true;
}

QString resolveNdiSourceName(const QString& argument, const QStringList& discovered)
{
    QString token = argument.trimmed();
    if (token.startsWith(QLatin1String("[NDI]"), Qt::CaseInsensitive)) {
        token = token.mid(5).trimmed();
        if (token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"')) && token.size() >= 2) {
            token = token.mid(1, token.size() - 2);
        }
    }
    if (token.startsWith(QLatin1String("ndi://"), Qt::CaseInsensitive)) {
        QString rest = token.mid(6);
        rest.replace(QLatin1Char('+'), QLatin1Char(' '));
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            token = rest.left(slash) + QStringLiteral(" (") + rest.mid(slash + 1) + QLatin1Char(')');
        }
    }
    if (token.isEmpty()) {
        return {};
    }
    if (discovered.isEmpty()) {
        return token;
    }

    QStringList exact;
    QStringList sourceHits;
    QStringList machineHits;
    for (const QString& name : discovered) {
        if (name.compare(token, Qt::CaseInsensitive) == 0) {
            exact.append(name);
        }
        QString machine;
        QString source;
        if (!parseNdiDisplayName(name, &machine, &source)) {
            continue;
        }
        if (source.compare(token, Qt::CaseInsensitive) == 0) {
            sourceHits.append(name);
        }
        if (machine.compare(token, Qt::CaseInsensitive) == 0) {
            machineHits.append(name);
        }
    }
    if (exact.size() == 1) {
        return exact.first();
    }
    if (sourceHits.size() == 1) {
        return sourceHits.first();
    }
    if (machineHits.size() == 1) {
        return machineHits.first();
    }
    return token;
}

QString ndiProducerCommand(const QString& argument, const QStringList& discovered)
{
    const QString resolved = resolveNdiSourceName(argument, discovered);
    if (resolved.isEmpty()) {
        return {};
    }
    if (resolved.startsWith(QLatin1String("[NDI]"), Qt::CaseInsensitive)
        || resolved.startsWith(QLatin1String("ndi://"), Qt::CaseInsensitive)) {
        return resolved;
    }
    return QStringLiteral("[NDI] %1").arg(quoteArgument(resolved));
}

QStringList parseNdiListLines(const QStringList& lines)
{
    QStringList names;
    for (const QString& line : lines) {
        const int first = line.indexOf(QLatin1Char('"'));
        const int last = line.lastIndexOf(QLatin1Char('"'));
        if (first < 0 || last <= first) {
            continue;
        }
        const QString name = line.mid(first + 1, last - first - 1);
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    return names;
}

QString Source::producerCommand(int width, int height) const
{
    if (argument.contains(QLatin1Char('\r')) || argument.contains(QLatin1Char('\n'))
        || argument.contains(QChar(0))) return {};
    switch (type) {
    case SourceType::V4l2: {
        QString device = argument.trimmed();
        if (device.startsWith(QLatin1String("v4l2://"), Qt::CaseInsensitive)) device = device.mid(7);
        // The path belongs to the CasparCG host, which may be remote.
        // Stable udev links are accepted without checking the local filesystem.
        static const QRegularExpression path(QStringLiteral("^/dev/(?:video[0-9]+|v4l/by-(?:id|path)/[A-Za-z0-9_.:+-]+)$"));
        if (!path.match(device).hasMatch() || device.endsWith(QLatin1String("/."))
            || device.endsWith(QLatin1String("/.."))) return {};
        return QStringLiteral("%1 SEEKABLE 0 VF %2")
            .arg(quoteArgument(QStringLiteral("v4l2://") + device), quoteArgument(letterboxVideoFilter(width, height)));
    }
    case SourceType::Matte: {
        // Opaque RGB only: never treat an invalid colour as a clip or command.
        return opaqueMatteColor(argument);
    }
    case SourceType::SuperSource: return {};
    case SourceType::MeProgram: return {}; // Engine-owned whole-channel route.
    case SourceType::Decklink:
        return QStringLiteral("DECKLINK DEVICE %1").arg(argument.isEmpty() ? QString::number(id + 1) : argument);
    case SourceType::Ndi:
        return ndiProducerCommand(argument);
    case SourceType::File:
        // VF is video-only. FILTER is also applied to audio in Caspar 2.5.
        if (argument.trimmed().isEmpty()) {
            return {};
        }
        return QStringLiteral("%1%2 VF %3")
            .arg(quoteArgument(argument), loop ? QStringLiteral(" LOOP") : QString(),
                quoteArgument(letterboxVideoFilter(width, height)));
    case SourceType::Html:
        if (argument.trimmed().isEmpty()) return {};
        return QStringLiteral("[HTML] %1").arg(quoteArgument(argument));
    case SourceType::Ffmpeg: {
        QString url = argument.trimmed();
        if (url.isEmpty()) return {};
        // Caspar 2.5 does not strip ffmpeg://. That prefix is passed to
        // libavformat as the filename and fails (Protocol not found).
        // FFmpeg is registered before HTML, so a quoted http(s) URL is
        // opened as media instead of a CEF page.
        if (url.startsWith(QLatin1String("ffmpeg://"), Qt::CaseInsensitive)) {
            url = url.mid(9);
            if (url.trimmed().isEmpty()) return {};
        }
        return QStringLiteral("%1 SEEKABLE 0 VF %2")
            .arg(quoteArgument(url), quoteArgument(letterboxVideoFilter(width, height)));
    }
    case SourceType::Still:
        // Caspar 2.5 has no [IMAGE] producer hint; the path extension selects it.
        if (argument.trimmed().isEmpty()) {
            return {};
        }
        return quoteArgument(argument);
    case SourceType::ColorBars:
        return colorBarsCommand(argument, width, height);
    }
    return {};
}

bool Source::isAssigned() const
{
    return enabled && (type==SourceType::SuperSource ? !argument.trimmed().isEmpty() : type == SourceType::MeProgram ? argument.toInt() >= 1 && argument.toInt() <= 4 : !producerCommand().isEmpty());
}

bool Source::isClip() const
{
    return type == SourceType::File && isAssigned();
}

QRectF Source::mixerFill(int destWidth, int destHeight) const
{
    if (type != SourceType::Still || destWidth <= 0 || destHeight <= 0 || argument.trimmed().isEmpty()) {
        return QRectF(0, 0, 1, 1);
    }
    const QSize size = QImageReader(argument).size();
    if (!size.isValid() || size.height() <= 0) {
        return QRectF(0, 0, 1, 1);
    }
    return letterboxFill(double(size.width()) / double(size.height()),
                         double(destWidth) / double(destHeight));
}

Configuration::Configuration(QObject* parent)
    : QObject(parent)
{
    setDefaults();
}

void Configuration::setDefaults()
{
    m_casparHost = QStringLiteral("127.0.0.1");
    m_casparPort = 5250;
    m_previewChannel = 9;
    m_programChannel = 10;
    m_multiviewChannel = 11;
    m_programOutputChannel = 12;
    m_ndiProgramEnabled = true;
    m_ndiProgramName = QStringLiteral("KAVTOR_PGM");
    m_ndiCleanEnabled = true;
    m_ndiCleanName = QStringLiteral("KAVTOR_CLEAN");
    m_panelPort = 9100;
    m_oscPort = 6250;
    m_dskSourceId = 7;
    m_dsk2SourceId = -1;
    m_keySources = {-1, -1, -1, -1};
    m_meKeySources = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    m_dmeBackgrounds={};m_dmeBackgroundScopes={};
    m_dipColor = QStringLiteral("#000000");
    m_autoDurationFrames = 25;m_superMixGainA=m_superMixGainB=100;
    m_wipePatternId = QStringLiteral("wipe_horizontal");
    m_wipeDirectionMode = WipeDirectionMode::Forward;
    m_wipeEdgeMode = WipeEdgeMode::Hard;
    m_wipeEdgeAmount = 8;
    m_wipeBorderColor = QStringLiteral("#ffffff");
    m_wipePresets = {23, 5, 21, 24, 18, 9, 6, 1, 3, 17};
    m_wipeMulti = 1;
    m_wipeVertices=5;m_wipeRounding=15;m_wipeTileSize=10;
    m_wipeBorderAmount = 0;
    m_wipeShadowAmount = 0;
    m_wipeAspectW = 1;
    m_wipeAspectH = 1;
    m_wipePosX = 500;
    m_wipePosY = 500;
    m_safePreviewAspect = m_safeProgramAspect = QStringLiteral("16:9");
    m_safePreset = QStringLiteral("ebu-r95");
    m_meters = false;
    m_stingers.clear();
    for (int i = 0; i < 10; ++i) {
        m_stingers.append(StingerSlot{});
    }
    m_windowWidth = 980;
    m_windowHeight = 720;

    m_destinations.clear();
    m_sources.clear();
    const struct {
        const char* name;
        SourceType type;
        const char* argument;
        bool enabled;
    } defaults[] = {
        {"Clip 1", SourceType::File, "AMB", true},
        {"Clip 2", SourceType::File, "AMB", true},
        {"NDI 1", SourceType::Ndi, "CAM1", true},
        {"NDI 2", SourceType::Ndi, "CAM2", true},
        {"Stream 1", SourceType::Ffmpeg, "rtsp://127.0.0.1/live", true},
        {"HTTP 1", SourceType::Html, "https://example.com", true},
        {"Clip 3", SourceType::File, "", false},
        {"DSK", SourceType::Still, "", true},
    };
    for (int i = 0; i < maxSources(); ++i) {
        Source src;
        src.id = i;
        src.casparChannel = i < 8 ? i + 1 : i + 11;
        if (i >= 8) { src.name = QStringLiteral("Source %1").arg(i + 1); src.enabled = false; m_sources.append(src); continue; }
        src.name = QString::fromLatin1(defaults[i].name);
        src.type = defaults[i].type;
        src.argument = QString::fromLatin1(defaults[i].argument);
        src.enabled = defaults[i].enabled;
        m_sources.append(src);
    }
}

QString Configuration::defaultConfigFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + QStringLiteral("/.config");
    }
    return QDir(configDir + QStringLiteral("/kavtor")).filePath(QStringLiteral("switcher.json"));
}

QString Configuration::configFilePath() const
{
    return m_configFilePath.isEmpty() ? defaultConfigFilePath() : m_configFilePath;
}

void Configuration::setConfigFilePath(const QString& path)
{
    m_configFilePath = path;
}

bool Configuration::load()
{
    const QString filePath = configFilePath();
    QFile file(filePath);
    if (!file.exists()) {
        // Import the prior development name only for the default path. Keep
        // the original intact and never replace an existing kavtor config.
        if (m_configFilePath.isEmpty()) {
            const QString legacyPath = QDir::cleanPath(QDir(QFileInfo(filePath).absolutePath()).filePath(QStringLiteral("../strata/switcher.json")));
            QFile legacy(legacyPath);
            if (legacy.exists()) {
                if (!legacy.open(QIODevice::ReadOnly)) return false;
                const QJsonDocument legacyDoc = QJsonDocument::fromJson(legacy.readAll());
                if (!legacyDoc.isObject() || !fromJson(legacyDoc.object())) return false;
                return save();
            }
        }
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        return save();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Cannot open configuration file %s", qPrintable(filePath));
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning("Configuration file is not a JSON object");
        return false;
    }
    return fromJson(doc.object());
}

bool Configuration::save() const
{
    const QString filePath = configFilePath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("Cannot write configuration file %s", qPrintable(filePath));
        return false;
    }
    const QByteArray data = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        qWarning("Cannot commit configuration file %s", qPrintable(filePath));
        return false;
    }
    return true;
}

QJsonObject Configuration::toJson() const
{
    QJsonObject root;

    root.insert("superSources",superSourcesJson(m_superSources));
    QJsonObject caspar;
    caspar.insert(QStringLiteral("host"), m_casparHost);
    caspar.insert(QStringLiteral("port"), m_casparPort);
    caspar.insert(QStringLiteral("previewChannel"), m_previewChannel);
    caspar.insert(QStringLiteral("programChannel"), m_programChannel);
    caspar.insert(QStringLiteral("multiviewChannel"), m_multiviewChannel);
    caspar.insert(QStringLiteral("oscPort"), m_oscPort);
    root.insert(QStringLiteral("casparcg"), caspar);

    QJsonObject outputs;
    outputs.insert(QStringLiteral("programOutputChannel"), m_programOutputChannel);
    QJsonObject ndiProgram;
    ndiProgram.insert(QStringLiteral("enabled"), m_ndiProgramEnabled);
    ndiProgram.insert(QStringLiteral("name"), m_ndiProgramName);
    outputs.insert(QStringLiteral("ndiProgram"), ndiProgram);
    QJsonObject ndiClean;
    ndiClean.insert(QStringLiteral("enabled"), m_ndiCleanEnabled);
    ndiClean.insert(QStringLiteral("name"), m_ndiCleanName);
    outputs.insert(QStringLiteral("ndiClean"), ndiClean);
    QJsonArray destinations;
    for (const auto& out : m_destinations) destinations.append(QJsonObject{{"id",out.id},{"name",out.name},{"type",out.type},{"source",out.source},{"aux",out.aux},{"device",out.device},{"enabled",out.enabled},{"fullscreen",out.fullscreen}});
    outputs.insert(QStringLiteral("destinations"), destinations);
    root.insert(QStringLiteral("outputs"), outputs);

    QJsonObject dsk;
    dsk.insert(QStringLiteral("sourceId"), m_dskSourceId);
    dsk.insert(QStringLiteral("sourceId2"), m_dsk2SourceId);
    QJsonArray dskProcessing;
    for(const auto& processing:m_dskProcessing)dskProcessing.append(processing.toJson());
    dsk.insert(QStringLiteral("processing"),dskProcessing);
    root.insert(QStringLiteral("dsk"), dsk);
    QJsonArray keyers;
    for (int sourceId : m_keySources) {
        QJsonObject entry;
        entry.insert(QStringLiteral("sourceId"), sourceId);
        keyers.append(entry);
    }
    root.insert(QStringLiteral("keyers"), keyers);
    QJsonArray meKeyers;
    for (int me = 0; me < 4; ++me) {
        QJsonArray row;
        for (int slot = 0; slot < 4; ++slot) {
            QJsonObject entry;
            entry.insert(QStringLiteral("sourceId"), meKeySource(me, slot));
            entry.insert(QStringLiteral("processing"),keyProcessing(me,slot).toJson());
            row.append(entry);
        }
        meKeyers.append(row);
    }
    root.insert(QStringLiteral("meKeyers"), meKeyers);

    QJsonArray sourcesArray;
    for (const Source& src : m_sources) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), src.id);
        obj.insert(QStringLiteral("name"), src.name);
        obj.insert(QStringLiteral("type"), sourceTypeToString(src.type));
        obj.insert(QStringLiteral("casparChannel"), src.casparChannel);
        obj.insert(QStringLiteral("argument"), src.argument);
        obj.insert(QStringLiteral("enabled"), src.enabled);
        obj.insert(QStringLiteral("loop"), src.loop);
        sourcesArray.append(obj);
    }
    root.insert(QStringLiteral("sources"), sourcesArray);

    QJsonObject transitions;
    transitions.insert("superMixGainA",m_superMixGainA);transitions.insert("superMixGainB",m_superMixGainB);
    transitions.insert(QStringLiteral("dipColor"), m_dipColor);
    transitions.insert(QStringLiteral("autoDurationFrames"), m_autoDurationFrames);
    transitions.insert(QStringLiteral("wipePattern"), m_wipePatternId);
    transitions.insert(QStringLiteral("wipeDirection"), wipeDirectionModeToString(m_wipeDirectionMode));
    transitions.insert(QStringLiteral("wipeEdge"), wipeEdgeModeToString(m_wipeEdgeMode));
    transitions.insert(QStringLiteral("wipeEdgeAmount"), m_wipeEdgeAmount);
    transitions.insert(QStringLiteral("wipeBorderColor"), m_wipeBorderColor);
    QJsonArray presets;
    for (int code : m_wipePresets) {
        presets.append(code);
    }
    transitions.insert(QStringLiteral("wipePresets"), presets);
    transitions.insert(QStringLiteral("wipeMulti"), m_wipeMulti);
    transitions.insert("wipeTileSize",m_wipeTileSize);transitions.insert("wipeVertices",m_wipeVertices);transitions.insert("wipeRounding",m_wipeRounding);
    transitions.insert(QStringLiteral("wipeBorder"), m_wipeBorderAmount);
    transitions.insert(QStringLiteral("wipeShadow"), m_wipeShadowAmount);
    transitions.insert(QStringLiteral("wipeAspectW"), m_wipeAspectW);
    transitions.insert(QStringLiteral("wipeAspectH"), m_wipeAspectH);
    transitions.insert(QStringLiteral("wipePosX"), m_wipePosX);
    transitions.insert(QStringLiteral("wipePosY"), m_wipePosY);
    transitions.insert("dmeBackgrounds",m_dmeBackgrounds);transitions.insert("dmeBackgroundScopes",m_dmeBackgroundScopes);
    transitions.insert(QStringLiteral("stingers"), stingersJson());
    root.insert(QStringLiteral("transitions"), transitions);

    QJsonObject panel;
    panel.insert(QStringLiteral("port"), m_panelPort);
    root.insert(QStringLiteral("panel"), panel);

    QJsonObject ui;
    ui.insert(QStringLiteral("windowWidth"), m_windowWidth);
    ui.insert(QStringLiteral("windowHeight"), m_windowHeight);
    root.insert(QStringLiteral("ui"), ui);
    root.insert(QStringLiteral("multiview"), multiviewLayoutJson());

    return root;
}

QJsonObject Configuration::multiviewLayoutJson() const
{
    QJsonObject layout;
    layout.insert(QStringLiteral("programLeft"), m_programLeft);
    layout.insert(QStringLiteral("nameEdge"), m_nameEdge);
    layout.insert(QStringLiteral("nameAlign"), m_nameAlign);
    layout.insert(QStringLiteral("clockEdge"), m_clockEdge);
    layout.insert(QStringLiteral("clockAlign"), m_clockAlign);
    layout.insert(QStringLiteral("safePreviewAspect"), m_safePreviewAspect);
    layout.insert(QStringLiteral("safeProgramAspect"), m_safeProgramAspect);
    layout.insert(QStringLiteral("safePreset"), m_safePreset);
    layout.insert(QStringLiteral("safePreview"), m_safePreview);
    layout.insert(QStringLiteral("safeProgram"), m_safeProgram);
    layout.insert(QStringLiteral("meters"), m_meters);
    return layout;
}

bool Configuration::fromJson(const QJsonObject& obj)
{
    if(obj.contains("superSources")){if(!obj.value("superSources").isArray())return false;QList<SuperSourceLayout> layouts;if(!parseSuperSources(obj.value("superSources").toArray(),&layouts))return false;m_superSources=layouts;}else m_superSources.clear();
    const QJsonObject previous = toJson();
    bool applied = false;
    {
        // No observer may see a partially applied document or a rollback.
        const QSignalBlocker blocker(this);
        applied = applyJson(obj);
        if (!applied) {
            const bool restored = applyJson(previous);
            Q_ASSERT(restored);
            Q_UNUSED(restored);
        }
    }
    if (applied) emit configurationChanged();
    return applied;
}

bool Configuration::applyJson(const QJsonObject& obj)
{
    if (obj.value(QStringLiteral("casparcg")).isObject()) {
        const QJsonObject caspar = obj.value(QStringLiteral("casparcg")).toObject();
        m_casparHost = caspar.value(QStringLiteral("host")).toString(m_casparHost);
        m_casparPort = caspar.value(QStringLiteral("port")).toInt(m_casparPort);
        m_previewChannel = caspar.value(QStringLiteral("previewChannel")).toInt(m_previewChannel);
        m_programChannel = caspar.value(QStringLiteral("programChannel")).toInt(m_programChannel);
        m_multiviewChannel = caspar.value(QStringLiteral("multiviewChannel")).toInt(m_multiviewChannel);
        m_oscPort = caspar.value(QStringLiteral("oscPort")).toInt(m_oscPort);
    }

    if (obj.value(QStringLiteral("outputs")).isObject()) {
        const QJsonObject outputs = obj.value(QStringLiteral("outputs")).toObject();
        m_programOutputChannel = outputs.value(QStringLiteral("programOutputChannel")).toInt(
            outputs.value(QStringLiteral("cleanFeedChannel")).toInt(m_programOutputChannel));
        QList<OutputDestination> destinations;
        for (const auto& value : outputs.value(QStringLiteral("destinations")).toArray()) {
            const auto out = value.toObject(); OutputDestination d;
            d.id=out.value("id").toInt(); d.name=out.value("name").toString(); d.type=out.value("type").toString();
            d.source=out.value("source").toInt(1201); d.aux=out.value("aux").toInt(); d.device=out.value("device").toInt();
            d.enabled=out.value("enabled").toBool(); d.fullscreen=out.value("fullscreen").toBool(true); destinations.append(d);
        }
        if (!setDestinations(destinations)) return false;
        if (outputs.value(QStringLiteral("ndiProgram")).isObject()) {
            const QJsonObject ndiProgram = outputs.value(QStringLiteral("ndiProgram")).toObject();
            m_ndiProgramEnabled = ndiProgram.value(QStringLiteral("enabled")).toBool(m_ndiProgramEnabled);
            const QString name = ndiProgram.value(QStringLiteral("name")).toString();
            if (!name.trimmed().isEmpty()) {
                m_ndiProgramName = name.trimmed();
            }
        }
        if (outputs.value(QStringLiteral("ndiClean")).isObject()) {
            const QJsonObject ndiClean = outputs.value(QStringLiteral("ndiClean")).toObject();
            m_ndiCleanEnabled = ndiClean.value(QStringLiteral("enabled")).toBool(m_ndiCleanEnabled);
            const QString name = ndiClean.value(QStringLiteral("name")).toString();
            if (!name.trimmed().isEmpty()) {
                m_ndiCleanName = name.trimmed();
            }
        }
    }

    if (obj.value(QStringLiteral("sources")).isArray()) {
        m_sources.clear();
        const QJsonArray arr = obj.value(QStringLiteral("sources")).toArray();
        for (const QJsonValue& val : arr) {
            if (!val.isObject() || m_sources.size() >= maxSources()) {
                continue;
            }
            const QJsonObject srcObj = val.toObject();
            if (srcObj.contains(QStringLiteral("type"))) {
                Source src;
                src.id = srcObj.value(QStringLiteral("id")).toInt();
                src.name = srcObj.value(QStringLiteral("name")).toString();
                src.casparChannel = srcObj.value(QStringLiteral("casparChannel")).toInt(src.id + 1);
                src.argument = srcObj.value(QStringLiteral("argument")).toString();
                src.enabled = srcObj.value(QStringLiteral("enabled")).toBool(true);
                src.loop = srcObj.value(QStringLiteral("loop")).toBool(true);
                if (!sourceTypeFromString(srcObj.value(QStringLiteral("type")).toString(), &src.type)) {
                    src.type = SourceType::File;
                }
                m_sources.append(src);
            } else {
                m_sources.append(sourceFromLegacyAmcp(
                    srcObj.value(QStringLiteral("id")).toInt(),
                    srcObj.value(QStringLiteral("name")).toString(),
                    srcObj.value(QStringLiteral("amcp")).toString(),
                    srcObj.value(QStringLiteral("enabled")).toBool(true)));
            }
        }
    }

    for (int id = 0; id < maxSources(); ++id) if (!sourceById(id)) {
        Source src; src.id = id; src.name = QStringLiteral("Source %1").arg(id + 1);
        src.casparChannel = id < 8 ? id + 1 : id + 11; src.enabled = false; m_sources.append(src);
    }
    std::sort(m_sources.begin(), m_sources.end(), [](const Source& a, const Source& b) { return a.id < b.id; });
    for (Source& src : m_sources) if (src.id == 11 || src.id == 23) src.enabled = false;

    if (obj.value(QStringLiteral("transitions")).isObject()) {
        const QJsonObject transitions = obj.value(QStringLiteral("transitions")).toObject();
        const QString dip = opaqueMatteColor(transitions.value(QStringLiteral("dipColor")).toString());
        if (!dip.isEmpty()) m_dipColor = dip;
        m_superMixGainA=qBound(0,transitions.value("superMixGainA").toInt(100),100);m_superMixGainB=qBound(0,transitions.value("superMixGainB").toInt(100),100);
        m_autoDurationFrames = transitions.value(QStringLiteral("autoDurationFrames")).toInt(m_autoDurationFrames);
        const QString wipe = transitions.value(QStringLiteral("wipePattern")).toString(m_wipePatternId);
        const WipePatternLookup lookup = lookupWipePattern(wipe);
        m_wipePatternId = lookup.pattern.id;
        WipeDirectionMode direction = lookup.reverse ? WipeDirectionMode::Reverse : WipeDirectionMode::Forward;
        if (wipeDirectionModeFromString(transitions.value(QStringLiteral("wipeDirection")).toString(), &direction)) {
            m_wipeDirectionMode = direction;
        } else {
            m_wipeDirectionMode = direction;
        }
        WipeEdgeMode edge = m_wipeEdgeMode;
        if (wipeEdgeModeFromString(transitions.value(QStringLiteral("wipeEdge")).toString(), &edge)) {
            m_wipeEdgeMode = edge;
        }
        m_wipeEdgeAmount = qBound(0, transitions.value(QStringLiteral("wipeEdgeAmount")).toInt(m_wipeEdgeAmount), 40);
        const QString color = transitions.value(QStringLiteral("wipeBorderColor")).toString(m_wipeBorderColor);
        if (color.startsWith(QLatin1Char('#')) && (color.size() == 7 || color.size() == 4)) {
            m_wipeBorderColor = color;
        }
        if (transitions.value(QStringLiteral("wipePresets")).isArray()) {
            QList<int> presets;
            for (const QJsonValue& value : transitions.value(QStringLiteral("wipePresets")).toArray()) {
                if (presets.size() >= 10) {
                    break;
                }
                presets.append(value.toInt());
            }
            replaceWipePresets(presets);
        }
        m_wipeTileSize=qBound(2,transitions.value("wipeTileSize").toInt(10),50);
        m_wipeVertices=qBound(3,transitions.value("wipeVertices").toInt(m_wipeVertices),64);
        m_wipeRounding=qBound(0,transitions.value("wipeRounding").toInt(m_wipeRounding),50);
        const int multi = transitions.value(QStringLiteral("wipeMulti")).toInt(m_wipeMulti);
        if (multi == 1 || multi == 2 || multi == 4 || multi == 9 || multi == 16) {
            m_wipeMulti = multi;
        }
        m_dmeBackgroundScopes={};
        m_dmeBackgrounds={};if(transitions.value("dmeBackgrounds").isObject()){auto settings=transitions.value("dmeBackgrounds").toObject();for(auto i=settings.begin();i!=settings.end();++i){if(i.value().isObject()){const QString image=i.value().toObject().value("image").toString();if(!staticDmeImageProducer(image).isEmpty())m_dmeBackgrounds.insert(i.key(),QJsonObject{{"image",image}});continue;}const int source=i.value().toInt(-2);if(source==-1||(source>=0&&source<24&&source!=11&&source!=23)||(source>=1000&&source<=1003))m_dmeBackgrounds.insert(i.key(),source);}}
        if(transitions.value("dmeBackgroundScopes").isObject()){const auto scopes=transitions.value("dmeBackgroundScopes").toObject();for(auto i=scopes.begin();i!=scopes.end();++i)if(i.value().isBool())m_dmeBackgroundScopes.insert(i.key(),i.value());}
        else for(auto i=m_dmeBackgrounds.begin();i!=m_dmeBackgrounds.end();++i)if(i.key()!="global")m_dmeBackgroundScopes.insert(i.key(),i.value()!=QJsonValue(-1)); // Preserve explicit legacy backgrounds; default black inherits.

        m_wipeShadowAmount = qBound(0, transitions.value(QStringLiteral("wipeShadow")).toInt(m_wipeShadowAmount), 40);
        m_wipeBorderAmount = qBound(0, transitions.value(QStringLiteral("wipeBorder")).toInt(m_wipeBorderAmount), 40);
        const int aspectW = transitions.value(QStringLiteral("wipeAspectW")).toInt(m_wipeAspectW);
        const int aspectH = transitions.value(QStringLiteral("wipeAspectH")).toInt(m_wipeAspectH);
        if (aspectW >= 1 && aspectW <= 1000 && aspectH >= 1 && aspectH <= 1000) {
            m_wipeAspectW = aspectW;
            m_wipeAspectH = aspectH;
        }
        m_wipePosX = qBound(0, transitions.value(QStringLiteral("wipePosX")).toInt(m_wipePosX), 1000);
        m_wipePosY = qBound(0, transitions.value(QStringLiteral("wipePosY")).toInt(m_wipePosY), 1000);
        if (transitions.value(QStringLiteral("stingers")).isArray()) {
            QList<StingerSlot> entries;
            const QJsonArray rows = transitions.value(QStringLiteral("stingers")).toArray();
            for (const QJsonValue& value : rows) {
                if (entries.size() >= 10 || !value.isObject()) {
                    break;
                }
                const QJsonObject row = value.toObject();
                StingerSlot entry;
                entry.media = row.value(QStringLiteral("media")).toString();
                entry.reverse = row.value(QStringLiteral("reverse")).toString();
                entry.cutFrames = row.value(QStringLiteral("cutFrames")).toInt(12);
                entry.lengthFrames = row.value(QStringLiteral("lengthFrames")).toInt(50);
                entries.append(entry);
            }
            while (entries.size() < 10) {
                entries.append(StingerSlot{});
            }
            if (!replaceStingers(entries)) return false;
        }
    }

    if (obj.value(QStringLiteral("dsk")).isObject()) {
        const QJsonObject dsk = obj.value(QStringLiteral("dsk")).toObject();
        m_dskSourceId = dsk.value(QStringLiteral("sourceId")).toInt(m_dskSourceId);
        m_dsk2SourceId = dsk.value(QStringLiteral("sourceId2")).toInt(m_dsk2SourceId);
    }
    if (obj.value(QStringLiteral("keyers")).isArray()) {
        const QJsonArray keyers = obj.value(QStringLiteral("keyers")).toArray();
        for (int i = 0; i < m_keySources.size() && i < keyers.size(); ++i) {
            m_keySources[i] = keyers.at(i).toObject().value(QStringLiteral("sourceId")).toInt(-1);
        }
        for (int i = 0; i < 4 && i < m_meKeySources.size(); ++i) {
            m_meKeySources[i] = m_keySources.at(i);
        }
    }
    m_keyProcessing = {};
    m_dskProcessing = {};
    const auto dskProcessing=obj.value("dsk").toObject().value("processing").toArray();
    for(int slot=0;slot<2&&slot<dskProcessing.size();++slot)
        m_dskProcessing[slot].update(dskProcessing[slot].toObject());
    if (obj.value(QStringLiteral("meKeyers")).isArray()) {
        const QJsonArray rows = obj.value(QStringLiteral("meKeyers")).toArray();
        for (int me = 0; me < 4 && me < rows.size(); ++me) {
            const QJsonArray row = rows.at(me).toArray();
            for (int slot = 0; slot < 4 && slot < row.size(); ++slot) {
                m_meKeySources[me * 4 + slot] = row.at(slot).toObject().value(QStringLiteral("sourceId")).toInt(-1);
                m_keyProcessing[me*4+slot].update(row.at(slot).toObject().value("processing").toObject());
            }
        }
        for (int slot = 0; slot < 4 && slot < m_keySources.size(); ++slot) {
            m_keySources[slot] = m_meKeySources.at(slot);
        }
    }

    if (obj.value(QStringLiteral("panel")).isObject()) {
        m_panelPort = obj.value(QStringLiteral("panel")).toObject()
            .value(QStringLiteral("port")).toInt(m_panelPort);
    }

    if (obj.value(QStringLiteral("ui")).isObject()) {
        const QJsonObject ui = obj.value(QStringLiteral("ui")).toObject();
        m_windowWidth = ui.value(QStringLiteral("windowWidth")).toInt(m_windowWidth);
        m_windowHeight = ui.value(QStringLiteral("windowHeight")).toInt(m_windowHeight);
    }

    if (obj.value(QStringLiteral("multiview")).isObject()) {
        const QJsonObject layout = obj.value(QStringLiteral("multiview")).toObject();
        if (layout.contains(QStringLiteral("programLeft"))) {
            m_programLeft = layout.value(QStringLiteral("programLeft")).toBool(m_programLeft);
        }
        const auto edge = [](const QString& text, const QString& fallback) {
            return text == QLatin1String("top") || text == QLatin1String("bottom") ? text : fallback;
        };
        const auto align = [](const QString& text, const QString& fallback) {
            return text == QLatin1String("left") || text == QLatin1String("center") || text == QLatin1String("right")
                ? text : fallback;
        };
        m_nameEdge = edge(layout.value(QStringLiteral("nameEdge")).toString(), m_nameEdge);
        m_nameAlign = align(layout.value(QStringLiteral("nameAlign")).toString(), m_nameAlign);
        m_clockEdge = edge(layout.value(QStringLiteral("clockEdge")).toString(), m_clockEdge);
        m_clockAlign = align(layout.value(QStringLiteral("clockAlign")).toString(), m_clockAlign);
        const QString previewAspect = layout.value(QStringLiteral("safePreviewAspect")).toString(QStringLiteral("16:9"));
        const QString programAspect = layout.value(QStringLiteral("safeProgramAspect")).toString(QStringLiteral("16:9"));
        m_safePreviewAspect = validSafeAspect(previewAspect) ? previewAspect : QStringLiteral("16:9");
        m_safeProgramAspect = validSafeAspect(programAspect) ? programAspect : QStringLiteral("16:9");
        m_safePreset = layout.value(QStringLiteral("safePreset")).toString(QStringLiteral("ebu-r95")) == QLatin1String("legacy")
            ? QStringLiteral("legacy") : QStringLiteral("ebu-r95");
        if (layout.contains(QStringLiteral("safePreview"))) {
            m_safePreview = layout.value(QStringLiteral("safePreview")).toBool(m_safePreview);
        }
        if (layout.contains(QStringLiteral("safeProgram"))) {
            m_safeProgram = layout.value(QStringLiteral("safeProgram")).toBool(m_safeProgram);
        }
        if (layout.contains(QStringLiteral("meters"))) {
            m_meters = layout.value(QStringLiteral("meters")).toBool(m_meters);
        }
    }

    return true;
}

const Source* Configuration::sourceById(int id) const
{
    for (const Source& src : m_sources) {
        if (src.id == id) {
            return &src;
        }
    }
    return nullptr;
}

Source* Configuration::sourceById(int id)
{
    for (Source& src : m_sources) {
        if (src.id == id) {
            return &src;
        }
    }
    return nullptr;
}

QString Configuration::sourceName(int id) const
{
    const Source* src = sourceById(id);
    return src ? src->name : QString();
}

void Configuration::setCasparHost(const QString& host)
{
    if (m_casparHost == host) {
        return;
    }
    m_casparHost = host;
    emit configurationChanged();
}

void Configuration::setCasparPort(int port)
{
    if (m_casparPort == port) {
        return;
    }
    m_casparPort = port;
    emit configurationChanged();
}

void Configuration::setPreviewChannel(int channel)
{
    if (m_previewChannel == channel) {
        return;
    }
    m_previewChannel = channel;
    emit configurationChanged();
}

void Configuration::setProgramChannel(int channel)
{
    if (m_programChannel == channel) {
        return;
    }
    m_programChannel = channel;
    emit configurationChanged();
}

void Configuration::setMultiviewChannel(int channel)
{
    if (m_multiviewChannel == channel) {
        return;
    }
    m_multiviewChannel = channel;
    emit configurationChanged();
}

void Configuration::setProgramOutputChannel(int channel)
{
    if (m_programOutputChannel == channel) {
        return;
    }
    m_programOutputChannel = channel;
    emit configurationChanged();
}

void Configuration::setNdiProgramEnabled(bool enabled)
{
    if (m_ndiProgramEnabled == enabled) {
        return;
    }
    m_ndiProgramEnabled = enabled;
    emit configurationChanged();
}

void Configuration::setNdiProgramName(const QString& name)
{
    const QString trimmed = name.trimmed().isEmpty()
        ? QStringLiteral("KAVTOR_PGM")
        : name.trimmed();
    if (m_ndiProgramName == trimmed) {
        return;
    }
    m_ndiProgramName = trimmed;
    emit configurationChanged();
}

void Configuration::setNdiCleanEnabled(bool enabled)
{
    if (m_ndiCleanEnabled == enabled) {
        return;
    }
    m_ndiCleanEnabled = enabled;
    emit configurationChanged();
}

void Configuration::setNdiCleanName(const QString& name)
{
    const QString trimmed = name.trimmed().isEmpty()
        ? QStringLiteral("KAVTOR_CLEAN")
        : name.trimmed();
    if (m_ndiCleanName == trimmed) {
        return;
    }
    m_ndiCleanName = trimmed;
    emit configurationChanged();
}

void Configuration::setPanelPort(int port)
{
    if (m_panelPort == port) {
        return;
    }
    m_panelPort = port;
    emit configurationChanged();
}

void Configuration::setOscPort(int port)
{
    if (m_oscPort == port) {
        return;
    }
    m_oscPort = port;
    emit configurationChanged();
}

void Configuration::setProgramLeft(bool left)
{
    if (m_programLeft == left) {
        return;
    }
    m_programLeft = left;
    emit configurationChanged();
}

void Configuration::setNameEdge(const QString& edge)
{
    if ((edge != QLatin1String("top") && edge != QLatin1String("bottom")) || m_nameEdge == edge) {
        return;
    }
    m_nameEdge = edge;
    emit configurationChanged();
}

void Configuration::setNameAlign(const QString& align)
{
    if (align != QLatin1String("left") && align != QLatin1String("center") && align != QLatin1String("right")) {
        return;
    }
    if (m_nameAlign == align) {
        return;
    }
    m_nameAlign = align;
    emit configurationChanged();
}

void Configuration::setClockEdge(const QString& edge)
{
    if ((edge != QLatin1String("top") && edge != QLatin1String("bottom")) || m_clockEdge == edge) {
        return;
    }
    m_clockEdge = edge;
    emit configurationChanged();
}

void Configuration::setClockAlign(const QString& align)
{
    if (align != QLatin1String("left") && align != QLatin1String("center") && align != QLatin1String("right")) {
        return;
    }
    if (m_clockAlign == align) {
        return;
    }
    m_clockAlign = align;
    emit configurationChanged();
}

void Configuration::setSafePreview(bool on)
{
    if (m_safePreview == on) {
        return;
    }
    m_safePreview = on;
    emit configurationChanged();
}

void Configuration::setSafeProgram(bool on)
{
    if (m_safeProgram == on) {
        return;
    }
    m_safeProgram = on;
    emit configurationChanged();
}

void Configuration::setMeters(bool on)
{
    if (m_meters == on) {
        return;
    }
    m_meters = on;
    emit configurationChanged();
}

namespace {

bool acceptableMediaName(const QString& name)
{
    if (name.isEmpty()) {
        return true;
    }
    if (name.size() > 200) {
        return false;
    }
    for (const QChar c : name) {
        if (c.unicode() > 127) {
            return false;
        }
        if (c.isLetterOrNumber() || c == QLatin1Char('/') || c == QLatin1Char('_')
            || c == QLatin1Char('-') || c == QLatin1Char('.') || c == QLatin1Char(' ')) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

const StingerSlot* Configuration::stinger(int slot) const
{
    if (slot < 0 || slot >= m_stingers.size()) {
        return nullptr;
    }
    return &m_stingers.at(slot);
}

QJsonArray Configuration::stingersJson() const
{
    QJsonArray rows;
    for (const StingerSlot& slot : m_stingers) {
        QJsonObject row;
        row.insert(QStringLiteral("media"), slot.media);
        row.insert(QStringLiteral("reverse"), slot.reverse);
        row.insert(QStringLiteral("cutFrames"), slot.cutFrames);
        row.insert(QStringLiteral("lengthFrames"), slot.lengthFrames);
        rows.append(row);
    }
    return rows;
}

bool Configuration::replaceStingers(const QList<StingerSlot>& entries)
{
    if (entries.size() != 10) {
        return false;
    }
    for (const StingerSlot& entry : entries) {
        if (!acceptableMediaName(entry.media) || !acceptableMediaName(entry.reverse)) {
            return false;
        }
        if (entry.cutFrames < 0 || entry.cutFrames > 2999) {
            return false;
        }
        if (entry.lengthFrames <= entry.cutFrames || entry.lengthFrames > 3000) {
            return false;
        }
    }
    m_stingers = entries;
    emit configurationChanged();
    return true;
}

void Configuration::setDskSourceId(int id)
{
    setDskSource(0, id);
}

int Configuration::dskSource(int slot) const
{
    if (slot == 0) {
        return m_dskSourceId;
    }
    if (slot == 1) {
        return m_dsk2SourceId;
    }
    return -1;
}

int Configuration::keySource(int slot) const
{
    if (slot < 0 || slot >= m_keySources.size()) {
        return -1;
    }
    return m_keySources.at(slot);
}

void Configuration::setDskSource(int slot, int id)
{
    if (slot == 0) {
        if (m_dskSourceId == id) {
            return;
        }
        m_dskSourceId = id;
        emit configurationChanged();
        return;
    }
    if (slot != 1 || m_dsk2SourceId == id) {
        return;
    }
    m_dsk2SourceId = id;
    emit configurationChanged();
}

void Configuration::setKeySource(int slot, int id)
{
    setMeKeySource(0, slot, id);
}

int Configuration::meKeySource(int me, int slot) const
{
    const int index = me * 4 + slot;
    if (me < 0 || me >= 4 || slot < 0 || slot >= 4 || index >= m_meKeySources.size()) {
        return -1;
    }
    return m_meKeySources.at(index);
}

void Configuration::setMeKeySource(int me, int slot, int id)
{
    const int index = me * 4 + slot;
    if (me < 0 || me >= 4 || slot < 0 || slot >= 4 || index >= m_meKeySources.size() || m_meKeySources.at(index) == id) {
        return;
    }
    m_meKeySources[index] = id;
    if (me == 0 && slot < m_keySources.size()) {
        m_keySources[slot] = id;
    }
    emit configurationChanged();
}

void Configuration::setSource(const Source& source)
{
    if (Source* existing = sourceById(source.id)) {
        *existing = source;
        emit configurationChanged();
        return;
    }
    if (m_sources.size() >= maxSources()) {
        return;
    }
    m_sources.append(source);
    emit configurationChanged();
}

void Configuration::setSources(const QList<Source>& sources)
{
    m_sources = sources.mid(0, maxSources());
    emit configurationChanged();
}

void Configuration::setAutoDurationFrames(int frames)
{
    if (m_autoDurationFrames == frames) {
        return;
    }
    m_autoDurationFrames = frames;
    emit configurationChanged();
}

void Configuration::setWipePatternId(const QString& id)
{
    const WipePatternLookup lookup = lookupWipePattern(id);
    if (m_wipePatternId == lookup.pattern.id) {
        return;
    }
    m_wipePatternId = lookup.pattern.id;
    emit configurationChanged();
}

void Configuration::setWipeDirectionMode(WipeDirectionMode mode)
{
    if (m_wipeDirectionMode == mode) {
        return;
    }
    m_wipeDirectionMode = mode;
    emit configurationChanged();
}

void Configuration::setWipeEdgeMode(WipeEdgeMode mode)
{
    if (m_wipeEdgeMode == mode) {
        return;
    }
    m_wipeEdgeMode = mode;
    emit configurationChanged();
}

void Configuration::setWipeEdgeAmount(int amount)
{
    amount = qBound(0, amount, 40);
    if (m_wipeEdgeAmount == amount) {
        return;
    }
    m_wipeEdgeAmount = amount;
    emit configurationChanged();
}

bool Configuration::replaceWipePresets(const QList<int>& entries)
{
    if (entries.size() != 10) {
        return false;
    }
    for (int code : entries) {
        if (code < 0 || code > 999) {
            return false;
        }
    }
    if (m_wipePresets == entries) {
        return true;
    }
    m_wipePresets = entries;
    emit configurationChanged();
    return true;
}

void Configuration::setWipeMulti(int count)
{
    if (count != 1 && count != 2 && count != 4 && count != 9 && count != 16) {
        return;
    }
    if (m_wipeMulti == count) {
        return;
    }
    m_wipeMulti = count;
    emit configurationChanged();
}

void Configuration::setWipeBorderAmount(int amount)
{
    amount = qBound(0, amount, 40);
    if (m_wipeBorderAmount == amount) {
        return;
    }
    m_wipeBorderAmount = amount;
    emit configurationChanged();
}

void Configuration::setWipeShadowAmount(int amount)
{
    amount = qBound(0, amount, 40);
    if (m_wipeShadowAmount == amount) return;
    m_wipeShadowAmount = amount;
    emit configurationChanged();
}

void Configuration::setWipeAspect(int width, int height)
{
    const bool known = width >= 1 && width <= 1000 && height >= 1 && height <= 1000;
    if (!known || (m_wipeAspectW == width && m_wipeAspectH == height)) {
        return;
    }
    m_wipeAspectW = width;
    m_wipeAspectH = height;
    emit configurationChanged();
}

void Configuration::setWipePos(int x, int y)
{
    x = qBound(0, x, 1000);
    y = qBound(0, y, 1000);
    if (m_wipePosX == x && m_wipePosY == y) {
        return;
    }
    m_wipePosX = x;
    m_wipePosY = y;
    emit configurationChanged();
}

void Configuration::setDipColor(const QString& color)
{
    const QString normalized = opaqueMatteColor(color);
    if (normalized.isEmpty() || normalized == m_dipColor) return;
    m_dipColor = normalized;
    emit configurationChanged();
}

void Configuration::setWipeBorderColor(const QString& color)
{
    const QString normalized = color.startsWith(QLatin1Char('#')) ? color : (QLatin1Char('#') + color);
    if (m_wipeBorderColor == normalized) {
        return;
    }
    m_wipeBorderColor = normalized;
    emit configurationChanged();
}

void Configuration::setWindowSize(int width, int height)
{
    if (m_windowWidth == width && m_windowHeight == height) {
        return;
    }
    m_windowWidth = width;
    m_windowHeight = height;
    emit configurationChanged();
}

bool Configuration::validSafeAspect(const QString& value)
{
    return QStringList{QStringLiteral("16:9"), QStringLiteral("4:3"), QStringLiteral("9:16"),
        QStringLiteral("14:9"), QStringLiteral("1:1"), QStringLiteral("4:5")}.contains(value);
}
void Configuration::setSafePreviewAspect(const QString& value)
{
    if (!validSafeAspect(value) || value == m_safePreviewAspect) return;
    m_safePreviewAspect = value; emit configurationChanged();
}
void Configuration::setSafeProgramAspect(const QString& value)
{
    if (!validSafeAspect(value) || value == m_safeProgramAspect) return;
    m_safeProgramAspect = value; emit configurationChanged();
}
void Configuration::setSafePreset(const QString& value)
{
    if ((value != QLatin1String("ebu-r95") && value != QLatin1String("legacy")) || value == m_safePreset) return;
    m_safePreset = value; emit configurationChanged();
}

bool Configuration::setDestinations(const QList<OutputDestination>& destinations)
{
    QSet<int> ids, roles; QSet<QString> names;
    for (const auto& d : destinations) {
        const bool source = (d.source >= 0 && d.source < 24 && d.source != 11 && d.source != 23)
            || (d.source >= 1000 && d.source <= 1003) || (d.source >= 1100 && d.source <= 1103) || d.source == 1200 || d.source == 1201;
        if (d.id < 0 || d.id > 127 || ids.contains(d.id) || !source || d.aux < 0 || d.aux > 4 || d.device < 0 || d.device > 32
            || (d.type != QLatin1String("ndi") && d.type != QLatin1String("screen"))
            || d.name.contains(QLatin1Char('\n')) || d.name.contains(QLatin1Char('\r')) || d.name.contains(QChar(0))) return false;
        ids.insert(d.id);
        if (!d.enabled) continue;
        if (d.name.trimmed().isEmpty() || (d.aux && roles.contains(d.aux))) return false;
        if (d.aux) roles.insert(d.aux);
        if (d.type == QLatin1String("ndi")) { if(names.contains(d.name.trimmed())) return false; names.insert(d.name.trimmed()); }
    }
    m_destinations=destinations; emit configurationChanged(); return true;
}

QJsonObject Configuration::dmeBackgrounds() const {
    QJsonObject result;for(const auto& effect:QStringList{"global","move","cube","zoom","page_curl","page_roll"})result.insert(effect,dmeBackgroundValue(effect).isUndefined()?QJsonValue(-1):dmeBackgroundValue(effect));
    for(int code:supportedSonyDmes(true,true,true,true,true)){const QString effect=QString("sony_%1").arg(code);auto value=dmeBackgroundValue(effect);result.insert(effect,value.isUndefined()?QJsonValue(-1):value);}
    return result;
}
void Configuration::setDmeBackground(const QString& effect,int input){
    if(m_dmeBackgrounds.value(effect)==input&&(effect=="global"||dmeBackgroundCustom(effect)))return;
    m_dmeBackgrounds.insert(effect,input);if(effect!="global")m_dmeBackgroundScopes.insert(effect,true);emit configurationChanged();
}
void Configuration::setDmeBackgroundScope(const QString& effect,bool custom,bool copyGlobal){
    if(effect=="global")return;
    if(custom&&(copyGlobal||!m_dmeBackgrounds.contains(effect))){auto global=m_dmeBackgrounds.value("global");m_dmeBackgrounds.insert(effect,global.isUndefined()?QJsonValue(-1):global);}
    m_dmeBackgroundScopes.insert(effect,custom);emit configurationChanged();
}

KeyProcessing Configuration::keyProcessing(int me,int slot,bool dsk) const
{
    if(dsk)return slot>=0&&slot<2?m_dskProcessing[slot]:KeyProcessing{};
    return me>=0&&me<4&&slot>=0&&slot<4?m_keyProcessing[me*4+slot]:KeyProcessing{};
}
void Configuration::setKeyProcessing(int me,int slot,bool dsk,const KeyProcessing& processing)
{
    if(dsk) {if(slot<0||slot>=2)return;m_dskProcessing[slot]=processing;}
    else {if(me<0||me>=4||slot<0||slot>=4)return;m_keyProcessing[me*4+slot]=processing;}
    emit configurationChanged();
}

const SuperSourceLayout* Configuration::superSource(const QString& id) const {for(const auto& layout:m_superSources)if(layout.id==id)return &layout;return nullptr;}
bool Configuration::setSuperSources(const QList<SuperSourceLayout>& layouts){if(!validateSuperSources(layouts).isEmpty())return false;m_superSources=layouts;emit configurationChanged();return true;}
QString Configuration::superSourceGraphError() const {
    const bool hasSuperSources=std::any_of(m_sources.begin(),m_sources.end(),[](const Source& source){return source.enabled&&source.type==SourceType::SuperSource;});
    if(hasSuperSources){
        for(int channel:{m_programChannel,m_previewChannel,m_multiviewChannel,m_programOutputChannel})if(channel>=39&&channel<=110)return "Channels 39–110 are reserved for independent SuperSource instances.";
        for(const auto& source:m_sources)if(source.enabled&&source.type!=SourceType::MeProgram&&source.casparChannel>=39&&source.casparChannel<=110)return "Channels 39–110 are reserved for independent SuperSource instances.";
    }

    QSet<int> reserved{m_previewChannel,m_programChannel,m_multiviewChannel,m_programOutputChannel};
    for(int channel=13;channel<=18;++channel)reserved.insert(channel);for(int channel=35;channel<=38;++channel)reserved.insert(channel);
    for(const auto& source:m_sources)if(source.enabled&&source.type==SourceType::SuperSource){
        if(source.id<0||source.id>=24||source.id==11||source.id==23||source.casparChannel<1||source.casparChannel>128||reserved.contains(source.casparChannel))return "SuperSources need a dedicated input channel, not an M/E or output channel.";
        for(const auto& other:m_sources)if(other.id!=source.id&&other.id!=11&&other.id!=23&&other.enabled&&other.isAssigned()&&other.type!=SourceType::MeProgram&&other.casparChannel==source.casparChannel)return "A SuperSource channel cannot be shared with another input.";
    }
    QSet<int> visiting,done;
    std::function<bool(int)> visit=[&](int id){if(done.contains(id))return true;if(visiting.contains(id))return false;auto source=sourceById(id);if(!source||!source->enabled)return true;if(source->type!=SourceType::SuperSource)return true;
        auto layout=superSource(source->argument);if(!layout)return false;visiting.insert(id);
        for(const auto& box:layout->boxes)if(box.kind=="input"&&box.input>=0&&!visit(box.input))return false;
        visiting.remove(id);done.insert(id);return true;};
    for(const auto& source:m_sources)if(!visit(source.id))return "SuperSource layout is missing or its inputs form a cycle.";
    return {};
}

void Configuration::setWipeGeometry(int vertices,int rounding){if(vertices<3||vertices>64||rounding<0||rounding>50||(m_wipeVertices==vertices&&m_wipeRounding==rounding))return;m_wipeVertices=vertices;m_wipeRounding=rounding;emit configurationChanged();}

void Configuration::setWipeTileSize(int size){if(size<2||size>50||m_wipeTileSize==size)return;m_wipeTileSize=size;emit configurationChanged();}

void Configuration::setSuperMixGains(int a,int b){if(a<0||a>100||b<0||b>100||(a==m_superMixGainA&&b==m_superMixGainB))return;m_superMixGainA=a;m_superMixGainB=b;emit configurationChanged();}
