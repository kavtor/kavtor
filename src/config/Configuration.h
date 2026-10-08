#pragma once

#include "core/Transition.h"
#include "core/SuperSource.h"
#include "core/KeyProcessing.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QRectF>
#include <array>

enum class SourceType {
    Decklink,
    Ndi,
    File,
    Html,
    Ffmpeg,
    Still,
    ColorBars,
    MeProgram,
    Matte,
    V4l2,
    SuperSource
};

QString colorBarsPattern(const QString& argument);
QString opaqueMatteColor(const QString& value);
QString staticDmeImageProducer(const QString& path);
QString sourceTypeToString(SourceType type);
bool sourceTypeFromString(const QString& text, SourceType* type);
QRectF letterboxFill(double sourceAspect, double destAspect);
QString resolveNdiSourceName(const QString& argument, const QStringList& discovered);
QString ndiProducerCommand(const QString& argument, const QStringList& discovered = {});
QStringList parseNdiListLines(const QStringList& lines);

struct StingerSlot {
    QString media;
    QString reverse;
    int cutFrames = 12;
    int lengthFrames = 50;
};

// Source IDs 0..23 are inputs; 1000..1003 M/E PGM, 1100..1103 M/E PVW,
// 1200 multiview, 1201 final program including DSK/FTB. AUX uses channels 35..38.
struct OutputDestination {
    int id = 0; QString name; QString type = QStringLiteral("ndi");
    int source = 1201; int aux = 0; int device = 0;
    bool enabled = false; bool fullscreen = true;
};

struct Source {
    int id = 0;
    QString name;
    SourceType type = SourceType::File;
    int casparChannel = 1;
    QString argument;
    bool enabled = true;
    bool loop = true; // Preserve the repeat behaviour of existing projects.

    QString producerCommand(int width = 1920, int height = 1080) const;
    QRectF mixerFill(int destWidth = 1920, int destHeight = 1080) const;
    bool isAssigned() const;
    bool isClip() const;
};

class Configuration : public QObject
{
    Q_OBJECT

public:
    explicit Configuration(QObject* parent = nullptr);
    ~Configuration() override = default;

    bool load();
    bool save() const;

    QString configFilePath() const;
    void setConfigFilePath(const QString& path);

    QString casparHost() const { return m_casparHost; }
    int casparPort() const { return m_casparPort; }
    int previewChannel() const { return m_previewChannel; }
    int programChannel() const { return m_programChannel; }
    int multiviewChannel() const { return m_multiviewChannel; }
    int programOutputChannel() const { return m_programOutputChannel; }
    bool ndiProgramEnabled() const { return m_ndiProgramEnabled; }
    QString ndiProgramName() const { return m_ndiProgramName; }
    bool ndiCleanEnabled() const { return m_ndiCleanEnabled; }
    QString ndiCleanName() const { return m_ndiCleanName; }
    int panelPort() const { return m_panelPort; }
    int oscPort() const { return m_oscPort; }
    bool programLeft() const { return m_programLeft; }
    QString nameEdge() const { return m_nameEdge; }
    QString nameAlign() const { return m_nameAlign; }
    QString clockEdge() const { return m_clockEdge; }
    QString clockAlign() const { return m_clockAlign; }
    QString safePreviewAspect() const { return m_safePreviewAspect; }
    QString safeProgramAspect() const { return m_safeProgramAspect; }
    QString safePreset() const { return m_safePreset; }
    static bool validSafeAspect(const QString& value);
    void setSafePreviewAspect(const QString& value);
    void setSafeProgramAspect(const QString& value);
    void setSafePreset(const QString& value);
    bool safePreview() const { return m_safePreview; }
    bool safeProgram() const { return m_safeProgram; }
    bool meters() const { return m_meters; }
    QJsonObject multiviewLayoutJson() const;
    QList<StingerSlot> stingers() const { return m_stingers; }
    const StingerSlot* stinger(int slot) const;
    QJsonArray stingersJson() const;
    bool replaceStingers(const QList<StingerSlot>& entries);
    int dskSourceId() const { return m_dskSourceId; }
    int dskSource(int slot) const;
    int keySource(int slot) const;
    int meKeySource(int me, int slot) const;
    KeyProcessing keyProcessing(int me, int slot, bool dsk = false) const;
    void setKeyProcessing(int me, int slot, bool dsk, const KeyProcessing& processing);
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }

    QList<SuperSourceLayout> superSources() const { return m_superSources; }
    const SuperSourceLayout* superSource(const QString& id) const;
    bool setSuperSources(const QList<SuperSourceLayout>& layouts);
    QString superSourceGraphError() const;
    QList<Source> sources() const { return m_sources; }
    const Source* sourceById(int id) const;
    Source* sourceById(int id);
    QString sourceName(int id) const;
    QList<OutputDestination> destinations() const { return m_destinations; }
    bool setDestinations(const QList<OutputDestination>& destinations);
    int maxSources() const { return 24; }

    QJsonObject dmeBackgrounds() const;
    QJsonObject dmeBackgroundScopes() const {return m_dmeBackgroundScopes;}
    bool dmeBackgroundCustom(const QString& effect) const {return effect!="global"&&m_dmeBackgroundScopes.value(effect).toBool(false);}
    QJsonValue dmeBackgroundValue(const QString& effect) const {return m_dmeBackgrounds.value(effect=="global"||dmeBackgroundCustom(effect)?effect:"global");}
    int dmeBackground(const QString& effect) const {auto value=dmeBackgroundValue(effect);return value.isObject()?-3:value.toInt(-1);}
    QString dmeBackgroundImage(const QString& effect) const {return dmeBackgroundValue(effect).toObject().value("image").toString();}
    bool setDmeBackgroundImage(const QString& effect,const QString& path);
    void setDmeBackground(const QString& effect,int input);
    void setDmeBackgroundScope(const QString& effect,bool custom,bool copyGlobal=false);

    int wipeBorderSide()const{return m_wipeBorderSide;}
    int wipeInnerSoft()const{return m_wipeInnerSoft;}
    int wipeOuterSoft()const{return m_wipeOuterSoft;}
    void setWipeBorderProfile(int side,int inner,int outer);
    int dustRatio()const{return m_dustRatio;}
    int dustSize()const{return m_dustSize;}
    int dustFlash()const{return m_dustFlash;}
    void setDustMix(int ratio,int size,int flash);
    int superMixGainA()const {return m_superMixGainA;}
    int superMixGainB()const {return m_superMixGainB;}
    void setSuperMixGains(int a,int b);
    QString dipColor() const { return m_dipColor; }
    void setDipColor(const QString& color);
    int autoDurationFrames() const { return m_autoDurationFrames; }
    QString wipePatternId() const { return m_wipePatternId; }
    WipeDirectionMode wipeDirectionMode() const { return m_wipeDirectionMode; }
    WipeEdgeMode wipeEdgeMode() const { return m_wipeEdgeMode; }
    int wipeEdgeAmount() const { return m_wipeEdgeAmount; }
    QString wipeBorderColor() const { return m_wipeBorderColor; }
    QList<int> wipePresets() const { return m_wipePresets; }
    bool replaceWipePresets(const QList<int>& entries);
    int wipeMulti() const { return m_wipeMulti; }
    int wipeTileSize() const {return m_wipeTileSize;}
    void setWipeTileSize(int size);
    int wipeVertices() const {return m_wipeVertices;}
    int wipeRounding() const {return m_wipeRounding;}
    void setWipeGeometry(int vertices,int rounding);
    int wipeShadowAmount() const { return m_wipeShadowAmount; }
    int wipeBorderAmount() const { return m_wipeBorderAmount; }
    int wipeAspectW() const { return m_wipeAspectW; }
    int wipeAspectH() const { return m_wipeAspectH; }
    int wipePosX() const { return m_wipePosX; }
    int wipePosY() const { return m_wipePosY; }

    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }

    void setCasparHost(const QString& host);
    void setCasparPort(int port);
    void setPreviewChannel(int channel);
    void setProgramChannel(int channel);
    void setMultiviewChannel(int channel);
    void setProgramOutputChannel(int channel);
    void setNdiProgramEnabled(bool enabled);
    void setNdiProgramName(const QString& name);
    void setNdiCleanEnabled(bool enabled);
    void setNdiCleanName(const QString& name);
    void setPanelPort(int port);
    void setOscPort(int port);
    void setProgramLeft(bool left);
    void setNameEdge(const QString& edge);
    void setNameAlign(const QString& align);
    void setClockEdge(const QString& edge);
    void setClockAlign(const QString& align);
    void setSafePreview(bool on);
    void setSafeProgram(bool on);
    void setMeters(bool on);
    void setDskSourceId(int id);
    void setDskSource(int slot, int id);
    void setKeySource(int slot, int id);
    void setMeKeySource(int me, int slot, int id);
    void setSource(const Source& source);
    void setSources(const QList<Source>& sources);
    void setAutoDurationFrames(int frames);
    void setWipePatternId(const QString& id);
    void setWipeDirectionMode(WipeDirectionMode mode);
    void setWipeEdgeMode(WipeEdgeMode mode);
    void setWipeEdgeAmount(int amount);
    void setWipeBorderColor(const QString& color);
    void setWipeMulti(int count);
    void setWipeBorderAmount(int amount);
    void setWipeShadowAmount(int amount);
    void setWipeAspect(int width, int height);
    void setWipePos(int x, int y);
    void setWindowSize(int width, int height);

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj);

signals:
    void configurationChanged();

private:
    bool applyJson(const QJsonObject& obj);
    void setDefaults();
    QString defaultConfigFilePath() const;

    QString m_configFilePath;
    QJsonObject m_dmeBackgrounds,m_dmeBackgroundScopes;
    QList<SuperSourceLayout> m_superSources;
    QString m_casparHost;
    int m_casparPort = 5250;
    int m_previewChannel = 9;
    int m_programChannel = 10;
    int m_multiviewChannel = 11;
    int m_programOutputChannel = 12;
    bool m_ndiProgramEnabled = true;
    QString m_ndiProgramName;
    bool m_ndiCleanEnabled = true;
    QString m_ndiCleanName;
    int m_panelPort = 9100;
    int m_oscPort = 6250;
    bool m_programLeft = true;
    QString m_nameEdge = QStringLiteral("bottom");
    QString m_nameAlign = QStringLiteral("center");
    QString m_clockEdge = QStringLiteral("top");
    QString m_clockAlign = QStringLiteral("center");
    QString m_safePreviewAspect = QStringLiteral("16:9");
    QString m_safeProgramAspect = QStringLiteral("16:9");
    QString m_safePreset = QStringLiteral("ebu-r95");
    bool m_safePreview = true;
    bool m_safeProgram = false;
    bool m_meters = false;
    QList<StingerSlot> m_stingers;
    int m_dskSourceId = 7;
    int m_dsk2SourceId = -1;
    QList<int> m_keySources;
    QList<int> m_meKeySources;
    std::array<KeyProcessing,16> m_keyProcessing{};
    std::array<KeyProcessing,2> m_dskProcessing{};
    int m_videoWidth = 1920;
    int m_videoHeight = 1080;
    QList<OutputDestination> m_destinations;
    QList<Source> m_sources;
    QString m_dipColor = QStringLiteral("#000000");
    int m_wipeBorderSide=0,m_wipeInnerSoft=-1,m_wipeOuterSoft=-1;
    int m_dustRatio=50,m_dustSize=2,m_dustFlash=0;
    int m_superMixGainA=100,m_superMixGainB=100;
    int m_autoDurationFrames = 25;
    QString m_wipePatternId;
    WipeDirectionMode m_wipeDirectionMode = WipeDirectionMode::Forward;
    WipeEdgeMode m_wipeEdgeMode = WipeEdgeMode::Hard;
    int m_wipeEdgeAmount = 8;
    QString m_wipeBorderColor;
    QList<int> m_wipePresets;
    int m_wipeMulti = 1;
    int m_wipeVertices=5,m_wipeRounding=15,m_wipeTileSize=10;
    int m_wipeBorderAmount = 0;
    int m_wipeShadowAmount = 0;
    int m_wipeAspectW = 1;
    int m_wipeAspectH = 1;
    int m_wipePosX = 500;
    int m_wipePosY = 500;
    int m_windowWidth = 720;
    int m_windowHeight = 480;
};
