#pragma once
#include "KeyProcessing.h"

#include "Transition.h"

#include <array>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QHash>
#include <QtCore/QMap>
#include <QtCore/QVector>

#include "OscListener.h"
#include "NativeMultiview.h"

class Configuration;
class AmcpClient;
class QTimer;
struct Source;

class SwitcherEngine : public QObject
{
    Q_OBJECT

public:
    explicit SwitcherEngine(Configuration* configuration, AmcpClient* amcpClient, QObject* parent = nullptr);
    ~SwitcherEngine() override = default;

    Configuration* configuration() const { return m_config; }
    AmcpClient* amcpClient() const { return m_amcp; }

    bool isConnected() const { return m_connected; }
    bool nativeDmeAvailable() const { return m_nativeDmeAvailable; }
    bool setDmeBackgroundScope(const QString& effect,bool custom,bool copyGlobal=false);
    void refreshDmeBackgroundPreview(const QString& effect);
    bool setDmeBackground(const QString& effect,int source);
    QString dmeBackgroundProducer(int source,const QString& image = {}) const;
    bool pageDmeAvailable() const { return m_pageDmeAvailable; }
    bool expandedSonyAvailable() const { return m_expandedSonyAvailable; }
    bool staticDmeAvailable() const {return m_staticDmeAvailable;}
    bool broadcastMixAvailable()const {return m_broadcastMixAvailable;}
    bool setSuperMixGains(int a,int b);
    bool setDipColour(const QString& colour);
    bool nativeKeyAvailable() const {return m_nativeKeyAvailable;}
    bool frameSonyAvailable() const {return m_frameSonyAvailable;}
    bool mirrorSonyAvailable() const {return m_mirrorSonyAvailable;}
    bool planarSonyAvailable() const {return m_planarSonyAvailable;}
    bool spatialSonyAvailable() const {return m_spatialSonyAvailable;}
    bool primitiveSonyAvailable() const {return m_primitiveSonyAvailable;}
    bool compoundSonyAvailable() const {return m_compoundSonyAvailable;}
    bool mosaicSonyAvailable() const {return m_mosaicSonyAvailable;}
    bool rotarySonyAvailable() const {return m_rotarySonyAvailable;}
    bool enhancedSonyAvailable() const {return m_enhancedSonyAvailable;}
    bool sourceReady(int sourceId,int context = -1) const;
    bool sourceSelectable(int sourceId) const;
    bool setAuxSource(int role, int source);
    bool setSuperSourceInput(int sourceId,const QString& boxId,int input);
    QHash<QString,int> superSourceBindings(int sourceId,int context = -1) const;
    int superSourceChannel(int sourceId,int context) const;
    int outputSourceChannel(int source) const;
    void setMultiviewBank(int bank);
    int multiviewBank() const { return m_multiviewBank; }
    bool outputsReady() const;
    bool isPreparing() const { return !m_armBatches.isEmpty() || m_restoreBatch != 0 || (m_restoreNeeded && !m_restoreFailed); }
    bool isTransitionPreview() const { return m_transitionPreview; }
    bool isPreviewTransitioning() const { return m_previewTakeRunning || (m_manual.active && m_manual.preview); }
    bool isTransitioning() const { return m_transitioning; }
    bool isBusy() const { return m_pendingKind != PendingKind::None || m_transitioning || m_restoreBatch != 0; }
    QString activeTakeName() const;
    bool isManualTransition() const { return m_manual.active; }
    bool hasManualTransitions() const;
    int manualPosition() const { return m_manual.position; }
    bool setManualPosition(int position, const Transition& transition);
    bool isDskOn() const { return m_dskOn; }
    bool isDskOn(int slot) const;
    bool isDskMixing(int slot) const;
    bool isDskPreview() const { return m_dskPreview; }
    bool isDskPreview(int slot) const;
    int meCount() const { return kMeCount; }
    int keyerCount() const { return kKeyerCount; }
    int dskCount() const { return 2; }
    int programSource(int me) const;
    int previewSource(int me) const;
    int keySource(int me, int slot) const;
    bool keyOn(int me, int slot) const;
    bool nextKey(int me, int slot) const;
    bool nextBackground(int me) const;
    bool keyOn(int slot) const;
    bool nextBackground() const { return m_nextBackground; }
    bool nextKey(int slot) const;
    int previewSource() const { return m_previewSource; }
    int programSource() const { return m_programSource; }
    int activeMe() const { return m_activeMe; }
    int keySource(int slot) const;
    int deckSource() const { return m_deckSource; }
    int cuedSource() const { return m_cuedSource; }

    enum class DeckJogMode {
        Jog,
        Shuttle,
        Scroll
    };

public slots:
    void connectToCaspar();
    void disconnectFromCaspar();
    void selectPreview(int sourceId);
    void setActiveMe(int me);
    void hotPunchProgram(int sourceId);
    bool setTransitionPreview(bool enabled);
    void executeTransition(const Transition& transition);
    void executeCut();
    void executeAuto();
    void executeWipe();
    void executeFtb(int frames = -1);
    bool isFtb() const { return m_ftb; }
    bool executeStinger(int slot, bool reverse);
    void setWipeDirectionMode(WipeDirectionMode mode);
    void setPreviewCursor(bool visible);
    bool wipeSenseForward() const { return m_wipeSenseForward; }
    void setDskOn(bool on);
    void setDskPreview(bool on);
    void setDskPreview(int slot, bool on);
    void setDskSlot(int slot, bool on, int mixFrames);
    void setDskSource(int slot, int sourceId);
    void setKeyOn(int slot, bool on);
    void setKeySource(int slot, int sourceId);
    bool setKeyProcessing(int slot, bool dsk, const KeyProcessing& processing);
    void resetNextTransition();
    void toggleNextBackground();
    void toggleNextKey(int slot);
    void armMixer();
    void applyPreparedConfiguration();
    void selectDeckSource(int sourceId);
    void setDeckJogMode(DeckJogMode mode);
    void jogDeck(int value);
    void toggleDeckPause();
    void toggleDeckCue();
    void ingestPlayback(const OscPlayback& playback);
    void ingestFileTime(int channel, int layer, double elapsed, double duration);
    void ingestAudioPeaks(const QVector<OscAudioPeak>& peaks);
    void flushOverlayClocks();
    void flushOverlayMeters();

signals:
    void superSourceBindingChanged(int sourceId,const QString& boxId,int input);
    void keyerConfigurationCommitted();
    void renderReadinessChanged();
    void connectionStatusChanged(bool connected);
    void previewSourceChanged(int sourceId);
    void programSourceChanged(int sourceId);
    void transitioningChanged(bool transitioning);
    void dskOnChanged(bool on);
    void dskPreviewChanged(bool on);
    void layersChanged();
    void wipeSenseChanged(bool forward);
    void deckSourceChanged(int sourceId);
    void deckCueChanged(int sourceId);
    void error(const QString& message);

private slots:
    void onAmcpConnected();
    void restoreCompositions();
    void onAmcpDisconnected();
    void onAmcpConnectionFailed(const QString& message);
    void onAmcpResponse(int code, const QString& status, const QStringList& lines, const QString& command);
    void onAmcpCommandFailed(const QString& command, const QString& message);
    void onTransitionHoldFinished();
    void commitStingerCut();
    void onDeckTick();

private:
    enum class PendingKind {
        None,
        Preview,
        Transition,
        PreviewTransition,
        PreviewCleanup,
        HotPunch,
        Dsk,
        Key,
        KeySource,
        KeyProcessing,
        DskSource,
        Ftb,
        ManualStart,
        ManualPosition,
        ManualFinish
    };
    enum class TakePlan {
        None,
        Ready,
        NeedPreview,
        MissingPreview
    };

    void setConnected(bool connected);
    void setPreviewSource(int sourceId);
    void setProgramSource(int sourceId);
    void setTransitioning(bool transitioning);
    void beginTake(const Transition& transition);
    void finishTake();
    bool executePreviewTransition(const Transition& transition);
    QStringList previewCleanupCommands() const;
    int transitionHoldMs(const Transition& transition) const;
    QString takeName(TransitionType type) const;
    void send(const QString& command);
    void beginPending(PendingKind kind, const QStringList& commands, int previewId, int programId);
    void finishPending();
    void clearPending();
    struct ArmedSource {
        int channel = 0;
        QString playCommand;
        QString mixerCommand;
    };
    struct ArmedOutputs {
        QString outputPlay;
        QString pgmAdd;
        QString cleanAdd;
        QMap<int, QString> managed;
        bool operator==(const ArmedOutputs& other) const
        {
            return outputPlay == other.outputPlay && pgmAdd == other.pgmAdd && cleanAdd == other.cleanAdd && managed == other.managed;
        }
    };

    QHash<int,QHash<QString,int>> m_superSourceBindings;
    QHash<int,QString> m_failedCompositions;
    static int superSourceKey(int sourceId,int context) {return sourceId+24*context;}
    QStringList compositionCommands(int sourceId,int context = -1) const;
    void setupSuperSources();
    bool sourceWouldFeedback(int sourceId,int owner) const;
    enum class ArmKind { SourceComposePart, SourceCompose, SourcePlay, SourceMixer, SourceStop, OutputRoute, OutputProgram, OutputClean, ManagedOutput };
    struct ArmCommand {
        ArmKind kind;
        int sourceId;
        int channel;
        QString command;
        QString confirmedValue;
        int context = -1; // SuperSource instance owner; ordinary sources are shared.
    };
    void queueArmBatch(const QList<ArmCommand>& commands);
    void onArmCommandFinished(quint64 batch, const QString& command, bool success);
    void onArmBatchFinished(quint64 batch, bool success);
    void abortFailedTake();
    void requestNdiList();
    void ingestNdiList(int code, const QStringList& lines);
    void setupSources();
    void setupMultiview();
    void syncMultiviewSources();
    void syncOverlayTally();
    int sourceMe(int sourceId, int owner) const;
    bool routeWouldFeedback(int owner, int target) const;
    int m_multiviewBank = 0;
    void applyMultiviewLayout();
    void setupOutputs();
    ArmedOutputs plannedOutputs() const;
    bool programOutputUsable() const;
    int airChannel() const;
    QString ndiConsumerCommand(const QString& verb, int channel, const QString& name) const;
    QString ndiRemoveCommand(const QString& addCommand) const;
    void applyDsk(bool wait);
    TakePlan composeTake(const Transition& transition, QStringList* commands, int* newPreview, int* newProgram);
    QString opacityCommand(int channel, int layer, double value, int frames) const;
    QString layerVolumeCommand(int channel, int layer, double value, int frames) const;
    void muteAirIfBlack(QStringList* commands, int layer) const;
    void finishFtb();
    void applyDeferredBus();
    void beginDskMix(int slot, int frames);
    QStringList overlayLayer(int channel, int layer, int sourceId, bool on, int frames, int context = -1) const;
    QStringList routedKeyLayerCommands(int layer, int sourceId, const QString& suffix) const;
    QStringList shapedKeyCommands(int layer, int sourceId, bool on, const Transition& transition) const;
    QString transparentPlay(int channel, int layer) const;
    bool keyProducerReady(int sourceId) const;
    bool nextTransitionWouldChange() const;
    void syncPreviewKey(int slot);
    void syncPreviewBackground();
    void scheduleDskClear(int slot, int frames);
    int deskNextCount() const;
    QString routePlayCommand(int destChannel, int sourceChannel, const Transition& transition) const;
    QString backgroundRoute(int destChannel, int sourceId, const Transition& transition) const;
    QString mixerFillCommand(int channel, int layer, const Source& source) const;
    QStringList dskLayerCommands(int destChannel, bool on) const;
    QString dskStopCommand(int destChannel) const;
    QString resolveShareHtml(const QString& fileName) const;
    QString resolveWipeMattePath() const;
    QString wipeMatteUrl(const Transition& transition, const QString& role) const;
    void syncPreviewCursor();
    void syncMeterTimer();
    int meterSlot(int casparChannel) const;
    int mePreviewChannel(int me) const;
    int meProgramChannel(int me) const;
    int activePreviewChannel() const;
    int activeProgramChannel() const;
    int sourceChannel(int sourceId,int context = -1) const;
    void captureBank();
    void applyBank();
    QString previewCursorCall(const QString& script) const;
    QString smilWipeStingSuffix(const Transition& transition) const;
    int nativeSonyCode(const Transition& transition, bool* reverse = nullptr) const;
    QString nativeWipeOptions(const Transition& transition) const;
    int m_takeDmeBackground=-1;
    bool m_transportGraphicsAvailable=false;
    bool m_nativeDmeAvailable=false,m_pageDmeAvailable=false,m_expandedSonyAvailable=false,m_staticDmeAvailable=false,m_enhancedSonyAvailable=false,m_rotarySonyAvailable=false,m_mosaicSonyAvailable=false,m_compoundSonyAvailable=false,m_primitiveSonyAvailable=false,m_spatialSonyAvailable=false,m_planarSonyAvailable=false,m_mirrorSonyAvailable=false,m_frameSonyAvailable=false,m_nativeKeyAvailable=false,m_broadcastMixAvailable=false;
    QString nativeDmeSuffix(const Transition&, bool manual=false) const;
    QString nativeWipeSuffix(const Transition& transition, bool manual = false) const;
    QString wipeBorderLayerCommand(const Transition& transition) const;
    QStringList sourceLabelNames() const;
    QVector<bool> sourceVacantFlags() const;
    void sendNativeGraphics();
    NativeMultiview m_nativeMv;
    bool m_nativeMvReady=false;
    QByteArray m_lastNativeScene;
    QByteArray m_lastNativeValues;
    void flushOverlayLabels();
    void flushOverlayArmed();
    void playCuedIfNeeded(int sourceId, QStringList* commands);
    void resetDeckMotion(bool resetPosition = true);
    void ensureDeckPaused();
    void seedDeckFrame();
    void syncDeckClock();
    void flushDeckSeek();
    bool isDeckSeekCommand(const QString& command) const;
    void onDeckSeekFinished();
    double shuttleRate() const;
    void startOscListener();
    int sourceIdForChannel(int channel) const;

    static constexpr int kVideoFps = 50;
    static constexpr int kDskLayer = 20;
    static constexpr int kKeyLayer0 = 11;
    static constexpr int kKeyerCount = 4;
    static constexpr int kMeCount = 4;
    static constexpr int kReentrySource = 11;
    static constexpr int kWipeBorderLayer = 15;
    static constexpr int kStingerLayer = 25;
    static constexpr int kFtbLayer = 40;
    static constexpr int kJogUnitsPerFrame = 360;
    static constexpr int kScrollUnitsPerFrame = 36;
    static constexpr int kShuttleDeadzone = 256;
    static constexpr int kShuttleMax = 4096;
    static constexpr int kDeckTickMs = 40;
    static constexpr int kMeterSlots = 33;
    static constexpr int kMeterIntervalMs = 80;

    Configuration* m_config = nullptr;
    AmcpClient* m_amcp = nullptr;
    bool m_connected = false;
    bool m_transitioning = false;
    bool m_transitionPreview = false;
    bool m_previewTakeRunning = false;
    Transition m_previewStyleTransition;
    unsigned m_previewStyleRevision = 0;
    TransitionType m_activeTake = TransitionType::Cut;
    bool m_takeActive = false;
    bool m_dskOn = false;
    bool m_dskMixing[2] = {};
    int m_dskMixToken[2] = {};
    bool m_deferBus = false;
    int m_deferPreview = -1;
    int m_deferProgram = -1;
    bool m_deferKeys = false;
    bool m_deferKeyOn[kKeyerCount] = {};
    bool m_ftb = false;
    bool m_ftbFade = false;
    bool m_ftbReveal = false;
    bool m_dskPreview = false;
    bool m_dsk2On = false;
    bool m_dsk2Preview = false;
    bool m_pendingDsk2Preview = false;
    int m_pendingKeySlot = 0;
    bool m_pendingDirectKeyOn = false;
    bool m_pendingDirectKeyRouted = false;
    bool m_pendingDirectKeyPreviewLive = false;
    int m_pendingKeySource = -1;
    int m_pendingDskSource = -1;
    bool m_keyOn[kKeyerCount] = {};
    bool m_nextKey[kKeyerCount] = {};
    bool m_nextBackground = true;
    bool m_pendingKeyOn[kKeyerCount] = {};
    bool m_pendingKeyRouted[kKeyerCount] = {};
    bool m_keyPreviewLive[kKeyerCount] = {};
    bool m_keyRouted[kKeyerCount] = {};
    bool m_commitKeys = false;
    int m_pendingDskSlot = 0;
    bool m_pendingDsk2On = false;
    int m_dskFadeToken[2] = {};
    bool m_wipeSenseForward = true;
    bool m_pendingWipeFlip = false;
    bool m_stingerActive = false;
    bool m_cursorReady = false;
    bool m_cursorVisible = false;
    struct MeBank {
        int preview = -1;
        int program = -1;
        bool keyOn[4] = {};
        int keySource[4] = {-1, -1, -1, -1};
        bool nextKey[4] = {};
        bool keyPreviewLive[4] = {};
        bool keyRouted[4] = {};
        bool nextBackground = true;
        bool transitionPreview = false;
    };
    MeBank m_bank[4];
    bool m_restoreNeeded = false;
    bool m_restoreFailed = false;
    bool m_restoreVersionKnown = false;
    quint64 m_restoreBatch = 0;
    int m_activeMe = 0;
    int m_keySource[4] = {-1, -1, -1, -1};
    int m_stingerChannel = 0;
    int m_previewSource = -1;
    int m_programSource = -1;
    int m_deckSource = -1;
    int m_cuedSource = -1;
    bool m_deckPaused = false;
    DeckJogMode m_deckJogMode = DeckJogMode::Jog;
    qint64 m_jogUnits = 0;
    int m_pendingJogFrames = 0;
    int m_shuttleValue = 0;
    double m_shuttleAcc = 0;
    int m_deckFrame = 0;
    bool m_deckFrameKnown = false;
    bool m_deckSeekInFlight = false;
    int m_deckSentFrame = -1;
    QString m_deckSeekCommand;
    PendingKind m_pendingKind = PendingKind::None;
    ::KeyProcessing m_pendingProcessing;
    int m_pendingProcessingSlot = 0;
    bool m_pendingProcessingDsk = false;
    quint64 m_pendingBatch = 0;
    int m_transitionHoldMs = 0;
    int m_pendingDskFrames = 0;
    int m_pendingPreview = -1;
    int m_pendingProgram = -1;
    bool m_pendingDskOn = false;
    bool m_pendingDskPreview = false;
    QStringList m_pendingCommands;
    QHash<int, ArmedSource> m_armedSources;
    QHash<quint64, QList<ArmCommand>> m_armBatches;
    ArmedOutputs m_armedOutputs;
    QStringList m_ndiNames;
    bool m_ndiListReady = false;
    bool m_ndiListRetryScheduled = false;
    OscListener* m_osc = nullptr;
    QTimer* m_clockTimer = nullptr;
    void flushManualPosition();
    void finishManualTransition();
    QTimer* m_manualTimer = nullptr;
    struct ManualState {
        bool active=false, ready=false, background=false, preview=false, endpoint=false, native=false;
        int position=0, sent=-1;
        unsigned revision=0;
        Transition transition;
        std::array<bool,4> keys{}, keyBefore{};
    };
    unsigned m_wipeRevision = 0;
    ManualState m_manual;
    std::array<ManualState,4> m_manualBanks{};
    int m_requestedMe = -1;
    QTimer* m_transitionTimer = nullptr;
    QTimer* m_stingerCutTimer = nullptr;
    QTimer* m_deckTimer = nullptr;
    qint64 m_wipeClockMs = 0;
    QVector<OscFileTime> m_clipClocks;
    qint64 m_lastOverlayHeartbeat = 0;
    int m_meterHold[kMeterSlots][2] = {};
    QTimer* m_meterTimer = nullptr;
};
