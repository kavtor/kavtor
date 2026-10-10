#include "SwitcherEngine.h"
#include "MoveTransition.h"
#include <QRegularExpression>
#include <QVersionNumber>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include "config/Configuration.h"
#include "AmcpClient.h"
#include "SystemMonitor.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRectF>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QDebug>
#include <QtCore/QtGlobal>
#include <cmath>
#include <functional>

SwitcherEngine::SwitcherEngine(Configuration* configuration, AmcpClient* amcpClient, QObject* parent)
    : QObject(parent)
    , m_config(configuration)
    , m_amcp(amcpClient)
{
    Q_ASSERT(m_config);
    Q_ASSERT(m_amcp);

    connect(m_config, &Configuration::configurationChanged, this, [this]() {
        if (m_connected) {
            applyMultiviewLayout();
        }
        syncMeterTimer();
    });
    connect(m_amcp, &AmcpClient::connected, this, &SwitcherEngine::onAmcpConnected);
    connect(m_amcp, &AmcpClient::disconnected, this, &SwitcherEngine::onAmcpDisconnected);
    connect(m_amcp, &AmcpClient::connectionFailed, this, &SwitcherEngine::onAmcpConnectionFailed);
    connect(m_amcp, &AmcpClient::responseReceived, this, &SwitcherEngine::onAmcpResponse);
    connect(m_amcp, &AmcpClient::commandFailed, this, &SwitcherEngine::onAmcpCommandFailed);
    connect(m_amcp, &AmcpClient::batchCommandFinished, this, &SwitcherEngine::onArmCommandFinished);
    connect(m_amcp, &AmcpClient::batchFinished, this, &SwitcherEngine::onArmBatchFinished);

    connect(this, &SwitcherEngine::previewSourceChanged, this, [this](int) { syncOverlayTally(); });
    connect(this, &SwitcherEngine::programSourceChanged, this, [this](int) { syncOverlayTally(); });
    connect(this, &SwitcherEngine::transitioningChanged, this, [this](bool) { syncOverlayTally(); });
    connect(this, &SwitcherEngine::layersChanged, this, [this]() { syncOverlayTally(); });
    auto* systemMonitor = new SystemMonitor(this);
    connect(this, &SwitcherEngine::connectionStatusChanged, systemMonitor, [systemMonitor](bool connected){if(connected)systemMonitor->start();else systemMonitor->stop();});
    connect(systemMonitor, &SystemMonitor::changed, this, [this](const QJsonObject& metrics){
        m_nativeMv.system=metrics; sendNativeGraphics();
    });
    m_clipClocks.resize(m_config->maxSources());
    m_osc = new OscListener(this);
    connect(m_osc,&OscListener::playbackReceived,this,&SwitcherEngine::ingestPlayback);
    connect(m_osc, &OscListener::fileTimeReceived, this, &SwitcherEngine::ingestFileTime);
    connect(m_osc, &OscListener::audioPeaksReceived, this, &SwitcherEngine::ingestAudioPeaks);
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(250);
    connect(m_clockTimer, &QTimer::timeout, this, &SwitcherEngine::flushOverlayClocks);
    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(kMeterIntervalMs);
    connect(m_meterTimer, &QTimer::timeout, this, &SwitcherEngine::flushOverlayMeters);
    connect(m_config, &Configuration::configurationChanged, this, [this] { ++m_wipeRevision; });
    m_manualTimer = new QTimer(this);
    m_manualTimer->setInterval(20);
    connect(m_manualTimer, &QTimer::timeout, this, &SwitcherEngine::flushManualPosition);
    m_transitionTimer = new QTimer(this);
    m_transitionTimer->setSingleShot(true);
    connect(m_transitionTimer, &QTimer::timeout, this, &SwitcherEngine::onTransitionHoldFinished);
    m_stingerCutTimer = new QTimer(this);
    m_stingerCutTimer->setSingleShot(true);
    connect(m_stingerCutTimer, &QTimer::timeout, this, &SwitcherEngine::commitStingerCut);
    m_deckTimer = new QTimer(this);
    m_deckTimer->setInterval(kDeckTickMs);
    connect(m_deckTimer, &QTimer::timeout, this, &SwitcherEngine::onDeckTick);
    for (int me = 0; me < kMeCount; ++me) {
        for (int slot = 0; slot < kKeyerCount; ++slot) {
            m_bank[me].keySource[slot] = m_config->meKeySource(me, slot);
        }
    }
    applyBank();
    startOscListener();
}

void SwitcherEngine::connectToCaspar()
{
    m_amcp->connectToHost(m_config->casparHost(), m_config->casparPort());
}

void SwitcherEngine::disconnectFromCaspar()
{
    m_amcp->disconnectFromHost();
}

void SwitcherEngine::selectPreview(int sourceId)
{
    if (m_manual.active) return;
    const int channel = sourceChannel(sourceId);
    if (channel < 0) {
        return;
    }
    if (!m_connected) {
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (isBusy()) {
        emit error(tr("Mixer is busy"));
        return;
    }
    if (!m_nextBackground) {
        if (sourceId != m_previewSource) {
            setPreviewSource(sourceId);
        }
        return;
    }
    if (sourceId == m_previewSource) {
        return;
    }

    beginPending(PendingKind::Preview,
                 {backgroundRoute(activePreviewChannel(), sourceId, Transition::cut())},
                 sourceId,
                 m_programSource);
}

void SwitcherEngine::hotPunchProgram(int sourceId)
{
    const int channel = sourceChannel(sourceId);
    if (channel < 0) {
        return;
    }
    if (!m_connected) {
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (sourceId == m_programSource && m_pendingKind == PendingKind::None) {
        return;
    }
    if (isBusy()) {
        return;
    }

    QStringList commands;
    playCuedIfNeeded(sourceId, &commands);
    commands.append(backgroundRoute(activeProgramChannel(), sourceId, Transition::cut()));
    commands.append(wipeBorderLayerCommand(Transition::cut()));
    beginPending(PendingKind::HotPunch, commands, m_previewSource, sourceId);
}

void SwitcherEngine::setActiveMe(int me)
{
    if (me < 0 || me >= kMeCount) return;
    // The latest delegation wins, including a return to the current M/E while
    // a previous request is waiting for an AMCP batch to finish.
    if (me == m_activeMe) { m_requestedMe = -1; return; }
    if (isBusy() && !m_manual.active) return;
    if (m_pendingKind != PendingKind::None) { m_requestedMe = me; return; }
    m_requestedMe = -1;
    m_manualBanks[m_activeMe] = m_manual;
    m_manual = m_manualBanks[me];
    m_takeActive = m_manual.active;
    m_activeTake = m_manual.transition.type;
    if (m_manual.active) m_manualTimer->start(); else m_manualTimer->stop();
    captureBank();
    m_activeMe = me;
    m_cursorReady = false;
    applyBank();
    setTransitioning(m_manual.active);
    if (m_connected) {
        syncPreviewBackground();
        for (int slot = 0; slot < kKeyerCount; ++slot) {
            syncPreviewKey(slot);
        }
        syncMultiviewSources();
        flushOverlayLabels();
        syncPreviewCursor();
    }
    emit previewSourceChanged(m_previewSource);
    emit programSourceChanged(m_programSource);
    emit layersChanged();
}

void SwitcherEngine::executeTransition(const Transition& requested)
{
    Transition transition = requested;
    if (transition.type == TransitionType::Wipe && ((transition.edge != WipeEdgeMode::Hard && transition.edgeAmount > 0)
        || transition.borderAmount > 0 || transition.shadowAmount > 0 || transition.multi > 1
        || transition.aspectW != transition.aspectH || transition.posX != 500 || transition.posY != 500)) {
        const bool vertical = transition.direction == TransitionDirection::FromTop
            || transition.direction == TransitionDirection::FromBottom;
        transition.type = TransitionType::Smil;
        transition.smilType = QStringLiteral("barWipe");
        transition.smilSubtype = vertical ? QStringLiteral("topToBottom") : QStringLiteral("leftToRight");
        transition.reverse = transition.direction == TransitionDirection::FromRight
            || transition.direction == TransitionDirection::FromBottom;
    }
    if (transition.type == TransitionType::Smil && resolveWipeMattePath().isEmpty()) {
        emit error(tr("Wipe matte HTML is missing"));
        return;
    }
    if (transition.type == TransitionType::Dip) {
        transition.dipColor = opaqueMatteColor(transition.dipColor.isEmpty() ? m_config->dipColor() : transition.dipColor);
        if (transition.dipColor.isEmpty()) { emit error(tr("DIP needs an opaque RGB colour")); return; }
    }
    if (!m_connected) {
        m_pendingWipeFlip = false;
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (isBusy()) {
        m_pendingWipeFlip = false;
        return;
    }

    transition.borderSide=m_config->wipeBorderSide();transition.innerSoft=m_config->wipeInnerSoft();transition.outerSoft=m_config->wipeOuterSoft();
    transition.dustRatio=m_config->dustRatio();transition.dustSize=m_config->dustSize();transition.dustFlash=m_config->dustFlash();
    transition.videoGainA=m_config->superMixGainA();transition.videoGainB=m_config->superMixGainB();
    const bool mesh=transition.type==TransitionType::Move||transition.type==TransitionType::Cube||transition.type==TransitionType::Zoom||transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll||transition.type==TransitionType::SonyDme||transition.type==TransitionType::Nam||(transition.type==TransitionType::SuperMix||transition.type==TransitionType::DustMix);
    if(mesh&&nativeDmeSuffix(transition).isEmpty()){emit error(tr("DME scene cannot be prepared safely"));return;}
    if(transition.type==TransitionType::Smil&&transition.smilType=="sonyWipe"&&!nativeSonyCode(transition)){emit error(tr("Sony pattern or modifiers are unavailable in the connected engine"));return;}
    if(transition.type==TransitionType::Move||transition.type==TransitionType::Cube||transition.type==TransitionType::Zoom||transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll||transition.type==TransitionType::SonyDme){const QString effect=transition.type==TransitionType::Move?"move":transition.type==TransitionType::Cube?"cube":transition.type==TransitionType::Zoom?"zoom":transition.type==TransitionType::SonyDme?QString("sony_%1").arg(transition.sonyDmeCode):transition.type==TransitionType::PageCurl?"page_curl":"page_roll";if(transition.dmeBackground==-2){transition.dmeBackground=m_config->dmeBackground(effect);transition.dmeBackgroundImage=m_config->dmeBackgroundImage(effect);}if((transition.dmeBackground==-3&&!m_staticDmeAvailable)||dmeBackgroundProducer(transition.dmeBackground,transition.dmeBackgroundImage).isEmpty()||sourceWouldFeedback(transition.dmeBackground,m_activeMe)){emit error(tr("DME background is unavailable or would feed back"));return;}}
    if((transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll)&&!m_pageDmeAvailable){emit error(tr("Page DME requires casparMIX 0.6.0 or newer"));return;}
    if((transition.type==TransitionType::Move||transition.type==TransitionType::Cube||transition.type==TransitionType::Zoom||transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll||transition.type==TransitionType::SonyDme||transition.type==TransitionType::Nam||(transition.type==TransitionType::SuperMix||transition.type==TransitionType::DustMix))&&!m_nativeDmeAvailable){emit error(tr("Native DME requires casparMIX 0.5.0 or newer"));return;}
    if((transition.type==TransitionType::Move||transition.type==TransitionType::Cube||transition.type==TransitionType::Zoom||transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll||transition.type==TransitionType::SonyDme||transition.type==TransitionType::Nam||(transition.type==TransitionType::SuperMix||transition.type==TransitionType::DustMix))&&(!m_nextBackground||std::any_of(std::begin(m_nextKey),std::end(m_nextKey),[](bool on){return on;}))) {emit error(tr("Native DME requires background-only NEXT TRANSITION"));return;}
    if(transition.alternateMix()&&(!m_nextBackground||std::any_of(std::begin(m_nextKey),std::end(m_nextKey),[](bool on){return on;}))) {
        emit error(tr("Alternate MIX modes currently require background-only NEXT TRANSITION"));return;
    }
    if (m_transitionPreview) {
        executePreviewTransition(transition);
        return;
    }

    QStringList commands;
    int newPreview = m_previewSource;
    int newProgram = m_programSource;
    m_wipeClockMs = QDateTime::currentMSecsSinceEpoch();
    const TakePlan plan = composeTake(transition, &commands, &newPreview, &newProgram);
    if (plan == TakePlan::NeedPreview) {
        m_wipeClockMs = 0;
        m_pendingWipeFlip = false;
        emit error(tr("No preview source selected"));
        return;
    }
    if (plan == TakePlan::MissingPreview) {
        m_wipeClockMs = 0;
        m_pendingWipeFlip = false;
        emit error(tr("Preview source is no longer configured"));
        return;
    }
    if (plan != TakePlan::Ready) {
        m_wipeClockMs = 0;
        m_pendingWipeFlip = false;
        return;
    }

    beginTake(transition);
    beginPending(PendingKind::Transition, commands, newPreview, newProgram);
}

bool SwitcherEngine::setTransitionPreview(bool enabled)
{
    if (isBusy()) { emit error(tr("Mixer is busy")); return false; }
    if (m_transitionPreview == enabled) return true;
    m_transitionPreview = enabled;
    emit layersChanged();
    return true;
}

QStringList SwitcherEngine::previewCleanupCommands() const
{
    // Only private rehearsal layers; never touch layers routed into program.
    QStringList commands;
    for (int layer : {100, 101, 102, 103, 104, 105, 106, 107, 115, 190, 111, 112, 113, 114, 120, 121})
        commands.append(QStringLiteral("CLEAR %1-%2").arg(activePreviewChannel()).arg(layer));
    return commands;
}

bool SwitcherEngine::executePreviewTransition(const Transition& transition)
{
    if (!nextTransitionWouldChange()) {
        emit error(tr("No configured source in NEXT TRANSITION"));
        m_pendingWipeFlip = false;
        return false;
    }
    const int preview = activePreviewChannel();
    const int program = activeProgramChannel();
    // Re-target only the command destination. Embedded routes stay unchanged.
    const auto target = [preview, program](const QString& command) {
        const QStringList parts = command.split(QLatin1Char(' '));
        if (parts.size() < 2) return QString();
        const QString prefix = QString::number(program) + QLatin1Char('-');
        if (!parts[1].startsWith(prefix)) return QString();
        bool ok = false;
        const int layer = parts[1].mid(prefix.size()).toInt(&ok);
        if (!ok || (layer != 1 && (layer < 11 || layer > 15))) return QString();
        QString result = command;
        result.replace(parts[0].size() + 1, parts[1].size(),
            QStringLiteral("%1-%2").arg(preview).arg(100 + layer));
        return result;
    };
    m_wipeClockMs = QDateTime::currentMSecsSinceEpoch();
    QStringList commands = previewCleanupCommands();
    for (int layer : {100, 101, 102, 103, 104, 105, 106, 107, 115, 190, 111, 112, 113, 114, 120, 121})
        commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(preview).arg(layer));
    commands.append(QStringLiteral("PLAY %1-100 %2").arg(preview).arg(transition.type == TransitionType::Dip ? transition.dipColor : QStringLiteral("#000000")));
    commands.append(QStringLiteral("PLAY %1-101 #000000").arg(preview));
    if (m_programSource >= 0)
        commands.append(QStringLiteral("PLAY %1-101 route://%2-1").arg(preview).arg(program));
    for (int key = 0; key < kKeyerCount; ++key) {
        if (m_keyOn[key])
            commands.append(QStringLiteral("PLAY %1-%2 route://%3-%4")
                .arg(preview).arg(111 + key).arg(program).arg(kKeyLayer0 + key));
    }
    // Preserve DSK visibility from the normal preview, above the rehearsal.
    if (m_activeMe == 0) {
        for (int slot = 0; slot < dskCount(); ++slot) {
            if (isDskOn(slot) || isDskPreview(slot))
                commands.append(QStringLiteral("PLAY %1-%2 route://%1-%3")
                    .arg(preview).arg(120 + slot).arg(kDskLayer + slot));
        }
    }
    if (m_nextBackground) {
        if (sourceChannel(m_previewSource) < 0) {
            emit error(tr("No configured preview source selected"));
            m_pendingWipeFlip = false;
            return false;
        }
        commands.append(target(backgroundRoute(program, m_previewSource, transition)));
        commands.append(target(wipeBorderLayerCommand(transition)));
    }
    m_wipeClockMs = QDateTime::currentMSecsSinceEpoch();
    for (int key = 0; key < kKeyerCount; ++key) {
        if (!m_nextKey[key]) continue;
        const bool entering = !m_keyOn[key];
        if (entering && !keyProducerReady(m_keySource[key])) continue;
        const int layer = kKeyLayer0 + key;
        QStringList change;
        if (entering && transition.type == TransitionType::Mix && transition.durationFrames > 0)
            change = routedKeyLayerCommands(layer, m_keySource[key],
                QStringLiteral(" MIX %1 LINEAR").arg(transition.durationFrames));
        if (change.isEmpty()) change = shapedKeyCommands(layer, m_keySource[key], entering, transition);
        if (change.isEmpty()) change = overlayLayer(program, layer, m_keySource[key], entering,
            transition.type == TransitionType::Mix ? transition.durationFrames : 0);
        for (const QString& command : change) commands.append(target(command));
    }
    if (commands.contains(QString())) {
        emit error(tr("Unsupported transition preview command"));
        return false;
    }
    m_pendingWipeFlip = false; // Rehearsal does not consume the next WIPE direction.
    m_previewTakeRunning = true;
    m_previewStyleTransition = transition;
    m_previewStyleRevision = m_wipeRevision;
    if(nativeSonyCode(transition))m_manualTimer->start();
    beginTake(transition);
    beginPending(PendingKind::PreviewTransition, commands, m_previewSource, m_programSource);
    return true;
}

void SwitcherEngine::executeCut()
{
    m_pendingWipeFlip = false;
    executeTransition(Transition::cut());
}

void SwitcherEngine::executeFtb(int frames)
{
    if (!m_connected) {
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (isBusy()) {
        return;
    }
    if (frames < 1 || frames > 1000) {
        frames = qBound(1, m_config->autoDurationFrames(), 1000);
    }
    const bool down = !m_ftb;
    const int air = airChannel();
    QStringList commands;
    if (down) {
        // Layer 40 sits above both DSK layers, so the black covers the whole line.
        commands.append(QStringLiteral("PLAY %1-%2 #000000").arg(air).arg(kFtbLayer));
        commands.append(QStringLiteral("MIXER %1-%2 FILL 0 0 1 1").arg(air).arg(kFtbLayer));
        commands.append(opacityCommand(air, kFtbLayer, 0.0, 0));
        commands.append(opacityCommand(air, kFtbLayer, 1.0, frames));
    } else {
        commands.append(opacityCommand(air, kFtbLayer, 0.0, frames));
    }
    const double volume = down ? 0.0 : 1.0;
    const int audioLayers[] = {1, kDskLayer, kDskLayer + 1};
    for (int layer : audioLayers) {
        commands.append(layerVolumeCommand(air, layer, volume, frames));
    }
    m_ftb = true;
    m_ftbReveal = !down;
    m_ftbFade = true;
    emit layersChanged();
    setTransitioning(true);
    m_transitionHoldMs = transitionHoldMs(Transition::mix(frames));
    beginPending(PendingKind::Ftb, commands, m_previewSource, m_programSource);
}

void SwitcherEngine::finishFtb()
{
    m_ftbFade = false;
    if (m_ftbReveal) {
        m_ftbReveal = false;
        if (m_connected) {
            send(QStringLiteral("CLEAR %1-%2").arg(airChannel()).arg(kFtbLayer));
        }
        m_ftb = false;
        emit layersChanged();
    }
    setTransitioning(false);
}

void SwitcherEngine::executeAuto()
{
    m_pendingWipeFlip = false;
    executeTransition(Transition::mix(m_config->autoDurationFrames()));
}

bool SwitcherEngine::executeStinger(int slot, bool reverse)
{
    if (m_transitionPreview) {
        emit error(tr("Stinger transition preview is not available yet"));
        return false;
    }
    const StingerSlot* configured = m_config->stinger(slot);
    if (!configured || !m_connected || isBusy() || !nextTransitionWouldChange()) {
        return false;
    }
    const QString media = (reverse && !configured->reverse.isEmpty()) ? configured->reverse : configured->media;
    if (media.isEmpty()) {
        return false;
    }

    m_pendingWipeFlip = false;
    m_stingerActive = true;
    m_stingerChannel = activeProgramChannel();
    send(QStringLiteral("PLAY %1-%2 \"%3\"").arg(m_stingerChannel).arg(kStingerLayer).arg(media));
    Transition hold;
    hold.type = TransitionType::Wipe;
    hold.durationFrames = configured->lengthFrames;
    beginTake(hold);
    m_transitionTimer->start(m_transitionHoldMs);
    const int cutMs = (configured->cutFrames * 1000 + (kVideoFps - 1)) / kVideoFps;
    m_stingerCutTimer->start(qMax(0, cutMs));
    return true;
}

void SwitcherEngine::commitStingerCut()
{
    if (!m_stingerActive || !m_connected || m_pendingKind != PendingKind::None) {
        return;
    }
    QStringList commands;
    int newPreview = m_previewSource;
    int newProgram = m_programSource;
    if (composeTake(Transition::cut(), &commands, &newPreview, &newProgram) != TakePlan::Ready) {
        return;
    }
    beginPending(PendingKind::Transition, commands, newPreview, newProgram);
}

void SwitcherEngine::executeWipe()
{
    if (isBusy()) {
        return;
    }
    const WipePattern pattern = wipePatternById(m_config->wipePatternId());
    if (pattern.type == TransitionType::Smil && resolveWipeMattePath().isEmpty()) {
        emit error(tr("Wipe matte HTML is missing"));
        return;
    }
    const WipeDirectionMode mode = m_config->wipeDirectionMode();
    const bool reverse = mode == WipeDirectionMode::Reverse
        || (mode == WipeDirectionMode::PingPong && !m_wipeSenseForward);
    Transition transition = Transition::fromWipePattern(pattern, m_config->autoDurationFrames(), reverse);
    transition.edge = m_config->wipeEdgeMode();
    transition.edgeAmount = m_config->wipeEdgeAmount();
    transition.borderAmount = m_config->wipeBorderAmount();
    transition.shadowAmount = m_config->wipeShadowAmount();
    transition.multi = m_config->wipeMulti();
    transition.aspectW = m_config->wipeAspectW();
    transition.aspectH = m_config->wipeAspectH();
    transition.posX = m_config->wipePosX();
    transition.posY = m_config->wipePosY();
    transition.vertices=m_config->wipeVertices();transition.rounding=m_config->wipeRounding();transition.tileSize=m_config->wipeTileSize();
    transition.borderColor = m_config->wipeBorderColor();
    m_pendingWipeFlip = mode == WipeDirectionMode::PingPong;
    executeTransition(transition);
}

void SwitcherEngine::setPreviewCursor(bool visible)
{
    m_cursorVisible = visible;
    syncPreviewCursor();
}

void SwitcherEngine::syncPreviewCursor()
{
    if (!m_connected) {
        return;
    }
    const QString path = resolveShareHtml(QStringLiteral("preview-cursor.html"));
    if (path.isEmpty()) {
        return;
    }
    const int x = m_config->wipePosX();
    const int y = m_config->wipePosY();
    if (!m_cursorReady) {
        QUrl url = QUrl::fromLocalFile(path);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("x"), QString::number(x));
        query.addQueryItem(QStringLiteral("y"), QString::number(y));
        query.addQueryItem(QStringLiteral("show"), m_cursorVisible ? QStringLiteral("1") : QStringLiteral("0"));
        url.setQuery(query);
        send(QStringLiteral("PLAY %1-80 [HTML] %2")
                 .arg(activePreviewChannel())
                 .arg(url.toString(QUrl::FullyEncoded)));
        m_cursorReady = true;
        return;
    }
    send(previewCursorCall(QStringLiteral("updateCursor(%1,%2,%3)")
                               .arg(x)
                               .arg(y)
                               .arg(m_cursorVisible ? 1 : 0)));
}

QString SwitcherEngine::previewCursorCall(const QString& script) const
{
    QString escaped = script;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("CALL %1-80 \"%2\"").arg(activePreviewChannel()).arg(escaped);
}

void SwitcherEngine::setWipeDirectionMode(WipeDirectionMode mode)
{
    const bool resetSense = mode != m_config->wipeDirectionMode();
    m_config->setWipeDirectionMode(mode);
    if (resetSense && m_wipeSenseForward != true) {
        m_wipeSenseForward = true;
        emit wipeSenseChanged(true);
    }
}

void SwitcherEngine::setDskOn(bool on)
{
    if (!m_connected) {
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (on == m_dskOn && m_pendingKind == PendingKind::None) {
        return;
    }
    if (isBusy()) {
        emit error(tr("Mixer is busy"));
        return;
    }
    if (on && !keyProducerReady(m_config->dskSource(0))) return;
    ++m_dskFadeToken[0];
    m_pendingDskSlot = 0;
    m_pendingDskOn = on;
    m_pendingDskPreview = m_dskPreview;
    applyDsk(true);
}

void SwitcherEngine::setDskPreview(bool on)
{
    if (!m_connected) {
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (on == m_dskPreview && m_pendingKind == PendingKind::None) {
        return;
    }
    if (isBusy()) {
        emit error(tr("Mixer is busy"));
        return;
    }
    if (on && !keyProducerReady(m_config->dskSource(0))) return;
    m_pendingDskSlot = 0;
    m_pendingDskOn = m_dskOn;
    m_pendingDskPreview = on;
    applyDsk(true);
}

bool SwitcherEngine::isDskOn(int slot) const
{
    if (slot == 0) {
        return m_dskOn;
    }
    if (slot == 1) {
        return m_dsk2On;
    }
    return false;
}

bool SwitcherEngine::isDskMixing(int slot) const
{
    return slot >= 0 && slot < 2 && m_dskMixing[slot];
}

void SwitcherEngine::beginDskMix(int slot, int frames)
{
    if (slot < 0 || slot > 1 || frames <= 0) {
        return;
    }
    const int token = ++m_dskMixToken[slot];
    if (!m_dskMixing[slot]) {
        m_dskMixing[slot] = true;
        emit layersChanged();
    }
    const int ms = qMax(1, frames * 1000 / kVideoFps);
    QTimer::singleShot(ms, this, [this, slot, token]() {
        if (m_dskMixToken[slot] != token || !m_dskMixing[slot]) {
            return;
        }
        m_dskMixing[slot] = false;
        emit layersChanged();
    });
}

bool SwitcherEngine::keyOn(int slot) const
{
    if (slot < 0 || slot >= kKeyerCount) {
        return false;
    }
    return m_keyOn[slot];
}

bool SwitcherEngine::nextKey(int slot) const
{
    if (slot < 0 || slot >= kKeyerCount) {
        return false;
    }
    return m_nextKey[slot];
}

int SwitcherEngine::deskNextCount() const
{
    int count = m_nextBackground ? 1 : 0;
    for (bool selected : m_nextKey) if (selected) ++count;
    return count;
}

void SwitcherEngine::resetNextTransition()
{
    if (isBusy()) return;
    m_nextBackground = true;
    for (int key = 0; key < kKeyerCount; ++key) m_nextKey[key] = false;
    syncPreviewBackground();
    for (int key = 0; key < kKeyerCount; ++key) syncPreviewKey(key);
    emit layersChanged();
}

void SwitcherEngine::toggleNextBackground()
{
    if (isBusy()) return;
    if (m_nextBackground && deskNextCount() <= 1) {
        return;
    }
    m_nextBackground = !m_nextBackground;
    emit layersChanged();
    syncPreviewBackground();
}

void SwitcherEngine::toggleNextKey(int slot)
{
    if (slot < 0 || slot >= kKeyerCount) {
        return;
    }
    if (isBusy()) return;
    if (m_nextKey[slot] && deskNextCount() <= 1) {
        return;
    }
    m_nextKey[slot] = !m_nextKey[slot];
    emit layersChanged();
    syncPreviewKey(slot);
}

void SwitcherEngine::setKeyOn(int slot, bool on)
{
    if (slot < 0 || slot >= kKeyerCount || isBusy() || on == m_keyOn[slot]) return;
    if (!m_connected) { emit error(tr("Not connected to CasparCG")); return; }
    if (on && !keyProducerReady(m_keySource[slot])) return;
    const int layer = kKeyLayer0 + slot;
    QStringList commands;
    bool live = m_keyPreviewLive[slot];
    bool routed = false;
    if (on) {
        if (!live) {
            commands += overlayLayer(activePreviewChannel(), layer, m_keySource[slot], true, 0);
            live = !commands.isEmpty();
        }
        const QStringList program = routedKeyLayerCommands(layer, m_keySource[slot], QString());
        commands += program;
        routed = !program.isEmpty();
    } else {
        commands.append(QStringLiteral("CLEAR %1-%2").arg(activeProgramChannel()).arg(layer));
    }
    if (commands.isEmpty()) return;
    m_pendingKeySlot = slot;
    m_pendingDirectKeyOn = on;
    m_pendingDirectKeyRouted = routed;
    m_pendingDirectKeyPreviewLive = live;
    beginPending(PendingKind::Key, commands, m_previewSource, m_programSource);
}

void SwitcherEngine::setKeySource(int slot, int sourceId)
{
    if (slot < 0 || slot >= kKeyerCount || isBusy() || !keyProducerReady(sourceId) || m_keySource[slot] == sourceId) return;
    if (!m_connected || !m_keyOn[slot]) {
        m_keySource[slot] = sourceId;
        m_keyPreviewLive[slot] = false;
        m_config->setMeKeySource(m_activeMe, slot, sourceId);
        syncPreviewKey(slot);
        emit keyerConfigurationCommitted();
        emit layersChanged();
        return;
    }
    const int layer = kKeyLayer0 + slot;
    QStringList commands = overlayLayer(activeProgramChannel(), layer, sourceId, true, 0);
    commands += overlayLayer(activePreviewChannel(), layer, sourceId, true, 0);
    commands.append(opacityCommand(activePreviewChannel(), layer, m_nextKey[slot] ? 0.0 : 1.0, 0));
    m_pendingKeySlot = slot;
    m_pendingKeySource = sourceId;
    beginPending(PendingKind::KeySource, commands, m_previewSource, m_programSource);
}

void SwitcherEngine::setDskSource(int slot, int sourceId)
{
    if (slot < 0 || slot >= dskCount() || isBusy() || m_dskMixing[slot] || !keyProducerReady(sourceId)
        || m_config->dskSource(slot) == sourceId) return;
    QStringList commands;
    if (m_connected && isDskOn(slot)) {
        commands += overlayLayer(airChannel(), kDskLayer + slot, sourceId, true, 0);
        muteAirIfBlack(&commands, kDskLayer + slot);
    }
    if (m_connected && (isDskOn(slot) || isDskPreview(slot)))
        commands += overlayLayer(m_config->previewChannel(), kDskLayer + slot, sourceId, true, 0);
    if (commands.isEmpty()) {
        m_config->setDskSource(slot, sourceId);
        emit keyerConfigurationCommitted();
        emit layersChanged();
        return;
    }
    m_pendingDskSlot = slot;
    m_pendingDskSource = sourceId;
    beginPending(PendingKind::DskSource, commands, m_previewSource, m_programSource);
}

bool SwitcherEngine::isDskPreview(int slot) const
{
    return slot == 0 ? m_dskPreview : slot == 1 ? m_dsk2Preview : false;
}

void SwitcherEngine::setDskPreview(int slot, bool on)
{
    if (slot == 0) { setDskPreview(on); return; }
    if (slot != 1 || !m_connected || isBusy() || on == m_dsk2Preview) return;
    const int source = m_config->dskSource(slot);
    if (on && !keyProducerReady(source)) return;
    const QStringList commands = overlayLayer(m_config->previewChannel(), kDskLayer + slot, source, on || m_dsk2On, 0);
    m_pendingDskSlot = slot;
    m_pendingDsk2On = m_dsk2On;
    m_pendingDsk2Preview = on;
    m_pendingDskFrames = 0;
    beginPending(PendingKind::Dsk, commands, m_previewSource, m_programSource);
}

void SwitcherEngine::setDskSlot(int slot, bool on, int mixFrames)
{
    if (slot < 0 || slot > 1) {
        return;
    }
    if (slot == 0 && mixFrames <= 0) {
        setDskOn(on);
        return;
    }
    if (!m_connected) {
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (isBusy() || on == isDskOn(slot)) {
        return;
    }
    const int sourceId = m_config->dskSource(slot);
    if (on && !keyProducerReady(sourceId)) {
        return;
    }
    ++m_dskFadeToken[slot];
    const int frames = mixFrames > 0 ? mixFrames : 0;
    QStringList commands = overlayLayer(airChannel(), kDskLayer + slot, sourceId, on, frames);
    if (on) {
        muteAirIfBlack(&commands, kDskLayer + slot);
    }
    commands += overlayLayer(m_config->previewChannel(), kDskLayer + slot, sourceId, on || isDskPreview(slot), isDskPreview(slot) ? 0 : frames);
    if (commands.isEmpty()) {
        return;
    }
    m_pendingDskSlot = slot;
    if (slot == 0) {
        m_pendingDskOn = on;
        m_pendingDskPreview = m_dskPreview;
    } else {
        m_pendingDsk2On = on;
        m_pendingDsk2Preview = m_dsk2Preview;
    }
    m_pendingDskFrames = frames;
    if (frames > 0) {
        m_dskMixing[slot] = true;
        emit layersChanged();
    }
    beginPending(PendingKind::Dsk, commands, m_previewSource, m_programSource);
}

void SwitcherEngine::scheduleDskClear(int slot, int frames)
{
    const int token = m_dskFadeToken[slot];
    const int layer = kDskLayer + slot;
    const int ms = qMax(1, frames * 1000 / kVideoFps);
    QTimer::singleShot(ms, this, [this, slot, token, layer]() {
        if (!m_connected || m_dskFadeToken[slot] != token || isDskOn(slot)) {
            return;
        }
        send(QStringLiteral("CLEAR %1-%2").arg(airChannel()).arg(layer));
        if (!isDskPreview(slot)) send(QStringLiteral("CLEAR %1-%2").arg(m_config->previewChannel()).arg(layer));
    });
}

void SwitcherEngine::syncPreviewKey(int slot)
{
    if (slot < 0 || slot >= kKeyerCount || !m_connected || isBusy()) {
        return;
    }
    const bool show = m_keyOn[slot] != m_nextKey[slot];
    const int layer = kKeyLayer0 + slot;
    const int channel = activePreviewChannel();
    if (show) {
        if (!m_keyPreviewLive[slot]) {
            const QStringList commands = overlayLayer(channel, layer, m_keySource[slot], true, 0);
            for (const QString& command : commands) {
                send(command);
            }
            m_keyPreviewLive[slot] = !commands.isEmpty();
        }
        if (m_keyPreviewLive[slot]) {
            send(opacityCommand(channel, layer, 1.0, 0));
        }
        return;
    }
    if (m_keyOn[slot] || m_keyRouted[slot]) {
        if (!m_keyPreviewLive[slot]) {
            const QStringList commands = overlayLayer(channel, layer, m_keySource[slot], true, 0);
            for (const QString& command : commands) {
                send(command);
            }
            m_keyPreviewLive[slot] = !commands.isEmpty();
        }
        if (m_keyPreviewLive[slot]) {
            send(opacityCommand(channel, layer, 0.0, 0));
            return;
        }
    }
    send(QStringLiteral("CLEAR %1-%2").arg(channel).arg(layer));
    m_keyPreviewLive[slot] = false;
}

void SwitcherEngine::syncPreviewBackground()
{
    if (!m_connected || isBusy()) {
        return;
    }
    const int sourceId = m_nextBackground ? m_previewSource : m_programSource;
    const int channel = sourceChannel(sourceId);
    if (channel < 0) {
        return;
    }
    send(backgroundRoute(activePreviewChannel(), sourceId, Transition::cut()));
}

int SwitcherEngine::keySource(int slot) const
{
    if (slot < 0 || slot >= kKeyerCount) {
        return -1;
    }
    return m_keySource[slot];
}

int SwitcherEngine::meProgramChannel(int me) const
{
    switch (me) {
    case 0:
        return m_config->programChannel();
    case 1:
        return 13;
    case 2:
        return 15;
    case 3:
        return 17;
    default:
        return -1;
    }
}

int SwitcherEngine::mePreviewChannel(int me) const
{
    switch (me) {
    case 0:
        return m_config->previewChannel();
    case 1:
        return 14;
    case 2:
        return 16;
    case 3:
        return 18;
    default:
        return -1;
    }
}

int SwitcherEngine::activePreviewChannel() const
{
    return mePreviewChannel(m_activeMe);
}

int SwitcherEngine::activeProgramChannel() const
{
    return meProgramChannel(m_activeMe);
}

int SwitcherEngine::sourceChannel(int sourceId,int context) const
{
    const int owner=context<0?m_activeMe:context;
    if (sourceId == kReentrySource || sourceId == 23) {
        return owner + 1 < kMeCount && !routeWouldFeedback(owner, owner + 1) ? meProgramChannel(owner + 1) : -1;
    }
    const Source* source = m_config->sourceById(sourceId);
    if (!source || !source->isAssigned()) {
        return -1;
    }
    if(source->type==SourceType::SuperSource && sourceWouldFeedback(sourceId,owner))return -1;
    if (source->type == SourceType::MeProgram) {
        const int me = source->argument.toInt() - 1;
        return routeWouldFeedback(owner, me) ? -1 : meProgramChannel(me);
    }
    return source->type==SourceType::SuperSource?superSourceChannel(sourceId,owner):source->casparChannel;
}

int SwitcherEngine::programSource(int me) const
{
    return me == m_activeMe ? m_programSource : me >= 0 && me < kMeCount ? m_bank[me].program : -1;
}
int SwitcherEngine::previewSource(int me) const
{
    return me == m_activeMe ? m_previewSource : me >= 0 && me < kMeCount ? m_bank[me].preview : -1;
}
int SwitcherEngine::keySource(int me, int slot) const
{
    if (me < 0 || me >= kMeCount || slot < 0 || slot >= kKeyerCount) return -1;
    return me == m_activeMe ? m_keySource[slot] : m_bank[me].keySource[slot];
}
bool SwitcherEngine::keyOn(int me, int slot) const
{
    if (me < 0 || me >= kMeCount || slot < 0 || slot >= kKeyerCount) return false;
    return me == m_activeMe ? m_keyOn[slot] : m_bank[me].keyOn[slot];
}
bool SwitcherEngine::nextKey(int me, int slot) const
{
    if (me < 0 || me >= kMeCount || slot < 0 || slot >= kKeyerCount) return false;
    return me == m_activeMe ? m_nextKey[slot] : m_bank[me].nextKey[slot];
}
bool SwitcherEngine::nextBackground(int me) const
{
    if (me < 0 || me >= kMeCount) return false;
    return me == m_activeMe ? m_nextBackground : m_bank[me].nextBackground;
}

void SwitcherEngine::captureBank()
{
    MeBank& bank = m_bank[m_activeMe];
    bank.preview = m_previewSource;
    bank.program = m_programSource;
    bank.nextBackground = m_nextBackground;
    bank.transitionPreview = m_transitionPreview;
    for (int i = 0; i < kKeyerCount; ++i) {
        bank.keyOn[i] = m_keyOn[i];
        bank.nextKey[i] = m_nextKey[i];
        bank.keySource[i] = m_keySource[i];
        bank.keyPreviewLive[i] = m_keyPreviewLive[i];
        bank.keyRouted[i] = m_keyRouted[i];
    }
}

void SwitcherEngine::applyBank()
{
    const MeBank& bank = m_bank[m_activeMe];
    m_previewSource = bank.preview;
    m_programSource = bank.program;
    m_nextBackground = bank.nextBackground;
    m_transitionPreview = bank.transitionPreview;
    for (int i = 0; i < kKeyerCount; ++i) {
        m_keyOn[i] = bank.keyOn[i];
        m_nextKey[i] = bank.nextKey[i];
        m_keySource[i] = bank.keySource[i];
        m_keyPreviewLive[i] = bank.keyPreviewLive[i];
        m_keyRouted[i] = bank.keyRouted[i];
    }
}

bool SwitcherEngine::keyProducerReady(int sourceId) const
{
    const Source* source = m_config->sourceById(sourceId);
    if (!source || !source->isAssigned() || !source->enabled) {
        return false;
    }
    if(source->type==SourceType::SuperSource)return sourceReady(sourceId)&&!sourceWouldFeedback(sourceId,m_activeMe);
    return source->type != SourceType::MeProgram && !source->producerCommand(m_config->videoWidth(), m_config->videoHeight()).isEmpty();
}

bool SwitcherEngine::nextTransitionWouldChange() const
{
    if (m_nextBackground && sourceChannel(m_previewSource) >= 0) {
        return true;
    }
    for (int i = 0; i < kKeyerCount; ++i) {
        if (!m_nextKey[i]) {
            continue;
        }
        if (m_keyOn[i] || keyProducerReady(m_keySource[i])) {
            return true;
        }
    }
    return false;
}

SwitcherEngine::TakePlan SwitcherEngine::composeTake(const Transition& transition, QStringList* commands, int* newPreview, int* newProgram)
{
    m_commitKeys = false;
    for (int i = 0; i < kKeyerCount; ++i) {
        m_pendingKeyOn[i] = m_keyOn[i];
    }
    bool any = false;
    if (m_nextBackground) {
        if (m_previewSource < 0) {
            return TakePlan::NeedPreview;
        }
        const int incoming = sourceChannel(m_previewSource);
        if (incoming < 0) {
            return TakePlan::MissingPreview;
        }
        playCuedIfNeeded(m_previewSource, commands);
        if (transition.type == TransitionType::Dip) {
            commands->append(QStringLiteral("MIXER %1-0 CLEAR").arg(activeProgramChannel()));
            commands->append(QStringLiteral("PLAY %1-0 %2").arg(activeProgramChannel()).arg(transition.dipColor));
        }
        commands->append(backgroundRoute(activeProgramChannel(), m_previewSource, transition));
        if (transition.type != TransitionType::Dip)
            commands->append(QStringLiteral("CLEAR %1-0").arg(activeProgramChannel()));
        commands->append(wipeBorderLayerCommand(transition));
        const bool canSwap = m_programSource >= 0 && m_programSource != m_previewSource;
        *newProgram = m_previewSource;
        *newPreview = canSwap ? m_programSource : m_previewSource;
        any = true;
    } else {
        *newProgram = m_programSource;
        *newPreview = m_previewSource;
    }

    const int frames = transition.type == TransitionType::Mix ? transition.durationFrames : 0;
    for (int i = 0; i < kKeyerCount; ++i) {
        // A key that is not in the next transition stays on its layer, above the effect.
        if (!m_nextKey[i]) {
            continue;
        }
        const int layer = kKeyLayer0 + i;
        const int sourceId = m_keySource[i];
        const bool bringOn = !m_keyOn[i];
        QStringList change;
        bool routed = false;
        if (bringOn && transition.type == TransitionType::Mix && transition.durationFrames > 0) {
            change = routedKeyLayerCommands(
                layer, sourceId, QStringLiteral(" MIX %1 LINEAR").arg(transition.durationFrames));
            routed = !change.isEmpty();
        }
        if (change.isEmpty()) {
            change = shapedKeyCommands(layer, sourceId, bringOn, transition);
            routed = bringOn && !change.isEmpty();
        }
        if (change.isEmpty()) {
            change = overlayLayer(activeProgramChannel(), layer, sourceId, bringOn, frames);
            routed = false;
        }
        if (bringOn && change.isEmpty()) {
            continue;
        }
        *commands += change;
        m_pendingKeyOn[i] = bringOn;
        m_pendingKeyRouted[i] = bringOn && routed;
        // Preview keeps the pre-take picture until finishTake swaps the buses.
        m_commitKeys = true;
        any = true;
    }
    return any ? TakePlan::Ready : TakePlan::None;
}

QString SwitcherEngine::opacityCommand(int channel, int layer, double value, int frames) const
{
    if (frames > 0) {
        return QStringLiteral("MIXER %1-%2 OPACITY %3 %4 LINEAR")
            .arg(channel)
            .arg(layer)
            .arg(value, 0, 'f', 3)
            .arg(frames);
    }
    return QStringLiteral("MIXER %1-%2 OPACITY %3").arg(channel).arg(layer).arg(value, 0, 'f', 3);
}

QString SwitcherEngine::layerVolumeCommand(int channel, int layer, double value, int frames) const
{
    if (frames > 0) {
        return QStringLiteral("MIXER %1-%2 VOLUME %3 %4 LINEAR")
            .arg(channel)
            .arg(layer)
            .arg(value, 0, 'f', 3)
            .arg(frames);
    }
    return QStringLiteral("MIXER %1-%2 VOLUME %3").arg(channel).arg(layer).arg(value, 0, 'f', 3);
}

void SwitcherEngine::muteAirIfBlack(QStringList* commands, int layer) const
{
    if (!m_ftb || !commands) {
        return;
    }
    commands->append(layerVolumeCommand(airChannel(), layer, 0.0, 0));
}

QStringList SwitcherEngine::overlayLayer(int channel, int layer, int sourceId, bool on, int frames, int context) const
{
    const int owner = context < 0 ? m_activeMe : context;
    if (!on) {
        if (frames > 0) {
            return {opacityCommand(channel, layer, 0.0, frames)};
        }
        return {QStringLiteral("CLEAR %1-%2").arg(channel).arg(layer)};
    }
    const Source* source = m_config->sourceById(sourceId);
    if (!source || !source->isAssigned() || !source->enabled) {
        return {};
    }
    const QString producer = source->producerCommand(m_config->videoWidth(), m_config->videoHeight());
    if (producer.isEmpty() && source->type!=SourceType::SuperSource) {
        return {};
    }
    QStringList commands;
    // MIX waits until the producer has a frame, then fades. An opacity tween
    // started at PLAY finishes while a slow page is still loading, so the
    // graphic appears already full. OPACITY 1 clears a fade-out left at 0.
    // Route the prepared input layer, not the full mixed channel: layer routes
    // retain alpha and avoid restarting files or creating another HTML producer.
    QString play = QStringLiteral("PLAY %1-%2 route://%3%4").arg(channel).arg(layer).arg(source->type==SourceType::SuperSource?superSourceChannel(sourceId,owner):source->casparChannel).arg(source->type==SourceType::SuperSource?QString(" RENDERED"):QString("-1"));
    if (frames > 0) {
        play += QStringLiteral(" MIX %1 LINEAR").arg(frames);
        commands.append(play);
        commands.append(mixerFillCommand(channel, layer, *source));
        if(layer>=kKeyLayer0&&layer<kKeyLayer0+kKeyerCount) commands+=m_config->keyProcessing(owner,layer-kKeyLayer0).commands(channel,layer,m_nativeKeyAvailable);
        if(layer>=kDskLayer&&layer<kDskLayer+2) commands+=m_config->keyProcessing(0,layer-kDskLayer,true).commands(channel,layer,m_nativeKeyAvailable);
        commands.append(opacityCommand(channel, layer, 1.0, 0));
        return commands;
    }
    commands.append(play);
    commands.append(mixerFillCommand(channel, layer, *source));
    if(layer>=kKeyLayer0&&layer<kKeyLayer0+kKeyerCount) commands+=m_config->keyProcessing(owner,layer-kKeyLayer0).commands(channel,layer,m_nativeKeyAvailable);
    if(layer>=kDskLayer&&layer<kDskLayer+2) commands+=m_config->keyProcessing(0,layer-kDskLayer,true).commands(channel,layer,m_nativeKeyAvailable);
    commands.append(opacityCommand(channel, layer, 1.0, 0));
    return commands;
}

QString SwitcherEngine::transparentPlay(int channel, int layer) const
{
    return QStringLiteral("PLAY %1-%2 #00000000").arg(channel).arg(layer);
}

QStringList SwitcherEngine::routedKeyLayerCommands(int layer, int sourceId, const QString& suffix) const
{
    const Source* source = m_config->sourceById(sourceId);
    if (!source || !source->isAssigned() || !source->enabled) {
        return {};
    }
    const int program = activeProgramChannel();
    const int preview = activePreviewChannel();
    const QString play = QStringLiteral("PLAY %1-%2 route://%3-%4%5")
                             .arg(program)
                             .arg(layer)
                             .arg(preview)
                             .arg(layer)
                             .arg(suffix);
    QStringList commands{play, mixerFillCommand(program, layer, *source), opacityCommand(program, layer, 1.0, 0)};
    commands+=::KeyProcessing{}.commands(program,layer,m_nativeKeyAvailable); // The preview route already contains processing.
    return commands;
}

QStringList SwitcherEngine::shapedKeyCommands(int layer, int sourceId, bool on, const Transition& transition) const
{
    if (transition.type != TransitionType::Smil || transition.durationFrames <= 0) {
        return {};
    }
    const QString sting = smilWipeStingSuffix(transition);
    const QString blank = transparentPlay(activeProgramChannel(), layer);
    if (sting.isEmpty() || blank.isEmpty()) {
        return {};
    }
    if (!on) {
        return {blank + sting};
    }
    // Reveal the preview key that is already on screen. A new HTML play
    // stays blank until the page paints, so the wipe cuts the key in.
    QStringList commands = routedKeyLayerCommands(layer, sourceId, sting);
    if (commands.isEmpty()) {
        return {};
    }
    commands.prepend(blank);
    return commands;
}

void SwitcherEngine::applyPreparedConfiguration()
{
    if (isBusy()) return;
    captureBank();
    armMixer();
    for (int me = 0; me < kMeCount; ++me) {
        const int program = me == 0 ? m_config->programChannel() : 13 + (me - 1) * 2;
        for (int key = 0; key < kKeyerCount; ++key) {
            MeBank& bank = m_bank[me];
            const int source = m_config->meKeySource(me, key);
            if (bank.keySource[key] == source) continue;
            bank.keySource[key] = source;
            bank.keyPreviewLive[key] = false;
            bank.keyRouted[key] = false;
            if (bank.keyOn[key] && m_connected) {
                const QStringList commands = overlayLayer(program, kKeyLayer0 + key, source, keyProducerReady(source), 0);
                for (const QString& command : commands) send(command);
                if (!keyProducerReady(source)) bank.keyOn[key] = false;
            }
        }
    }
    applyBank();
    if (m_connected) {
        for (int key = 0; key < kKeyerCount; ++key) syncPreviewKey(key);
    }
    emit layersChanged();
}

void SwitcherEngine::armMixer()
{
    m_restoreFailed = false;
    m_failedCompositions.clear();
    if (!m_connected) {
        return;
    }
    requestNdiList();
    setupSources();
    setupMultiview();
    setupOutputs();
    m_pendingDskOn = m_dskOn;
    m_pendingDskPreview = m_dskPreview;
    applyDsk(false);
    const int dsk2Source = m_config->dskSource(1);
    QStringList second = overlayLayer(airChannel(), kDskLayer + 1, dsk2Source, m_dsk2On, 0);
    if (m_dsk2On) muteAirIfBlack(&second, kDskLayer + 1);
    second += overlayLayer(m_config->previewChannel(), kDskLayer + 1, dsk2Source, m_dsk2On || m_dsk2Preview, 0);
    m_amcp->sendBatch(second);
    restoreCompositions();
}

void SwitcherEngine::restoreCompositions()
{
    if (!m_connected || !m_restoreNeeded || m_restoreFailed || !m_restoreVersionKnown
        || m_restoreBatch || !m_armBatches.isEmpty() || isBusy()) return;
    captureBank();
    // Wait for confirmed shared producers and per-M/E SuperSource instances.
    // Replaying routes before their inputs are ready can leave a black bus.
    auto ready = [this](int id, int me) {
        if (id < 0) return true;
        if (sourceChannel(id, me) < 0) return false;
        const auto* source = m_config->sourceById(id);
        return id == kReentrySource || id == 23
            || (source && source->type == SourceType::MeProgram) || sourceReady(id, me);
    };
    for (int me = 0; me < kMeCount; ++me) {
        const auto& bank = m_bank[me];
        if (!ready(bank.program, me) || !ready(bank.preview, me)) return;
        for (int key = 0; key < kKeyerCount; ++key)
            if ((bank.keyOn[key] || bank.nextKey[key]) && !ready(bank.keySource[key], me)) return;
    }
    QStringList commands;
    // Restore upstream M/Es first, retaining logical bus choices without a take.
    for (int me = kMeCount - 1; me >= 0; --me) {
        const auto& bank = m_bank[me];
        for (const auto& bus : {qMakePair(meProgramChannel(me), bank.program),
                               qMakePair(mePreviewChannel(me), bank.nextBackground ? bank.preview : bank.program)}) {
            commands.append(QStringLiteral("MIXER %1-1 CLEAR").arg(bus.first));
            commands.append(bus.second < 0 ? QStringLiteral("CLEAR %1-1").arg(bus.first)
                                          : backgroundRoute(bus.first, bus.second, Transition::cut()));
        }
        for (int key = 0; key < kKeyerCount; ++key) {
            commands += overlayLayer(meProgramChannel(me), kKeyLayer0 + key,
                bank.keySource[key], bank.keyOn[key], 0, me);
            commands += overlayLayer(mePreviewChannel(me), kKeyLayer0 + key,
                bank.keySource[key], bank.keyOn[key] != bank.nextKey[key], 0, me);
        }
    }
    m_restoreBatch = m_amcp->sendBatch(commands);
    if (m_restoreBatch) m_restoreNeeded = false;
    else {
        m_restoreFailed = true;
        emit error(tr("Could not submit renderer recovery; retry ARM."));
    }
    emit renderReadinessChanged();
}

void SwitcherEngine::onAmcpConnected()
{
    m_restoreNeeded = true;
    m_restoreFailed = false;
    m_restoreVersionKnown = false;
    m_restoreBatch = 0;
    m_transportGraphicsAvailable=m_nativeDmeAvailable=m_pageDmeAvailable=m_expandedSonyAvailable=m_staticDmeAvailable=m_enhancedSonyAvailable=m_rotarySonyAvailable=m_mosaicSonyAvailable=m_compoundSonyAvailable=m_karaokeSonyAvailable=m_randomSonyAvailable=m_completeSonyWipesAvailable=m_primitiveSonyAvailable=m_spatialSonyAvailable=m_planarSonyAvailable=m_mirrorSonyAvailable=m_frameSonyAvailable=m_edgePageSonyAvailable=m_nativeKeyAvailable=m_broadcastMixAvailable=m_dustMixAvailable=false;
    setConnected(true);
    send(QStringLiteral("VERSION"));
    startOscListener();
    // Clean stale rehearsal layers once per connection, across all M/Es.
    // Regular ARM retries must not insert unrelated cleanup into preparation.
    QStringList cleanup;
    for (int me = 0; me < kMeCount; ++me) {
        for (int layer : {100, 101, 111, 112, 113, 114, 115, 120, 121})
            cleanup.append(QStringLiteral("CLEAR %1-%2").arg(mePreviewChannel(me)).arg(layer));
    }
    for (int me = 0; me < kMeCount; ++me) {
        for (int layer : {0, 2, 3, 4, 5, 6, 7, 15, 90}) cleanup.append(QStringLiteral("CLEAR %1-%2").arg(meProgramChannel(me)).arg(layer));
        for (int layer : {102, 103, 104, 105, 106, 107, 190}) cleanup.append(QStringLiteral("CLEAR %1-%2").arg(mePreviewChannel(me)).arg(layer));
    }
    m_amcp->sendBatch(cleanup);
    armMixer();
    m_clockTimer->start();
    syncMeterTimer();
}

void SwitcherEngine::onAmcpDisconnected()
{
    captureBank();
    m_restoreBatch = 0;
    m_restoreVersionKnown = false;
    m_transportGraphicsAvailable=m_nativeDmeAvailable=m_pageDmeAvailable=m_expandedSonyAvailable=m_staticDmeAvailable=m_enhancedSonyAvailable=m_rotarySonyAvailable=m_mosaicSonyAvailable=m_compoundSonyAvailable=m_karaokeSonyAvailable=m_randomSonyAvailable=m_completeSonyWipesAvailable=m_primitiveSonyAvailable=m_spatialSonyAvailable=m_planarSonyAvailable=m_mirrorSonyAvailable=m_frameSonyAvailable=m_edgePageSonyAvailable=m_nativeKeyAvailable=m_broadcastMixAvailable=m_dustMixAvailable=false;
    m_nativeMvReady=false;
    m_manualBanks = {};
    m_requestedMe = -1;
    clearPending();
    m_armedSources.clear();
    m_armBatches.clear();
    m_armedOutputs = {};
    m_ndiNames.clear();
    m_ndiListReady = false;
    m_ndiListRetryScheduled = false;
    m_clipClocks.fill(OscFileTime{});
    m_clockTimer->stop();
    resetDeckMotion();
    m_stingerActive = false;
    m_ftb = false;
    m_ftbFade = false;
    m_ftbReveal = false;
    m_dskMixing[0] = m_dskMixing[1] = false;
    for (int slot = 0; slot < 2; ++slot) {
        ++m_dskFadeToken[slot];
        ++m_dskMixToken[slot];
    }
    m_cursorReady = false;
    m_cursorVisible = false;
    if (m_stingerCutTimer) {
        m_stingerCutTimer->stop();
    }
    setConnected(false);
    abortFailedTake();
    for (int i = 0; i < kKeyerCount; ++i) {
        m_keyPreviewLive[i] = false;
        m_keyRouted[i] = false;
        m_pendingKeyRouted[i] = false;
    }
    for (MeBank& bank : m_bank) {
        for (int i = 0; i < kKeyerCount; ++i) {
            bank.keyPreviewLive[i] = false;
            bank.keyRouted[i] = false;
        }
    }
    syncMeterTimer();
}

void SwitcherEngine::onAmcpConnectionFailed(const QString& message)
{
    emit error(tr("CasparCG connection failed: %1").arg(message));
}

void SwitcherEngine::onAmcpResponse(int code, const QString& status, const QStringList& lines, const QString& command)
{
    if(command=="VERSION") {
        auto match=QRegularExpression("casparMIX\\s+(\\d+\\.\\d+\\.\\d+)").match(lines.join(' '));
        m_transportGraphicsAvailable=code<400&&match.hasMatch()&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,15,0);
        m_nativeDmeAvailable=code<400&&match.hasMatch()&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,5,0);
        m_pageDmeAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,6,0);
        m_expandedSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,7,0);
        m_staticDmeAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,7,1);
        m_enhancedSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,9,0);
        m_rotarySonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,9,2);
        m_mosaicSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,9,3);
        m_compoundSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,9,4);
        m_primitiveSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,10,0);
        m_spatialSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,12,0);
        m_frameSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,17,0);
        m_edgePageSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,20,0);
        m_completeSonyWipesAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,23,0);
        m_randomSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,22,0);
        m_karaokeSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,21,0);
        m_mirrorSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,16,0);
        m_planarSonyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,14,0);
        m_nativeKeyAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,13,0);
        m_dustMixAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,19,0);
        m_broadcastMixAvailable=m_nativeDmeAvailable&&QVersionNumber::fromString(match.captured(1))>=QVersionNumber(0,11,0);
        m_restoreVersionKnown = true;
        restoreCompositions();
        emit layersChanged();return;
    }
    if (command.compare(QLatin1String("NDI LIST"), Qt::CaseInsensitive) == 0) {
        ingestNdiList(code, lines);
        return;
    }
    if (isDeckSeekCommand(command)) {
        onDeckSeekFinished();
        return;
    }

    if (code >= 400) {
        qWarning() << "AMCP error" << code << status << "for" << command;
    }
    // Mixer completion is correlated by transport batch ID, not by command
    // text: a background operation can send an identical command.
}

void SwitcherEngine::onAmcpCommandFailed(const QString& command, const QString& message)
{
    if (isDeckSeekCommand(command)) {
        onDeckSeekFinished();
        return;
    }
    qWarning() << "AMCP command failed" << command << message;
}

void SwitcherEngine::abortFailedTake()
{
    m_manualTimer->stop();
    m_manual.active = m_manual.ready = false;
    m_manualBanks[m_activeMe] = m_manual;
    m_requestedMe = -1;
    m_transitionTimer->stop();
    m_stingerCutTimer->stop();
    m_deferBus = false;
    m_deferKeys = false;
    m_pendingWipeFlip = false;
    m_takeActive = false;
    m_stingerActive = false;
    m_ftbFade = false;
    if (m_previewTakeRunning && m_connected) m_amcp->sendBatch(previewCleanupCommands());
    m_previewTakeRunning = false;
    setTransitioning(false);
    emit layersChanged();
    // Some earlier commands may already have changed the render output. Do not
    // guess a rollback or clear layers that may still be on air.
}

void SwitcherEngine::setConnected(bool connected)
{
    if (m_connected == connected) {
        return;
    }
    m_connected = connected;
    emit connectionStatusChanged(m_connected);
}

void SwitcherEngine::setPreviewSource(int sourceId)
{
    if (m_previewSource == sourceId) {
        return;
    }
    m_previewSource = sourceId;
    emit previewSourceChanged(m_previewSource);
}

void SwitcherEngine::setProgramSource(int sourceId)
{
    if (m_programSource == sourceId) {
        return;
    }
    m_programSource = sourceId;
    emit programSourceChanged(m_programSource);
}

void SwitcherEngine::setTransitioning(bool transitioning)
{
    if (m_transitioning == transitioning) {
        return;
    }
    m_transitioning = transitioning;
    emit transitioningChanged(m_transitioning);
}

QString SwitcherEngine::activeTakeName() const
{
    return m_takeActive ? takeName(m_activeTake) : QString();
}

QString SwitcherEngine::takeName(TransitionType type) const
{
    switch (type) {
    case TransitionType::Mix:
    case TransitionType::Nam:
    case TransitionType::SuperMix:
    case TransitionType::Dip:
    case TransitionType::VFade:
    case TransitionType::FadeCut:
    case TransitionType::CutFade:
        return QStringLiteral("mix");
    case TransitionType::Wipe:
    case TransitionType::Push:
    case TransitionType::Slide:
    case TransitionType::Smil:
    case TransitionType::Move:
    case TransitionType::Cube:
    case TransitionType::Zoom:
    case TransitionType::PageCurl:
    case TransitionType::PageRoll:
    case TransitionType::SonyDme:
        return QStringLiteral("wipe");
    case TransitionType::Cut:
    default:
        return QStringLiteral("cut");
    }
}

int SwitcherEngine::transitionHoldMs(const Transition& transition) const
{
    if (transition.durationFrames <= 0) {
        return 0;
    }
    return (transition.durationFrames * 1000 + (kVideoFps - 1)) / kVideoFps;
}

void SwitcherEngine::beginTake(const Transition& transition)
{
    m_takeDmeBackground=transition.dmeBackground;
    m_activeTake = transition.type;
    m_takeActive = true;
    setTransitioning(true);
    // A queued command has not started a render operation yet. The hold begins
    // after the take batch is acknowledged, never while it is waiting in AMCP.
    m_transitionTimer->stop();
    m_transitionHoldMs = transitionHoldMs(transition);
}

void SwitcherEngine::finishTake()
{
    if (m_previewTakeRunning) {
        m_transitionTimer->stop();
        beginPending(PendingKind::PreviewCleanup, previewCleanupCommands(), m_previewSource, m_programSource);
        return;
    }
    if (m_activeTake == TransitionType::Dip && m_connected)
        send(QStringLiteral("CLEAR %1-0").arg(activeProgramChannel()));
    m_ftbFade = false;
    m_transitionTimer->stop();
    if (m_stingerCutTimer) {
        m_stingerCutTimer->stop();
    }
    if (m_stingerActive && m_connected) {
        send(QStringLiteral("CLEAR %1-%2").arg(m_stingerChannel).arg(kStingerLayer));
    }
    m_stingerActive = false;
    applyDeferredBus();
    if (m_connected) {
        for (int i = 0; i < kKeyerCount; ++i) {
            if (!m_keyOn[i]) {
                send(QStringLiteral("CLEAR %1-%2").arg(activeProgramChannel()).arg(kKeyLayer0 + i));
            }
        }
    }
    if (m_pendingWipeFlip && m_takeActive && takeName(m_activeTake) == QLatin1String("wipe")) {
        m_wipeSenseForward = !m_wipeSenseForward;
        emit wipeSenseChanged(m_wipeSenseForward);
    }
    m_pendingWipeFlip = false;
    m_takeActive = false;
    setTransitioning(false);
}

void SwitcherEngine::onTransitionHoldFinished()
{
    if (m_ftbFade) {
        finishFtb();
        return;
    }
    finishTake();
}

void SwitcherEngine::send(const QString& command)
{
    m_amcp->sendCommand(command);
}

void SwitcherEngine::beginPending(PendingKind kind, const QStringList& commands, int previewId, int programId)
{
    m_pendingKind = kind;
    m_pendingCommands = commands;
    m_pendingPreview = previewId;
    m_pendingProgram = programId;
    m_pendingBatch = m_amcp->sendBatch(commands);
    if (m_pendingBatch == 0) {
        emit error(tr("Could not submit mixer command batch"));
        clearPending();
        abortFailedTake();
    }
}

void SwitcherEngine::finishPending()
{
    const PendingKind kind = m_pendingKind;
    const int preview = m_pendingPreview;
    const int program = m_pendingProgram;
    const bool dskOn = m_pendingDskOn;
    const bool dskPreview = m_pendingDskPreview;
    const int dskSlot = m_pendingDskSlot;
    const int dskFrames = m_pendingDskFrames;
    const bool dsk2On = m_pendingDsk2On;
    const bool dsk2Preview = m_pendingDsk2Preview;
    const int keySlot = m_pendingKeySlot;
    const int keySource = m_pendingKeySource;
    const int dskSource = m_pendingDskSource;
    const bool directKeyOn = m_pendingDirectKeyOn;
    const bool directKeyRouted = m_pendingDirectKeyRouted;
    const bool directKeyPreviewLive = m_pendingDirectKeyPreviewLive;
    const bool commitKeys = m_commitKeys;
    bool pendingKeyOn[kKeyerCount];
    for (int i = 0; i < kKeyerCount; ++i) {
        pendingKeyOn[i] = m_pendingKeyOn[i];
    }
    clearPending();

    if (kind == PendingKind::ManualStart) {
        m_manual.ready = true;
        const int requested = m_requestedMe;
        m_requestedMe = -1;
        flushManualPosition();
        m_requestedMe = requested;
    } else if (kind == PendingKind::ManualPosition) {
        if (m_manual.endpoint && m_manual.sent == m_manual.position) finishManualTransition();
    } else if (kind == PendingKind::ManualFinish) {
        const bool commit = m_manual.position == 4095 && !m_manual.preview;
        m_manual.active = m_manual.ready = false;
        m_manualBanks[m_activeMe] = m_manual;
        m_manualTimer->stop();
        m_takeActive = false;
        if (commit) {
            if (m_manual.background) {
                const int outgoing = m_programSource;
                setProgramSource(m_previewSource);
                if (outgoing >= 0) setPreviewSource(outgoing);
            }
            for (int i = 0; i < kKeyerCount; ++i) if (m_manual.keys[i]) {
                m_keyOn[i] = !m_manual.keyBefore[i];
                m_keyRouted[i] = false;
            }
        }
        setTransitioning(false);
        syncPreviewBackground();
        for (int i = 0; i < kKeyerCount; ++i) syncPreviewKey(i);
        emit layersChanged();
    } else if (kind == PendingKind::PreviewTransition) {
        if (m_transitionHoldMs > 0) m_transitionTimer->start(m_transitionHoldMs);
        else finishTake();
    } else if (kind == PendingKind::PreviewCleanup) {
        m_previewTakeRunning = false;
        m_takeActive = false;
        setTransitioning(false);
        emit layersChanged();
    } else if (kind == PendingKind::Key) {
        m_keyOn[keySlot] = directKeyOn;
        m_keyRouted[keySlot] = directKeyRouted;
        m_keyPreviewLive[keySlot] = directKeyPreviewLive;
        syncPreviewKey(keySlot);
        emit layersChanged();
    } else if (kind == PendingKind::KeyProcessing) {
        m_config->setKeyProcessing(m_pendingProcessingMe,m_pendingProcessingSlot,m_pendingProcessingDsk,m_pendingProcessing);
        emit keyerConfigurationCommitted();
        emit layersChanged();
    } else if (kind == PendingKind::KeySource) {
        m_keySource[keySlot] = keySource;
        m_keyPreviewLive[keySlot] = true;
        m_keyRouted[keySlot] = false;
        m_config->setMeKeySource(m_activeMe, keySlot, keySource);
        emit keyerConfigurationCommitted();
        emit layersChanged();
    } else if (kind == PendingKind::DskSource) {
        m_config->setDskSource(dskSlot, dskSource);
        emit keyerConfigurationCommitted();
        emit layersChanged();
    } else if (kind == PendingKind::Preview) {
        setPreviewSource(preview);
    } else if (kind == PendingKind::Transition) {
        m_deferBus = true;
        m_deferPreview = preview;
        m_deferProgram = program;
        m_deferKeys = commitKeys;
        for (int i = 0; i < kKeyerCount; ++i) {
            m_deferKeyOn[i] = pendingKeyOn[i];
        }
        m_commitKeys = false;
        if (m_stingerActive) {
            if (!m_transitionTimer->isActive()) {
                finishTake();
            }
        } else if (m_transitionHoldMs > 0) {
            m_transitionTimer->start(m_transitionHoldMs);
        } else {
            finishTake();
        }
    } else if (kind == PendingKind::Ftb) {
        m_transitionTimer->start(m_transitionHoldMs);
    } else if (kind == PendingKind::HotPunch) {
        setProgramSource(program);
        if (!m_nextBackground) {
            syncPreviewBackground();
        }
    } else if (kind == PendingKind::Dsk && dskSlot != 0) {
        m_dsk2On = dsk2On;
        m_dsk2Preview = dsk2Preview;
        emit layersChanged();
    } else if (kind == PendingKind::Dsk) {
        if (m_dskOn != dskOn) {
            m_dskOn = dskOn;
            emit dskOnChanged(m_dskOn);
        }
        if (m_dskPreview != dskPreview) {
            m_dskPreview = dskPreview;
            emit dskPreviewChanged(m_dskPreview);
        }
    }
    if (m_requestedMe >= 0 && m_pendingKind == PendingKind::None) setActiveMe(m_requestedMe);
    if (kind == PendingKind::Dsk && dskFrames > 0) {
        beginDskMix(dskSlot, dskFrames);
        const bool on = dskSlot == 0 ? dskOn : dsk2On;
        if (!on) {
            scheduleDskClear(dskSlot, dskFrames);
        }
    }
}

void SwitcherEngine::applyDeferredBus()
{
    if (!m_deferBus) {
        return;
    }
    m_deferBus = false;
    const int preview = m_deferPreview;
    const int program = m_deferProgram;
    const bool keys = m_deferKeys;
    m_deferKeys = false;
    if (m_connected && preview >= 0 && preview != m_previewSource) {
        const QString route = backgroundRoute(activePreviewChannel(), preview, Transition::cut());
        if (!route.isEmpty()) {
            send(route);
        }
    }
    if (m_connected && keys) {
        const int previewChannel = activePreviewChannel();
        for (int i = 0; i < kKeyerCount; ++i) {
            if (m_keyOn[i] == m_deferKeyOn[i]) {
                continue;
            }
            const int layer = kKeyLayer0 + i;
            const bool entering = m_deferKeyOn[i];
            if (entering && m_pendingKeyRouted[i]) {
                // The program layer routes this preview producer. Hide it
                // instead of clearing it, or the key vanishes and reloads.
                send(opacityCommand(previewChannel, layer, 0.0, 0));
                m_keyRouted[i] = true;
            } else if (!entering && m_keyPreviewLive[i]) {
                send(opacityCommand(previewChannel, layer, 1.0, 0));
                m_keyRouted[i] = false;
            } else {
                const bool show = m_keyOn[i];
                const QStringList previewKeys = overlayLayer(previewChannel, layer, m_keySource[i], show, 0);
                for (const QString& command : previewKeys) {
                    send(command);
                }
                if (show && !previewKeys.isEmpty()) {
                    send(opacityCommand(previewChannel, layer, 1.0, 0));
                    m_keyPreviewLive[i] = true;
                } else if (!show) {
                    m_keyPreviewLive[i] = false;
                }
                m_keyRouted[i] = false;
            }
        }
    }
    setPreviewSource(preview);
    setProgramSource(program);
    if (!keys) {
        return;
    }
    bool changed = false;
    for (int i = 0; i < kKeyerCount; ++i) {
        if (m_keyOn[i] == m_deferKeyOn[i]) {
            continue;
        }
        m_keyOn[i] = m_deferKeyOn[i];
        changed = true;
    }
    if (changed) {
        emit layersChanged();
    }
}

void SwitcherEngine::clearPending()
{
    // Invalidate optimistic DSK activity on a failed batch. finishPending takes
    // a copy of the duration before clearing and starts the confirmed fade.
    if (m_pendingKind == PendingKind::Dsk && m_pendingDskFrames > 0) {
        ++m_dskMixToken[m_pendingDskSlot];
        m_dskMixing[m_pendingDskSlot] = false;
    }
    m_pendingDskFrames = 0;
    m_pendingBatch = 0;
    m_pendingKind = PendingKind::None;
    m_pendingCommands.clear();
    m_pendingPreview = -1;
    m_pendingProgram = -1;
    m_pendingWipeFlip = false;
}

void SwitcherEngine::requestNdiList()
{
    send(QStringLiteral("NDI LIST"));
}

void SwitcherEngine::ingestNdiList(int code, const QStringList& lines)
{
    const QStringList names = (code == 200) ? parseNdiListLines(lines) : QStringList();
    const bool changed = !m_ndiListReady || names != m_ndiNames;
    m_ndiNames = names;
    m_ndiListReady = true;
    if (code == 200 && names.isEmpty() && !m_ndiListRetryScheduled && m_connected) {
        m_ndiListRetryScheduled = true;
        QTimer::singleShot(800, this, [this]() {
            if (m_connected) {
                requestNdiList();
            }
        });
    }
    if (changed) {
        setupSources();
    }
}

bool SwitcherEngine::sourceReady(int sourceId,int context) const
{
    const Source* source = m_config->sourceById(sourceId);
    if (!m_connected || !source || !source->isAssigned()) {
        return false;
    }
    if(source->type==SourceType::SuperSource){
        if(!m_config->superSourceGraphError().isEmpty())return false;
        const int owner=context<0?m_activeMe:context;auto desired=compositionCommands(sourceId,owner);auto ready=m_armedSources.value(superSourceKey(sourceId,owner));
        return !desired.isEmpty()&&ready.channel==superSourceChannel(sourceId,owner)&&ready.playCommand==desired.join('\n');
    }
    if (source->type == SourceType::MeProgram) return sourceSelectable(sourceId);
    const QString producer = source->type == SourceType::Ndi
        ? ndiProducerCommand(source->argument, m_ndiNames)
        : source->producerCommand(m_config->videoWidth(), m_config->videoHeight());
    const ArmedSource confirmed = m_armedSources.value(sourceId);
    return confirmed.channel == source->casparChannel && !producer.isEmpty()
        && confirmed.playCommand == QStringLiteral("PLAY %1-1 %2").arg(source->casparChannel).arg(producer)
        && confirmed.mixerCommand == mixerFillCommand(source->casparChannel, 1, *source);
}

bool SwitcherEngine::outputsReady() const
{
    for(const auto& output:m_config->destinations())if(output.enabled&&outputSourceChannel(output.source)<1)return false;
    return m_connected && plannedOutputs() == m_armedOutputs;
}

void SwitcherEngine::queueArmBatch(const QList<ArmCommand>& commands)
{
    if (commands.isEmpty()) {
        return;
    }
    QStringList text;
    for (const ArmCommand& command : commands) {
        text.append(command.command);
    }
    const quint64 batch = m_amcp->sendBatch(text);
    if (batch != 0) {
        m_armBatches.insert(batch, commands);
        emit renderReadinessChanged();
    } else {
        emit error(tr("Could not submit render preparation batch"));
    }
}

void SwitcherEngine::onArmCommandFinished(quint64 batch, const QString& text, bool success)
{
    if (!success || !m_armBatches.contains(batch)) {
        return;
    }
    const QList<ArmCommand> commands = m_armBatches.value(batch);
    for (const ArmCommand& command : commands) {
        if (command.command != text) {
            continue;
        }
        switch (command.kind) {
        case ArmKind::SourceComposePart: break;
        case ArmKind::SourceCompose: break;
        case ArmKind::SourceStop:
            m_armedSources.remove(command.context<0?command.sourceId:superSourceKey(command.sourceId,command.context));
            break;
        case ArmKind::SourcePlay: {
            ArmedSource& confirmed = m_armedSources[command.sourceId];
            confirmed.channel = command.channel;
            confirmed.playCommand = command.confirmedValue;
            confirmed.mixerCommand.clear();
            break;
        }
        case ArmKind::SourceMixer: {
            ArmedSource& confirmed = m_armedSources[command.sourceId];
            if (confirmed.channel == command.channel) {
                confirmed.mixerCommand = command.confirmedValue;
            }
            break;
        }
        case ArmKind::ManagedOutput:
            if (command.confirmedValue.isEmpty()) m_armedOutputs.managed.remove(command.channel);
            else m_armedOutputs.managed[command.channel] = command.confirmedValue;
            break;
        case ArmKind::OutputRoute:
            m_armedOutputs.outputPlay = command.confirmedValue;
            break;
        case ArmKind::OutputProgram:
            m_armedOutputs.pgmAdd = command.confirmedValue;
            break;
        case ArmKind::OutputClean:
            m_armedOutputs.cleanAdd = command.confirmedValue;
            break;
        }
        emit renderReadinessChanged();
        break;
    }
}

void SwitcherEngine::onArmBatchFinished(quint64 batch, bool success)
{
    if (batch == m_restoreBatch && batch != 0) {
        m_restoreBatch = 0;
        m_restoreFailed = !success;
        m_restoreNeeded = !success;
        if (success) {
            for (int me = 0; me < kMeCount; ++me)
                for (int key = 0; key < kKeyerCount; ++key) {
                    m_bank[me].keyPreviewLive[key] = m_bank[me].keyOn[key] != m_bank[me].nextKey[key];
                    m_bank[me].keyRouted[key] = false;
                }
            applyBank();
        } else {
            emit error(tr("Renderer recovery failed; check the output and retry ARM."));
        }
        emit renderReadinessChanged();
        emit layersChanged();
        return;
    }
    if (m_pendingKind != PendingKind::None && batch == m_pendingBatch) {
        if (success) {
            finishPending();
        } else {
            const bool rehearsal = m_previewTakeRunning;
            clearPending();
            abortFailedTake();
            emit error(rehearsal
                ? tr("Transition preview failed; check the preview output before repeating the rehearsal.")
                : tr("Mixer command batch failed; earlier commands may have changed the output. Check CasparCG before continuing."));
        }
        return;
    }
    if (!m_armBatches.contains(batch)) {
        return;
    }
    const QList<ArmCommand> commands = m_armBatches.take(batch);
    if(success){for(const auto& command:commands)if(command.kind==ArmKind::SourceCompose){auto& ready=m_armedSources[command.context<0?command.sourceId:superSourceKey(command.sourceId,command.context)];ready.channel=command.channel;ready.playCommand=command.confirmedValue;ready.mixerCommand="SUPERSOURCE";}}
    else if(!commands.isEmpty()&&(commands.last().kind==ArmKind::SourceCompose)){const int key=commands.last().context<0?commands.last().sourceId:superSourceKey(commands.last().sourceId,commands.last().context);m_armedSources.remove(key);m_failedCompositions[key]=commands.last().confirmedValue;}
    if (!success) {
        const ArmCommand& first = commands.first();
        if (first.sourceId >= 0) {
            emit error(tr("Source %1 could not be prepared; fix the cause and retry ARM").arg(first.sourceId + 1));
        } else {
            emit error(tr("Output configuration could not be completed; fix the cause and retry ARM"));
        }
    } else if (m_connected) {
        // Configuration can change while an older preparation batch is pending.
        // Apply the latest desired configuration after a successful completion;
        // a failed batch requires an explicit retry and is never looped blindly.
        QTimer::singleShot(0,this,[this](){if(m_connected)setupSuperSources();});
        const int sourceId = commands.first().sourceId;
        if (sourceId < 0 && !(plannedOutputs() == m_armedOutputs)) {
            QTimer::singleShot(0, this, [this]() { if (m_connected) setupOutputs(); });
        } else if (sourceId >= 0) {
            const Source* source = m_config->sourceById(sourceId);
            if (source && ((source->isAssigned() && !sourceReady(sourceId))
                || (!source->isAssigned() && m_armedSources.contains(sourceId)))) {
                QTimer::singleShot(0, this, [this]() { if (m_connected) setupSources(); });
            }
        }
    }
    restoreCompositions();
    emit renderReadinessChanged();
}

void SwitcherEngine::setupSources()
{
    for (const Source& source : m_config->sources()) {
        if (source.id == 11 || source.id == 23 || source.type == SourceType::MeProgram || source.type == SourceType::SuperSource) continue;
        bool pending = false;
        for (const QList<ArmCommand>& commands : m_armBatches) {
            pending |= commands.first().sourceId == source.id;
        }
        if (pending || (source.type == SourceType::Ndi && !m_ndiListReady)) {
            continue;
        }
        const QString producer = source.type == SourceType::Ndi
            ? ndiProducerCommand(source.argument, m_ndiNames)
            : source.producerCommand(m_config->videoWidth(), m_config->videoHeight());
        const ArmedSource previous = m_armedSources.value(source.id);
        QList<ArmCommand> commands;
        if (!source.isAssigned() || producer.isEmpty()) {
            if (previous.channel > 0) {
                commands.append({ArmKind::SourceStop, source.id, previous.channel,
                    previous.mixerCommand=="SUPERSOURCE"?QString("CLEAR %1").arg(previous.channel):QStringLiteral("STOP %1-1").arg(previous.channel), {}});
                queueArmBatch(commands);
            }
            continue;
        }
        const QString play = QStringLiteral("PLAY %1-1 %2").arg(source.casparChannel).arg(producer);
        const QString mixer = mixerFillCommand(source.casparChannel, 1, source);
        if(previous.channel>0&&previous.mixerCommand=="SUPERSOURCE"){
            commands.append({ArmKind::SourceStop,source.id,previous.channel,QString("CLEAR %1").arg(previous.channel),{}});
        }
        if (previous.channel > 0 && previous.channel != source.casparChannel) {
            commands.append({ArmKind::SourceStop, source.id, previous.channel,
                previous.mixerCommand=="SUPERSOURCE"?QString("CLEAR %1").arg(previous.channel):QStringLiteral("STOP %1-1").arg(previous.channel), {}});
        }
        if (previous.channel != source.casparChannel || previous.playCommand != play) {
            commands.append({ArmKind::SourcePlay, source.id, source.casparChannel, play, play});
        }
        if (!commands.isEmpty() || previous.mixerCommand != mixer) {
            commands.append({ArmKind::SourceMixer, source.id, source.casparChannel, mixer, mixer});
        }
        queueArmBatch(commands);
    }
    setupSuperSources();
}

void SwitcherEngine::setupMultiview()
{
    const int mv = m_config->multiviewChannel();
    send(QStringLiteral("CLEAR %1").arg(mv));

    // Main air/preview remain fixed; side columns expose M/E 2–4 simultaneously.
    // Monitor the rendered bus texture, never its draw tree. Recomposition
    // at the multiview raster changes mask geometry, blending and DME shading.
    // The shared synchronization group supplies the same output epoch.
    send(QStringLiteral("PLAY %1-10 route://%2 RENDERED").arg(mv).arg(airChannel()));
    send(QStringLiteral("PLAY %1-9 route://%2 RENDERED").arg(mv).arg(mePreviewChannel(0)));

    for (int me = 1; me < kMeCount; ++me) {
        send(QStringLiteral("PLAY %1-%2 route://%3 RENDERED").arg(mv).arg(60+me).arg(mePreviewChannel(me)));
        send(QStringLiteral("PLAY %1-%2 route://%3 RENDERED").arg(mv).arg(64+me).arg(meProgramChannel(me)));
        send(QStringLiteral("MIXER %1-%2 FILL 0 %3 0.166667 0.2").arg(mv).arg(60+me).arg((me-1)*0.2,0,'f',6));
        send(QStringLiteral("MIXER %1-%2 FILL 0.833333 %3 0.166667 0.2").arg(mv).arg(64+me).arg((me-1)*0.2,0,'f',6));
    }
    syncMultiviewSources();

    send(QStringLiteral("PLAY %1-80 GRAPHICS").arg(mv));
    m_nativeMvReady=true;
    m_lastNativeScene.clear();
    m_lastNativeValues.clear();
    m_lastOverlayHeartbeat = 0;
    flushOverlayLabels();
    applyMultiviewLayout();
    m_cursorReady = false;
    syncPreviewCursor();
}

SwitcherEngine::ArmedOutputs SwitcherEngine::plannedOutputs() const
{
    ArmedOutputs next;
    const int program = m_config->programChannel();
    const int air = airChannel();
    if (programOutputUsable()) {
        next.outputPlay = QStringLiteral("PLAY %1-1 route://%2").arg(air).arg(program);
    }
    if (m_config->ndiProgramEnabled()) {
        next.pgmAdd = ndiConsumerCommand(QStringLiteral("ADD"), air, m_config->ndiProgramName());
    }
    if (m_config->ndiCleanEnabled() && programOutputUsable()) {
        next.cleanAdd = ndiConsumerCommand(QStringLiteral("ADD"), program, m_config->ndiCleanName());
    }
    for (const auto& d : m_config->destinations()) {
        if (!d.enabled) continue;
        const int channel = d.aux ? 34 + d.aux : outputSourceChannel(d.source);
        const int input = outputSourceChannel(d.source);
        if (channel < 1 || input < 1) continue;
        if (d.aux) next.managed[d.id * 2] = QStringLiteral("PLAY %1-1 route://%2%3").arg(channel).arg(input).arg(d.source < 24 && (sourceMe(d.source, m_activeMe) < 0 && (!m_config->sourceById(d.source)||m_config->sourceById(d.source)->type!=SourceType::SuperSource)) ? QStringLiteral("-1") : QString());
        QString name = d.name; name.replace(QLatin1Char('\\'), QStringLiteral("\\\\")); name.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        next.managed[d.id * 2 + 1] = d.type == QLatin1String("ndi")
            ? QStringLiteral("ADD %1-%2 NDI NAME \"%3\"").arg(channel).arg(1000 + d.id).arg(name)
            : QStringLiteral("ADD %1-%2 SCREEN %3 %4 NAME \"%5\"").arg(channel).arg(1000 + d.id).arg(d.device).arg(d.fullscreen ? QStringLiteral("FULLSCREEN") : QStringLiteral("WINDOWED")).arg(name);
    }
    return next;
}

bool SwitcherEngine::programOutputUsable() const
{
    const int output = m_config->programOutputChannel();
    if (output < 1) {
        return false;
    }
    if (output == m_config->programChannel()
        || output == m_config->previewChannel()
        || output == m_config->multiviewChannel()) {
        return false;
    }
    for (int me = 1; me < kMeCount; ++me) {
        if (output == meProgramChannel(me) || output == mePreviewChannel(me)) {
            return false;
        }
    }
    for (const Source& source : m_config->sources()) {
        if (source.casparChannel == output) {
            return false;
        }
    }
    return true;
}

int SwitcherEngine::airChannel() const
{
    return programOutputUsable() ? m_config->programOutputChannel() : m_config->programChannel();
}

QString SwitcherEngine::ndiConsumerCommand(const QString& verb, int channel, const QString& name) const
{
    QString trimmed = name.trimmed();
    if (channel < 1 || trimmed.isEmpty()) {
        return {};
    }
    if (trimmed.contains(QLatin1Char(' ')) || trimmed.contains(QLatin1Char('"'))) {
        trimmed.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        trimmed = QStringLiteral("\"%1\"").arg(trimmed);
    }
    return QStringLiteral("%1 %2 NDI NAME %3").arg(verb).arg(channel).arg(trimmed);
}

QString SwitcherEngine::ndiRemoveCommand(const QString& addCommand) const
{
    if (!addCommand.startsWith(QLatin1String("ADD "))) {
        return {};
    }
    return QStringLiteral("REMOVE ") + addCommand.mid(4);
}

void SwitcherEngine::setupOutputs()
{
    for (const QList<ArmCommand>& commands : m_armBatches) {
        if (commands.first().sourceId < 0) {
            return;
        }
    }
    const ArmedOutputs next = plannedOutputs();
    QList<ArmCommand> commands;
    if (m_armedOutputs.pgmAdd != next.pgmAdd && !m_armedOutputs.pgmAdd.isEmpty()) {
        commands.append({ArmKind::OutputProgram, -1, 0, ndiRemoveCommand(m_armedOutputs.pgmAdd), {}});
    }
    if (m_armedOutputs.cleanAdd != next.cleanAdd && !m_armedOutputs.cleanAdd.isEmpty()) {
        commands.append({ArmKind::OutputClean, -1, 0, ndiRemoveCommand(m_armedOutputs.cleanAdd), {}});
    }
    if (m_armedOutputs.outputPlay != next.outputPlay) {
        if (!next.outputPlay.isEmpty()) {
            commands.append({ArmKind::OutputRoute, -1, 0, next.outputPlay, next.outputPlay});
        } else if (!m_armedOutputs.outputPlay.isEmpty()) {
            const QString channel = m_armedOutputs.outputPlay.section(QLatin1Char(' '), 1, 1)
                .section(QLatin1Char('-'), 0, 0);
            commands.append({ArmKind::OutputRoute, -1, 0, QStringLiteral("STOP %1-1").arg(channel), {}});
        }
    }
    if (m_armedOutputs.pgmAdd != next.pgmAdd && !next.pgmAdd.isEmpty()) {
        commands.append({ArmKind::OutputProgram, -1, 0, next.pgmAdd, next.pgmAdd});
    }
    if (m_armedOutputs.cleanAdd != next.cleanAdd && !next.cleanAdd.isEmpty()) {
        commands.append({ArmKind::OutputClean, -1, 0, next.cleanAdd, next.cleanAdd});
    }
    for (auto it = m_armedOutputs.managed.cbegin(); it != m_armedOutputs.managed.cend(); ++it) {
        if (next.managed.value(it.key()) == it.value()) continue;
        // Route changes keep their consumer. Removed consumers use their explicit port.
        if (it.key() % 2 || !next.managed.contains(it.key())) {
            const QString target = it.value().section(QLatin1Char(' '), 1, 1);
            const QString remove = (it.key() % 2 ? QStringLiteral("REMOVE ") : QStringLiteral("STOP ")) + target;
            commands.append({ArmKind::ManagedOutput, -1, it.key(), remove, {}});
        }
    }
    for (auto it = next.managed.cbegin(); it != next.managed.cend(); ++it) if(m_armedOutputs.managed.value(it.key()) != it.value())
        commands.append({ArmKind::ManagedOutput, -1, it.key(), it.value(), it.value()});
    queueArmBatch(commands);
}

void SwitcherEngine::applyDsk(bool wait)
{
    const Source* dsk = m_config->sourceById(m_config->dskSourceId());
    const QString producer = dsk
        ? dsk->producerCommand(m_config->videoWidth(), m_config->videoHeight())
        : QString();
    if (!dsk || !dsk->enabled || producer.isEmpty()) {
        if (!wait) {
            send(dskStopCommand(airChannel()));
            send(dskStopCommand(m_config->previewChannel()));
            if (airChannel() != m_config->programChannel()) {
                send(dskStopCommand(m_config->programChannel()));
            }
        }
        return;
    }

    QStringList commands;
    if (!wait && airChannel() != m_config->programChannel()) {
        send(dskStopCommand(m_config->programChannel()));
    }
    if (!wait || m_pendingDskOn != m_dskOn) {
        commands += dskLayerCommands(airChannel(), m_pendingDskOn);
        if (m_pendingDskOn) {
            muteAirIfBlack(&commands, kDskLayer);
        }
    }
    const bool previewShow = m_pendingDskPreview || m_pendingDskOn;
    const bool previewWas = m_dskPreview || m_dskOn;
    if (!wait || previewShow != previewWas || m_pendingDskPreview != m_dskPreview) {
        commands += dskLayerCommands(m_config->previewChannel(), previewShow);
    }
    if (commands.isEmpty()) {
        return;
    }
    if (wait) {
        beginPending(PendingKind::Dsk, commands, m_previewSource, m_programSource);
    } else {
        for (const QString& command : commands) {
            send(command);
        }
    }
}

QString SwitcherEngine::routePlayCommand(int destChannel, int sourceChannel, const Transition& transition) const
{
    return QStringLiteral("PLAY %1-1 route://%2-1").arg(destChannel).arg(sourceChannel)
        + (transition.type == TransitionType::Smil ? smilWipeStingSuffix(transition) : transition.amcpSuffix());
}

QString SwitcherEngine::backgroundRoute(int destChannel, int sourceId, const Transition& transition) const
{
    int context=m_activeMe;for(int me=0;me<4;++me)if(destChannel==meProgramChannel(me)||destChannel==mePreviewChannel(me))context=me;
    const Transition& effective = transition;
    const QString suffix = effective.type == TransitionType::Move || effective.type == TransitionType::Cube || effective.type == TransitionType::Zoom || effective.type == TransitionType::PageCurl || effective.type == TransitionType::PageRoll || effective.type == TransitionType::SonyDme || effective.type == TransitionType::Nam || (effective.type == TransitionType::SuperMix || effective.type == TransitionType::DustMix)
        ? nativeDmeSuffix(effective) : effective.type == TransitionType::Smil
        ? smilWipeStingSuffix(effective)
        : effective.amcpSuffix();
    const auto* routed = m_config->sourceById(sourceId);
    if (sourceId == kReentrySource || sourceId == 23 || (routed && (routed->type == SourceType::MeProgram || routed->type==SourceType::SuperSource))) {
        const int upstream = sourceChannel(sourceId,context);
        if (upstream < 0) {
            return {};
        }
        // A rendered M/E behaves as one camera frame under destination keys.
        return QStringLiteral("PLAY %1-1 route://%2 RENDERED").arg(destChannel).arg(upstream) + suffix;
    }
    const int channel = sourceChannel(sourceId,context);
    if (channel < 0) {
        return {};
    }
    if(effective.type==TransitionType::Move||effective.type==TransitionType::Cube||effective.type==TransitionType::Zoom||effective.type==TransitionType::PageCurl||effective.type==TransitionType::PageRoll||effective.type==TransitionType::SonyDme||effective.type==TransitionType::Nam||(effective.type==TransitionType::SuperMix||effective.type==TransitionType::DustMix))
        return QStringLiteral("PLAY %1-1 route://%2 RENDERED").arg(destChannel).arg(channel)+suffix;
    return routePlayCommand(destChannel, channel, effective);
}

QString SwitcherEngine::mixerFillCommand(int channel, int layer, const Source& source) const
{
    const QRectF fill = source.mixerFill(m_config->videoWidth(), m_config->videoHeight());
    return QStringLiteral("MIXER %1-%2 FILL %3 %4 %5 %6")
        .arg(channel)
        .arg(layer)
        .arg(fill.x(), 0, 'f', 4)
        .arg(fill.y(), 0, 'f', 4)
        .arg(fill.width(), 0, 'f', 4)
        .arg(fill.height(), 0, 'f', 4);
}

QStringList SwitcherEngine::dskLayerCommands(int destChannel, bool on) const
{
    if (!on) {
        return {dskStopCommand(destChannel)};
    }
    const Source* dsk = m_config->sourceById(m_config->dskSourceId());
    if (!dsk) {
        return {dskStopCommand(destChannel)};
    }
    return overlayLayer(destChannel, kDskLayer, dsk->id, true, 0);
}

QString SwitcherEngine::dskStopCommand(int destChannel) const
{
    return QStringLiteral("CLEAR %1-%2").arg(destChannel).arg(kDskLayer);
}

QString SwitcherEngine::resolveShareHtml(const QString& fileName) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).absoluteFilePath(QStringLiteral("share/%1").arg(fileName)),
        QDir(appDir).absoluteFilePath(QStringLiteral("../share/%1").arg(fileName)),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../share/%1").arg(fileName)),
        QDir(appDir).absoluteFilePath(QStringLiteral("../share/kavtor/%1").arg(fileName)),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return {};
}

QString SwitcherEngine::resolveWipeMattePath() const
{
    return resolveShareHtml(QStringLiteral("wipe-matte.html"));
}

QString SwitcherEngine::wipeMatteUrl(const Transition& transition, const QString& role) const
{
    const QString path = resolveWipeMattePath();
    if (path.isEmpty() || transition.smilType.isEmpty()) {
        return {};
    }
    QUrl url = QUrl::fromLocalFile(path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), transition.smilType);
    query.addQueryItem(QStringLiteral("subtype"), transition.smilSubtype);
    query.addQueryItem(QStringLiteral("sense"), transition.reverse ? QStringLiteral("rev") : QStringLiteral("fwd"));
    query.addQueryItem(QStringLiteral("edge"), wipeEdgeModeToString(transition.edge));
    query.addQueryItem(QStringLiteral("amount"), QString::number(transition.edgeAmount));
    query.addQueryItem(QStringLiteral("border"), QString::number(transition.borderAmount));
    query.addQueryItem(QStringLiteral("shadow"), QString::number(transition.shadowAmount));
    query.addQueryItem(QStringLiteral("multi"), QString::number(transition.multi));
    query.addQueryItem(QStringLiteral("aw"), QString::number(transition.aspectW));
    query.addQueryItem(QStringLiteral("ah"), QString::number(transition.aspectH));
    query.addQueryItem(QStringLiteral("px"), QString::number(transition.posX));
    query.addQueryItem(QStringLiteral("py"), QString::number(transition.posY));
    query.addQueryItem(QStringLiteral("color"), transition.borderColor.isEmpty()
        ? QStringLiteral("#ffffff")
        : transition.borderColor);
    query.addQueryItem(QStringLiteral("frames"), QString::number(transition.durationFrames));
    query.addQueryItem(QStringLiteral("fps"), QStringLiteral("50"));
    query.addQueryItem(QStringLiteral("autorun"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("t0"), QString::number(
        m_wipeClockMs > 0 ? m_wipeClockMs : QDateTime::currentMSecsSinceEpoch()));
    query.addQueryItem(QStringLiteral("role"), role);
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

int SwitcherEngine::nativeSonyCode(const Transition& transition, bool* reverse) const
{
    if (transition.type != TransitionType::Smil || transition.shadowAmount > 0)
        return 0;
    for (int code : supportedSonyWipes(m_expandedSonyAvailable,m_enhancedSonyAvailable,m_rotarySonyAvailable,m_mosaicSonyAvailable,m_compoundSonyAvailable,m_karaokeSonyAvailable,m_randomSonyAvailable,m_completeSonyWipesAvailable)) {
        WipePattern pattern;
        bool inherent = false;
        if (lookupWipeBySony(code, &pattern, &inherent) && pattern.smilType == transition.smilType &&
            pattern.smilSubtype == transition.smilSubtype) {
            if (reverse) *reverse = transition.reverse != inherent;
            return code;
        }
    }
    return 0;
}

QString SwitcherEngine::nativeWipeOptions(const Transition& transition) const
{
    bool reverse = false;
    nativeSonyCode(transition, &reverse);
    const int softness = transition.edge == WipeEdgeMode::Soft ? transition.edgeAmount : 0;
    const int border = transition.borderAmount > 0 ? transition.borderAmount
        : transition.edge == WipeEdgeMode::Border ? transition.edgeAmount : 0;
    return (m_dustMixAvailable?QString("BORDER_SIDE %1 INNER_SOFT %2 OUTER_SOFT %3 ").arg(transition.borderSide).arg(transition.innerSoft).arg(transition.outerSoft):QString())+(m_mosaicSonyAvailable?QString("TILESIZE %1 ").arg(transition.tileSize):QString())+(m_enhancedSonyAvailable?QString("VERTICES %1 ROUNDING %2 ").arg(transition.vertices).arg(transition.rounding):QString())+QStringLiteral("SOFT %1 BORDER %2 ASPECT %3 MULTI %4 X %5 Y %6 REVERSE %7")
        .arg(softness).arg(border).arg(double(transition.aspectW)/qMax(1,transition.aspectH),0,'f',6)
        .arg(transition.multi).arg(transition.posX/1000.0,0,'f',6)
        .arg(transition.posY/1000.0,0,'f',6).arg(reverse ? 1 : 0);
}

QString SwitcherEngine::nativeWipeSuffix(const Transition& transition, bool manual) const
{
    const int code = nativeSonyCode(transition);
    if (!code) return {};
    return QStringLiteral(" WIPESONY %1 SONY %2 %3 BORDERCOLOR %4 MANUAL %5")
        .arg(qMax(1,transition.durationFrames)).arg(code).arg(nativeWipeOptions(transition))
        .arg(transition.borderColor.isEmpty() ? QStringLiteral("#ffffff") : transition.borderColor)
        .arg(manual ? 1 : 0);
}

QString SwitcherEngine::smilWipeStingSuffix(const Transition& transition) const
{
    const QString native = nativeWipeSuffix(transition);
    if (!native.isEmpty()) return native;
    const QString url = wipeMatteUrl(transition, QStringLiteral("mask"));
    if (url.isEmpty() || transition.durationFrames <= 0) {
        return {};
    }
    return QStringLiteral(" STING (MASK=\"[HTML] %1\" audio_fade_duration=%2)")
        .arg(url, QString::number(transition.durationFrames));
}

QString SwitcherEngine::wipeBorderLayerCommand(const Transition& transition) const
{
    const int dest = activeProgramChannel();
    const bool bordered = transition.shadowAmount > 0 || transition.borderAmount > 0
        || (transition.edge == WipeEdgeMode::Border && transition.edgeAmount > 0);
    if (transition.type != TransitionType::Smil || !bordered || nativeSonyCode(transition)) {
        return QStringLiteral("CLEAR %1-%2").arg(dest).arg(kWipeBorderLayer);
    }
    const QString url = wipeMatteUrl(transition, QStringLiteral("overlay"));
    if (url.isEmpty()) {
        return QStringLiteral("CLEAR %1-%2").arg(dest).arg(kWipeBorderLayer);
    }
    return QStringLiteral("PLAY %1-%2 [HTML] \"%3\"").arg(dest).arg(kWipeBorderLayer).arg(url);
}


void SwitcherEngine::startOscListener()
{
    m_osc->start(static_cast<quint16>(m_config->oscPort()));
}

int SwitcherEngine::sourceIdForChannel(int channel) const
{
    for (const Source& source : m_config->sources()) {
        if ((source.type==SourceType::SuperSource?superSourceChannel(source.id,m_activeMe):source.casparChannel) == channel) {
            return source.id;
        }
    }
    return -1;
}

void SwitcherEngine::ingestPlayback(const OscPlayback& playback) {
    if(playback.layer!=1)return;const int id=sourceIdForChannel(playback.channel);const auto* source=m_config->sourceById(id);
    if(id<0||id>=m_clipClocks.size()||!source||!source->isClip())return;
    auto& state=m_clipClocks[id];if(playback.hasPaused){state.playbackKnown=true;state.paused=playback.paused;}
    if(playback.nativeFps>0)state.nativeFps=playback.nativeFps;
}

void SwitcherEngine::ingestFileTime(int channel, int layer, double elapsed, double duration)
{
    if (layer != 1) {
        return;
    }
    const int sourceId = sourceIdForChannel(channel);
    if (sourceId < 0 || sourceId >= m_clipClocks.size()) {
        return;
    }
    const Source* source = m_config->sourceById(sourceId);
    if (!source || source->type != SourceType::File) {
        m_clipClocks[sourceId] = OscFileTime{};
        return;
    }
    OscFileTime time=m_clipClocks[sourceId];
    time.channel = channel;
    time.layer = layer;
    time.elapsed = elapsed;
    time.duration = duration;
    if (sourceId == m_deckSource && m_deckFrameKnown && (m_deckPaused || m_deckSeekInFlight)) {
        time.elapsed = double(m_deckFrame) / double(kVideoFps);
    } else if (sourceId == m_deckSource && !m_deckSeekInFlight) {
        m_deckFrame = qMax(0, qRound(elapsed * double(kVideoFps)));
        m_deckFrameKnown = true;
    }
    m_clipClocks[sourceId] = time;
}

void SwitcherEngine::applyMultiviewLayout()
{
    if (!m_connected) {
        return;
    }
    const int mv = m_config->multiviewChannel();
    const bool programLeft = m_config->programLeft();
    send(QStringLiteral("MIXER %1-10 FILL %2 0 0.333333 0.4").arg(mv).arg(programLeft ? QStringLiteral("0.166667") : QStringLiteral("0.5")));
    send(QStringLiteral("MIXER %1-9 FILL %2 0 0.333333 0.4").arg(mv).arg(programLeft ? QStringLiteral("0.5") : QStringLiteral("0.166667")));
    sendNativeGraphics();
}

void SwitcherEngine::syncMeterTimer()
{
    const bool run = m_connected && m_config->meters();
    if (run) {
        if (!m_meterTimer->isActive()) {
            m_meterTimer->start();
        }
        return;
    }
    if (m_meterTimer->isActive()) {
        m_meterTimer->stop();
    }
    for (int i = 0; i < kMeterSlots; ++i) {
        m_meterHold[i][0] = 0;
        m_meterHold[i][1] = 0;
    }
}

int SwitcherEngine::meterSlot(int casparChannel) const
{
    // casparMIX publishes audio peaks even without video consumers. Sources and
    // M/E meter channels must not use empty Art-Net consumers: those force an
    // unnecessary full-raster render and GPU-to-CPU readback each frame.
    const int sourceId = sourceIdForChannel(casparChannel);
    if (sourceId >= 0 && sourceId < m_config->maxSources()) return sourceId;
    if (casparChannel == mePreviewChannel(0)) {
        return 24;
    }
    if (casparChannel == airChannel()) {
        return 25;
    }
    for (int me = 1; me < kMeCount; ++me) {
        if (casparChannel == mePreviewChannel(me)) return 26 + (me-1)*2;
        if (casparChannel == meProgramChannel(me)) return 27 + (me-1)*2;
    }
    if (casparChannel == meProgramChannel(0)) return 32; // Clean program reentry differs from final air.
    return -1;
}

void SwitcherEngine::ingestAudioPeaks(const QVector<OscAudioPeak>& peaks)
{
    if (!m_config->meters()) {
        return;
    }
    for (const OscAudioPeak& peak : peaks) {
        const int slot = meterSlot(peak.channel);
        if (slot < 0 || slot >= kMeterSlots) {
            continue;
        }
        m_meterHold[slot][0] = qMax(m_meterHold[slot][0], meterUnit(peak.left));
        m_meterHold[slot][1] = qMax(m_meterHold[slot][1], meterUnit(peak.right));
    }
}

void SwitcherEngine::flushOverlayMeters()
{
    if (!m_nativeMvReady || !m_connected || !m_config->meters()) {
        return;
    }
    // M/E aliases and both fixed cascade crosspoints show complete program audio.
    for (int id = 0; id < m_config->maxSources(); ++id) {
        const int me = sourceMe(id, m_activeMe);
        if (me < 0) continue;
        const int slot = sourceSelectable(id) ? meterSlot(meProgramChannel(me)) : -1;
        for (int side = 0; side < 2; ++side) m_meterHold[id][side] = slot >= 0 ? m_meterHold[slot][side] : 0;
    }
    m_nativeMv.meters.resize(kMeterSlots);
    for (int i=0;i<kMeterSlots;++i) {
        m_nativeMv.meters[i]={m_meterHold[i][0],m_meterHold[i][1]};
        m_meterHold[i][0]=m_meterHold[i][1]=0;
    }
    QJsonObject updates;
    QVector<int> visible;
    for(int i=0;i<12;++i)visible.append(m_multiviewBank*12+i);
    for(int i=24;i<32;++i)visible.append(i);
    for(int slot:visible)for(int side=0;side<2;++side)for(int band=0;band<3;++band){
        double value=m_nativeMv.meters[slot][side]/1000.;
        updates.insert(QString("meter-%1-%2-%3").arg(slot).arg(side).arg(band),qBound(0.,value,1.));
    }
    const auto bytes=QJsonDocument(updates).toJson(QJsonDocument::Compact);
    if(bytes==m_lastNativeValues)return;
    m_lastNativeValues=bytes;
    send(QStringLiteral("CALL %1-80 VALUES %2").arg(m_config->multiviewChannel()).arg(QString::fromLatin1(bytes.toBase64())));
}

void SwitcherEngine::flushOverlayClocks()
{
    if (!m_nativeMvReady || !m_connected) {
        return;
    }
    const qint64 now=QDateTime::currentMSecsSinceEpoch();
    if(now-m_lastOverlayHeartbeat>=1000||now<m_lastOverlayHeartbeat) {
        m_lastOverlayHeartbeat=now;
        send(QStringLiteral("CALL %1-80 HEARTBEAT").arg(m_config->multiviewChannel()));
    }
    sendNativeGraphics();
}

QStringList SwitcherEngine::sourceLabelNames() const
{
    QStringList names;
    names.reserve(m_config->maxSources());
    for (int i = 0; i < m_config->maxSources(); ++i) {
        QString name = m_config->sourceName(i).trimmed();
        if (i == 11 || i == 23) {
            name = m_activeMe < 3 ? QStringLiteral("M/E %1 PROGRAM").arg(m_activeMe + 2)
                                  : QStringLiteral("NO NEXT M/E");
        } else {
            // Accept manually numbered legacy names without altering saved data.
            const QRegularExpression prefix(QStringLiteral("^%1\\s*-\\s*").arg(i+1));
            name.remove(prefix);
            if (name.isEmpty()) name = QStringLiteral("SRC%1").arg(i+1);
        }
        names.append(QStringLiteral("%1 - %2").arg(i+1).arg(name));
    }
    return names;
}

QVector<bool> SwitcherEngine::sourceVacantFlags() const
{
    QVector<bool> flags;
    flags.reserve(m_config->maxSources());
    for (int i = 0; i < m_config->maxSources(); ++i) {
        const Source* source = m_config->sourceById(i);
        flags.append(!sourceSelectable(i));
    }
    return flags;
}

void SwitcherEngine::sendNativeGraphics()
{
    if(!m_connected||!m_nativeMvReady)return;
    m_nativeMv.names=sourceLabelNames();m_nativeMv.vacant=sourceVacantFlags();
    m_nativeMv.transportIcons=m_transportGraphicsAvailable;m_nativeMv.clocks=m_clipClocks;m_nativeMv.bank=m_multiviewBank;
    m_nativeMv.me=m_activeMe+1;m_nativeMv.ready=outputsReady();
    m_nativeMv.armed=m_deckSource;m_nativeMv.cued=m_cuedSource;
    auto scene=m_nativeMv.scene(*m_config);
    auto signature=scene;signature.remove("meters");auto nodes=signature.value("nodes").toArray();
    for(int i=0;i<nodes.size();++i){auto n=nodes[i].toObject();if(n.value("type")=="bar")n.remove("value");nodes[i]=n;}
    signature.insert("nodes",nodes);
    const auto key=QJsonDocument(signature).toJson(QJsonDocument::Compact);
    if(key==m_lastNativeScene)return;
    m_lastNativeScene=key;
    m_lastNativeValues.clear();
    const auto bytes=QJsonDocument(scene).toJson(QJsonDocument::Compact);
    send(QStringLiteral("CALL %1-80 SCENE %2").arg(m_config->multiviewChannel()).arg(QString::fromLatin1(bytes.toBase64())));
}

void SwitcherEngine::flushOverlayLabels()
{
    if (!m_connected) {
        return;
    }
    sendNativeGraphics();
    flushOverlayArmed();
    syncOverlayTally();
}

void SwitcherEngine::flushOverlayArmed()
{
    if (!m_connected) {
        return;
    }
    sendNativeGraphics();
}

void SwitcherEngine::playCuedIfNeeded(int sourceId, QStringList* commands)
{
    if (sourceId < 0 || sourceId != m_cuedSource) {
        return;
    }
    const Source* source = m_config->sourceById(sourceId);
    if (source && source->isClip() && commands) {
        commands->prepend(QStringLiteral("RESUME %1-1").arg(source->casparChannel));
    }
    m_cuedSource = -1;
    if (m_deckSource == sourceId) {
        m_deckPaused = false;resetDeckMotion(false);m_clipClocks[sourceId].scrubDirection=0;
    }
    emit deckCueChanged(-1);
    flushOverlayArmed();
}

void SwitcherEngine::selectDeckSource(int sourceId)
{
    int next = -1;
    if (sourceId == m_deckSource) {
        next = -1;
    } else {
        const Source* source = m_config->sourceById(sourceId);
        if (source && source->isClip()) {
            next = sourceId;
        }
    }
    if (next == m_deckSource) {
        return;
    }
    m_deckSource = next;
    m_deckPaused = next>=0&&m_clipClocks[next].playbackKnown?m_clipClocks[next].paused:(next>=0&&next==m_cuedSource);
    resetDeckMotion(true);
    if (m_deckSource >= 0) {
        seedDeckFrame();
    }
    emit deckSourceChanged(m_deckSource);
    flushOverlayArmed();
}

void SwitcherEngine::setDeckJogMode(DeckJogMode mode)
{
    if (m_deckJogMode == mode) {
        return;
    }
    m_deckJogMode = mode;
    resetDeckMotion(false);
}

void SwitcherEngine::resetDeckMotion(bool resetPosition)
{
    m_jogUnits = 0;
    m_pendingJogFrames = 0;
    m_shuttleValue = 0;
    m_shuttleAcc = 0;
    if (resetPosition) {
        m_deckFrame = 0;
        m_deckFrameKnown = false;
        m_deckSeekInFlight = false;
        m_deckSentFrame = -1;
        m_deckSeekCommand.clear();
    }
    if (m_deckTimer) {
        m_deckTimer->stop();
    }
}

void SwitcherEngine::seedDeckFrame()
{
    if (m_deckSource < 0 || m_deckSource >= m_clipClocks.size()) {
        m_deckFrame = 0;
        m_deckFrameKnown = true;
        return;
    }
    const OscFileTime& clock = m_clipClocks[m_deckSource];
    m_deckFrame = qMax(0, qRound(clock.elapsed * double(kVideoFps)));
    m_deckFrameKnown = true;
}

void SwitcherEngine::syncDeckClock()
{
    if (m_deckSource < 0 || m_deckSource >= m_clipClocks.size()) {
        return;
    }
    OscFileTime& clock = m_clipClocks[m_deckSource];
    clock.elapsed = double(m_deckFrame) / double(kVideoFps);
    if (clock.duration > 0) {
        clock.elapsed = qBound(0.0, clock.elapsed, clock.duration);
        m_deckFrame = qMin(m_deckFrame, qMax(0, qRound(clock.duration * double(kVideoFps))));
    }
}

void SwitcherEngine::ensureDeckPaused()
{
    if (m_deckPaused || !m_connected || m_deckSource < 0) {
        return;
    }
    const Source* source = m_config->sourceById(m_deckSource);
    if (!source || !source->isClip()) {
        return;
    }
    m_deckPaused = true;
    send(QStringLiteral("PAUSE %1-1").arg(source->casparChannel));
}

void SwitcherEngine::jogDeck(int value)
{
    if (!m_connected || m_deckSource < 0) {
        return;
    }
    const Source* source = m_config->sourceById(m_deckSource);
    if (!source || !source->isClip()) {
        return;
    }
    if (m_deckJogMode == DeckJogMode::Shuttle) {
        m_shuttleValue = value;
        if (qAbs(m_shuttleValue) < kShuttleDeadzone) {
            m_shuttleValue = 0;
            m_shuttleAcc = 0;
        }
        ensureDeckPaused();
        if (m_shuttleValue != 0) {
            if (!m_deckTimer->isActive()) {
                m_deckTimer->start();
                onDeckTick();
            }
        } else if (m_pendingJogFrames == 0) {
            m_deckTimer->stop();
        }
        return;
    }
    if (value == 0) {
        return;
    }
    const int unitsPerFrame = (m_deckJogMode == DeckJogMode::Scroll)
        ? kScrollUnitsPerFrame
        : kJogUnitsPerFrame;
    m_jogUnits += value;
    const int frames = int(m_jogUnits / unitsPerFrame);
    m_jogUnits -= qint64(frames) * unitsPerFrame;
    if (frames == 0) {
        return;
    }
    m_pendingJogFrames += frames;
    ensureDeckPaused();
    flushDeckSeek();
}

void SwitcherEngine::onDeckTick()
{
    if (m_deckJogMode == DeckJogMode::Shuttle && m_shuttleValue != 0) {
        m_shuttleAcc += shuttleRate() * double(kVideoFps) * (double(kDeckTickMs) / 1000.0);
        const int frames = int(m_shuttleAcc);
        m_shuttleAcc -= frames;
        m_pendingJogFrames += frames;
    }
    flushDeckSeek();
    if (m_pendingJogFrames == 0
        && (m_deckJogMode != DeckJogMode::Shuttle || m_shuttleValue == 0)
        && m_jogUnits == 0) {
        m_deckTimer->stop();
    }
}

double SwitcherEngine::shuttleRate() const
{
    const int absVal = qAbs(m_shuttleValue);
    if (absVal <= kShuttleDeadzone) {
        return 0.0;
    }
    const double span = double(kShuttleMax - kShuttleDeadzone);
    double t = double(absVal - kShuttleDeadzone) / span;
    t = qBound(0.0, t, 1.0);
    const double mag = t * t * t * 8.0;
    return std::copysign(mag, m_shuttleValue);
}

bool SwitcherEngine::isDeckSeekCommand(const QString& command) const
{
    return !m_deckSeekCommand.isEmpty() && command == m_deckSeekCommand;
}

void SwitcherEngine::onDeckSeekFinished()
{
    m_deckSeekInFlight = false;
    m_deckSeekCommand.clear();
    flushDeckSeek();
}

void SwitcherEngine::flushDeckSeek()
{
    if (!m_connected || m_deckSource < 0) {
        return;
    }
    const Source* source = m_config->sourceById(m_deckSource);
    if (!source || !source->isClip()) {
        m_pendingJogFrames = 0;
        return;
    }
    if (!m_deckPaused){m_pendingJogFrames=0;return;}
    if (!m_deckFrameKnown) {
        seedDeckFrame();
    }
    const int applied = m_pendingJogFrames;
    if (applied != 0) {
        m_clipClocks[m_deckSource].scrubDirection=applied<0?-1:1;
        m_clipClocks[m_deckSource].scrubUntilMs=clipMonotonicMs()+300;
        m_deckFrame = qMax(0, m_deckFrame + applied);
        m_pendingJogFrames = 0;
        syncDeckClock();
    }
    if (m_deckSeekInFlight || m_deckFrame == m_deckSentFrame) {
        return;
    }
    if (m_deckSentFrame < 0 && applied == 0) {
        return;
    }
    // Absolute SEEK so a queued command cannot rebase on a stale producer time().
    m_deckSeekCommand = QStringLiteral("CALL %1-1 SEEK %2").arg(source->casparChannel).arg(m_deckFrame);
    m_deckSentFrame = m_deckFrame;
    m_deckSeekInFlight = true;
    send(m_deckSeekCommand);
}

void SwitcherEngine::toggleDeckPause()
{
    if (!m_connected || m_deckSource < 0) {
        return;
    }
    const Source* source = m_config->sourceById(m_deckSource);
    if (!source || !source->isClip()) {
        return;
    }
    m_deckPaused = !m_deckPaused;
    resetDeckMotion(false); // PLAY/PAUSE cancels transient shuttle and queued seeks.
    m_clipClocks[m_deckSource].scrubDirection=0;
    send(QStringLiteral("%1 %2-1")
             .arg(m_deckPaused ? QStringLiteral("PAUSE") : QStringLiteral("RESUME"))
             .arg(source->casparChannel));
    if (!m_deckPaused && m_cuedSource == m_deckSource) {
        m_cuedSource = -1;
        emit deckCueChanged(-1);
        flushOverlayArmed();
    }
}

void SwitcherEngine::toggleDeckCue()
{
    if (!m_connected) {
        emit error(tr("Not connected to CasparCG"));
        return;
    }
    if (m_deckSource < 0) {
        if (m_cuedSource < 0) {
            return;
        }
        m_cuedSource = -1;
        emit deckCueChanged(-1);
        flushOverlayArmed();
        return;
    }
    const Source* source = m_config->sourceById(m_deckSource);
    if (!source || !source->isClip()) {
        return;
    }
    if (m_cuedSource == m_deckSource) {
        m_cuedSource = -1;
        emit deckCueChanged(-1);
        flushOverlayArmed();
        return;
    }
    m_cuedSource = m_deckSource;
    if (!m_deckPaused) {
        m_deckPaused = true;
        send(QStringLiteral("PAUSE %1-1").arg(source->casparChannel));
    }
    emit deckCueChanged(m_cuedSource);
    flushOverlayArmed();
}

int SwitcherEngine::sourceMe(int id, int owner) const
{
    if (id >= 1000 && id <= 1003) return id - 1000;
    if (id == 11 || id == 23) return owner < 3 ? owner + 1 : -1;
    const auto* src = m_config->sourceById(id);
    return src && src->isAssigned() && src->type == SourceType::MeProgram ? src->argument.toInt() - 1 : -1;
}
bool SwitcherEngine::sourceWouldFeedback(int sourceId,int owner) const {
    QSet<int> seenSources,seenMes;
    std::function<bool(int,int)> sourceReach;
    std::function<bool(int)> meReach=[&](int me){if(me==owner)return true;if(me<0||me>=4||seenMes.contains(me))return false;seenMes.insert(me);
        if(sourceReach(programSource(me),me)||sourceReach(previewSource(me),me))return true;
        for(int key=0;key<4;++key)if((keyOn(me,key)||nextKey(me,key))&&sourceReach(keySource(me,key),me))return true;
        const auto& manual=me==m_activeMe?m_manual:m_manualBanks[me];
        if(manual.active&&sourceReach(manual.transition.dmeBackground,me))return true;
        if(me==m_activeMe&&m_takeActive&&!manual.active&&sourceReach(m_takeDmeBackground,me))return true;
        return false;};
    sourceReach=[&](int id,int context){if(id<0)return false;
        int me=sourceMe(id,context);if(me>=0)return meReach(me);
        if(seenSources.contains(superSourceKey(id,context)))return false;seenSources.insert(superSourceKey(id,context));auto source=m_config->sourceById(id);
        if(!source||!source->enabled||source->type!=SourceType::SuperSource)return false;
        auto layout=m_config->superSource(source->argument);if(!layout)return false;
        auto bindings=m_superSourceBindings.value(superSourceKey(id,context));
        for(auto& box:layout->boxes)if(box.kind=="input"&&sourceReach(bindings.value(box.id,box.input),context))return true;
        return false;};
    return sourceReach(sourceId,owner);
}
bool SwitcherEngine::routeWouldFeedback(int owner,int target) const {
    if(target<0||target>=4)return true;
    if(target==owner)return true;
    return sourceWouldFeedback(programSource(target),owner)||sourceWouldFeedback(previewSource(target),owner);
}
bool SwitcherEngine::sourceSelectable(int id) const { return sourceChannel(id) >= 0; }
void SwitcherEngine::setMultiviewBank(int bank)
{
    if (bank < 0 || bank > 1 || bank == m_multiviewBank) return;
    m_multiviewBank = bank;
    if (m_connected) { syncMultiviewSources(); flushOverlayLabels(); }
}
void SwitcherEngine::syncMultiviewSources()
{
    if (!m_connected) return;
    const int mv = m_config->multiviewChannel();
    for (int slot = 0; slot < 12; ++slot) {
        const int id = m_multiviewBank * 12 + slot, layer = 41 + slot, channel = sourceChannel(id);
        const auto* src = m_config->sourceById(id);
        const bool whole = slot == 11 || (src && (src->type == SourceType::MeProgram || src->type==SourceType::SuperSource));
        // A new cascade may be empty. Retire the previous route explicitly:
        // transition readiness would otherwise keep showing the old M/E under
        // the new label until the target starts producing a picture.
        if (slot == 11 && channel >= 0)
            send(QStringLiteral("CLEAR %1-%2").arg(mv).arg(layer));
        send(channel < 0 ? QStringLiteral("CLEAR %1-%2").arg(mv).arg(layer)
             : QStringLiteral("PLAY %1-%2 route://%3%4").arg(mv).arg(layer).arg(channel).arg(whole ? QStringLiteral(" RENDERED") : QStringLiteral("-1")));
        send(QStringLiteral("MIXER %1-%2 FILL %3 %4 0.166667 0.2")
             .arg(mv).arg(layer).arg(1.0/6 + slot % 4 / 6.0, 0, 'f', 6).arg(0.4 + slot / 4 * 0.2, 0, 'f', 6));
    }
}
void SwitcherEngine::syncOverlayTally()
{
    if (!m_connected) return;
    const QString take = activeTakeName();
    const bool both = m_transitioning && !m_transitionPreview && !m_ftb &&
        (take == QLatin1String("mix") || take == QLatin1String("wipe") || take == QLatin1String("dme")) &&
        m_nextBackground && m_previewSource >= 0 && m_previewSource != m_programSource;
    m_nativeMv.preview=m_previewSource;m_nativeMv.program=m_programSource;m_nativeMv.both=both;
    sendNativeGraphics();
}

int SwitcherEngine::outputSourceChannel(int source) const
{
    if (source >= 1000 && source <= 1003) return meProgramChannel(source - 1000);
    if (source >= 1100 && source <= 1103) return mePreviewChannel(source - 1100);
    if (source == 1200) return m_config->multiviewChannel();
    if (source == 1201) return airChannel();
    if (source == 11 || source == 23) return -1;
    const auto* input=m_config->sourceById(source);
    if (!input || !input->isAssigned()) return -1;
    return input->type == SourceType::MeProgram ? meProgramChannel(input->argument.toInt() - 1) : input->type==SourceType::SuperSource?superSourceChannel(source,m_activeMe):input->casparChannel;
}
bool SwitcherEngine::setAuxSource(int role, int source)
{
    if (role < 1 || role > 4 || outputSourceChannel(source) < 1 || !m_connected) return false;
    auto outputs=m_config->destinations();
    for (auto& out : outputs) if(out.enabled && out.aux == role) {
        out.source=source;
        if (!m_config->setDestinations(outputs)) return false;
        setupOutputs(); return true;
    }
    return false;
}

bool SwitcherEngine::setKeyProcessing(int slot,bool dsk,const ::KeyProcessing& processing)
{
    return setKeyProcessingForMe(m_activeMe,slot,dsk,processing);
}

bool SwitcherEngine::setKeyProcessingForMe(int me,int slot,bool dsk,const ::KeyProcessing& processing)
{
    ::KeyProcessing validated;
    if(me<0||me>=kMeCount||slot<0||slot>=(dsk?2:kKeyerCount)||!m_connected||isBusy()||!validated.update(processing.toJson())||(processing.requiresNative()&&!m_nativeKeyAvailable))return false;
    QStringList commands;
    if(dsk) {
        commands+=processing.commands(airChannel(),kDskLayer+slot,m_nativeKeyAvailable);
        commands+=processing.commands(m_config->previewChannel(),kDskLayer+slot,m_nativeKeyAvailable);
    } else {
        commands+=processing.commands(mePreviewChannel(me),kKeyLayer0+slot,m_nativeKeyAvailable);
        commands+=((me==m_activeMe?m_keyRouted[slot]:m_bank[me].keyRouted[slot])?::KeyProcessing{}:processing).commands(meProgramChannel(me),kKeyLayer0+slot,m_nativeKeyAvailable);
    }
    m_pendingProcessing=processing;m_pendingProcessingMe=me;m_pendingProcessingSlot=slot;m_pendingProcessingDsk=dsk;
    beginPending(PendingKind::KeyProcessing,commands,m_previewSource,m_programSource);
    return true;
}

QHash<QString,int> SwitcherEngine::superSourceBindings(int sourceId,int context) const {return m_superSourceBindings.value(superSourceKey(sourceId,context<0?m_activeMe:context));}
int SwitcherEngine::superSourceChannel(int sourceId,int context) const {auto src=m_config->sourceById(sourceId);return !src?-1:context==0?src->casparChannel:39+sourceId*3+context-1;}
QStringList SwitcherEngine::compositionCommands(int id,int context) const {
    const int owner=context<0?m_activeMe:context;
    const auto source=m_config->sourceById(id);if(!source||!source->isAssigned()||source->type!=SourceType::SuperSource)return {};
    const auto layout=m_config->superSource(source->argument);if(!layout)return {};
    auto route=[&](int input){if(input>=1000&&input<=1003)return QString("route://%1 RENDERED").arg(meProgramChannel(input-1000));auto src=m_config->sourceById(input);if(!src||!src->isAssigned()||input==11||input==23)return QString();
        if(src->type==SourceType::MeProgram)return QString("route://%1 RENDERED").arg(meProgramChannel(src->argument.toInt()-1));
        return QString("route://%1%2").arg(src->type==SourceType::SuperSource?superSourceChannel(input,owner):src->casparChannel).arg(src->type==SourceType::SuperSource?" RENDERED":"-1");};
    return superSourceCommands(*layout,superSourceChannel(id,owner),double(m_config->videoWidth())/m_config->videoHeight(),route,m_superSourceBindings.value(superSourceKey(id,owner)));
}
void SwitcherEngine::setupSuperSources() {
    if(!m_connected||!m_config->superSourceGraphError().isEmpty())return;
    for(auto& source:m_config->sources())for(int owner=0;owner<4;++owner){
        const int key=superSourceKey(source.id,owner),channel=superSourceChannel(source.id,owner);
        if(source.type!=SourceType::SuperSource||source.id==11||source.id==23){
            const auto previous=m_armedSources.value(key);
            if(owner>0&&previous.mixerCommand=="SUPERSOURCE")queueArmBatch({{ArmKind::SourceStop,source.id,previous.channel,QString("CLEAR %1").arg(previous.channel),{},owner}});
            continue;
        }
        bool pending=false;for(auto& batch:m_armBatches)pending|=batch.first().sourceId==source.id&&batch.first().context==owner;if(pending)continue;
        auto previous=m_armedSources.value(key);auto desired=compositionCommands(source.id,owner);
        if(desired.isEmpty()){if(previous.channel>0)queueArmBatch({{ArmKind::SourceStop,source.id,previous.channel,QString("CLEAR %1").arg(previous.channel),{},owner}});continue;}
        auto layout=m_config->superSource(source.argument);bool waiting=false;
        for(auto& box:layout->boxes)if(box.kind=="input"){int input=m_superSourceBindings.value(key).value(box.id,box.input);auto src=m_config->sourceById(input);
            if(src&&src->isAssigned()&&src->type!=SourceType::MeProgram&&!sourceReady(input,owner))waiting=true;
        }if(waiting)continue;
        const auto signature=desired.join('\n');if(m_failedCompositions.value(key)==signature)continue;if(previous.channel==channel&&previous.playCommand==signature)continue;
        QStringList updates;
        if(previous.channel>0&&previous.channel!=channel)updates.append(QString("CLEAR %1").arg(previous.channel));
        const auto old=previous.playCommand.split('\n');
        if(previous.channel!=channel||previous.mixerCommand!="SUPERSOURCE")updates=updates+desired;
        else {
            for(auto& cmd:desired)if(!cmd.startsWith("CLEAR ")&&!old.contains(cmd))updates.append(cmd);
            for(auto& cmd:old)if(cmd.startsWith("PLAY ")){const auto target=cmd.section(' ',1,1);bool retained=false;for(auto& now:desired)retained|=now.startsWith("PLAY "+target+' ');if(!retained)updates.append("CLEAR "+target);}
        }
        if(updates.isEmpty())continue;QList<ArmCommand> batch;
        for(int i=0;i<updates.size();++i)batch.append({i==updates.size()-1?ArmKind::SourceCompose:ArmKind::SourceComposePart,source.id,channel,updates[i],signature,owner});
        queueArmBatch(batch);
    }
}
bool SwitcherEngine::setSuperSourceInput(int id,const QString& boxId,int input) {
    auto source=m_config->sourceById(id);if(!m_connected||!source||source->type!=SourceType::SuperSource||input < -2||(input>23&&(input<1000||input>1003))||input==11||input==23)return false;
    auto layout=m_config->superSource(source->argument);if(!layout)return false;bool found=false;for(auto& box:layout->boxes)found|=box.id==boxId&&box.kind=="input";if(!found)return false;
    auto previous=m_superSourceBindings;const int instance=superSourceKey(id,m_activeMe);if(input==-2)m_superSourceBindings[instance].remove(boxId);else m_superSourceBindings[instance][boxId]=input;
    QSet<int> visiting,done;std::function<bool(int)> valid=[&](int sourceId){if(visiting.contains(sourceId))return false;if(done.contains(sourceId))return true;auto s=m_config->sourceById(sourceId);if(!s||s->type!=SourceType::SuperSource)return true;auto l=m_config->superSource(s->argument);if(!l)return false;visiting.insert(sourceId);
        for(auto& box:l->boxes)if(box.kind=="input"&&!valid(m_superSourceBindings.value(superSourceKey(sourceId,m_activeMe)).value(box.id,box.input)))return false;visiting.remove(sourceId);done.insert(sourceId);return true;};
    bool safe=valid(id);for(int me=0;me<4;++me)for(int selected:{programSource(me),previewSource(me)})if(sourceWouldFeedback(selected,me))safe=false;
    if(!safe){m_superSourceBindings=previous;return false;}setupSuperSources();
    int effective=input;if(input==-2)for(auto& box:layout->boxes)if(box.id==boxId)effective=box.input;
    emit superSourceBindingChanged(id,boxId,effective);return true;
}

QString SwitcherEngine::nativeDmeSuffix(const Transition& transition,bool manual) const {
    if(transition.type==TransitionType::DustMix){if(!m_dustMixAvailable)return {};return QString(" DUSTMIX %1 MANUAL %2 DUST_RATIO %3 H_SIZE %4 V_SIZE %4 FLASH_RATE %5").arg(qMax(1,transition.durationFrames)).arg(manual?1:0).arg(transition.dustRatio/100.,0,'f',4).arg(transition.dustSize/100.,0,'f',4).arg(transition.dustFlash);}

    if(transition.type!=TransitionType::Move&&transition.type!=TransitionType::Cube&&transition.type!=TransitionType::Zoom&&transition.type!=TransitionType::Push&&transition.type!=TransitionType::Slide&&transition.type!=TransitionType::PageCurl&&transition.type!=TransitionType::PageRoll&&transition.type!=TransitionType::SonyDme&&transition.type!=TransitionType::Nam&&transition.type!=TransitionType::SuperMix)return {};
    QString effect=transition.type==TransitionType::Move?"MOVE":transition.type==TransitionType::Cube?"CUBE":transition.type==TransitionType::PageCurl?"PAGE_CURL":transition.type==TransitionType::PageRoll?"PAGE_ROLL":"ZOOM";
    if(transition.type==TransitionType::Push||transition.type==TransitionType::Slide)effect=(transition.type==TransitionType::Push?"PUSH_":"SLIDE_")+transitionDirectionToken(transition.direction).mid(4);
    if(transition.type==TransitionType::SonyDme){if(!m_primitiveSonyAvailable||!supportedSonyDmes(true,m_spatialSonyAvailable,m_planarSonyAvailable,m_mirrorSonyAvailable,m_frameSonyAvailable,m_edgePageSonyAvailable).contains(transition.sonyDmeCode))return {};effect=QString("SONY_%1").arg(transition.sonyDmeCode);}
    const bool broadcast=transition.type==TransitionType::Nam||(transition.type==TransitionType::SuperMix||transition.type==TransitionType::DustMix);
    if(broadcast){if(!m_broadcastMixAvailable)return {};effect=transition.type==TransitionType::Nam?"NAM":"SUPER_MIX";}
    const QString key=effect.toLower();const int background=transition.dmeBackground==-2?m_config->dmeBackground(key):transition.dmeBackground;
    QString suffix=QString(" DMENATIVE %1 %2 MANUAL %3 REVERSE %4").arg(qMax(1,transition.durationFrames)).arg(effect).arg(manual?1:0).arg(transition.reverse?1:0);
    if((transition.type==TransitionType::SuperMix||transition.type==TransitionType::DustMix))suffix+=QString(" A_GAIN %1 B_GAIN %2").arg(transition.videoGainA/100.,0,'f',4).arg(transition.videoGainB/100.,0,'f',4);
    if(m_pageDmeAvailable&&!broadcast)suffix+=" BACKGROUND "+QString::fromLatin1(dmeBackgroundProducer(background,transition.dmeBackground==-2?m_config->dmeBackgroundImage(key):transition.dmeBackgroundImage).toUtf8().toHex());
    if(transition.type!=TransitionType::Move)return suffix;
    QHash<QString,QString> producers;
    std::function<QString(int,int)> identity=[&](int id,int context)->QString {
        int me=sourceMe(id,context);
        if(me>=0){QString key=QString("me:%1").arg(me);producers[key]=QString("route://%1 RENDERED").arg(meProgramChannel(me));return key;}
        auto src=m_config->sourceById(id);if(!src||!src->isAssigned())return {};
        QString key=QString("input:%1").arg(id);if(src->type==SourceType::SuperSource)key+=QString(":me:%1").arg(context);producers[key]=QString("route://%1 RENDERED").arg(src->type==SourceType::SuperSource?superSourceChannel(id,context):src->casparChannel);return key;
    };
    QSet<int> visiting;
    std::function<MoveScene(int,int)> scene=[&](int id,int context)->MoveScene {
        int me=sourceMe(id,context);
        if(me>=0&&!visiting.contains(me)) {
            bool keys=false;for(int k=0;k<4;++k)keys|=keyOn(me,k);
            const bool moving=me==m_activeMe?m_manual.active||m_transitioning:m_manualBanks[me].active;
            if(!keys&&!moving){visiting.insert(me);auto result=scene(programSource(me),me);visiting.remove(me);return result;}
        }
        auto src=m_config->sourceById(id);
        if(src&&src->type==SourceType::SuperSource){auto layout=m_config->superSource(src->argument);if(layout)return moveSuperSourceScene(*layout,double(m_config->videoWidth())/m_config->videoHeight(),[&](int input){return identity(input,context);},m_superSourceBindings.value(superSourceKey(id,context)));}
        MoveElement e;e.identity=identity(id,context);e.volume=1;if(e.identity.isEmpty())return {};return {e};
    };
    const auto plan=planMove(scene(m_programSource,m_activeMe),scene(m_previewSource,m_activeMe));
    auto rect=[](QRectF r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};
    auto pose=[&](const MoveElement& e){QJsonArray corners;for(auto p:e.perspective){corners.append(p.x());corners.append(p.y());}return QJsonObject{{"fill",rect(e.fill)},{"clip",rect(e.clip)},{"crop",rect(e.crop)},{"opacity",e.opacity},{"volume",e.volume},{"order",e.order},{"perspective",corners}};};
    QJsonArray tracks;
    for(auto& track:plan){QString producer=producers.value(track.from.identity);if(producer.isEmpty()){if(track.from.identity.startsWith("color:"))producer=track.from.identity.mid(6);else if(track.from.identity.startsWith("image:")){producer=track.from.identity.mid(6);producer.replace('\\',"\\\\");producer.replace('"',"\\\"");producer='"'+producer+"\" SCALE_MODE STRETCH";}}
        if(!producer.isEmpty())tracks.append(QJsonObject{{"producer",producer},{"from",pose(track.from)},{"to",pose(track.to)}});
    }
    if(!m_enhancedSonyAvailable){const std::array<QPointF,4> identity{{{0,0},{1,0},{1,1},{0,1}}};for(const auto& track:plan)if(track.from.perspective!=identity||track.to.perspective!=identity)return {};}
    if(tracks.isEmpty())return {};return suffix+" SCENE "+QString::fromLatin1(QJsonDocument(tracks).toJson(QJsonDocument::Compact).toHex());
}

QString SwitcherEngine::dmeBackgroundProducer(int source,const QString& image) const {
    if(source==-3)return staticDmeImageProducer(image);
    if(source==-1)return "#000000";
    int me=sourceMe(source,m_activeMe);if(me>=0)return QString("route://%1 RENDERED").arg(meProgramChannel(me));
    auto src=m_config->sourceById(source);return src&&src->isAssigned()&&sourceReady(source)?QString("route://%1 RENDERED").arg(src->type==SourceType::SuperSource?superSourceChannel(source,m_activeMe):src->casparChannel):QString();
}
void SwitcherEngine::refreshDmeBackgroundPreview(const QString& effect) {
    auto update=[&](Transition& t){const QString name=t.type==TransitionType::Move?"move":t.type==TransitionType::Cube?"cube":t.type==TransitionType::Zoom?"zoom":t.type==TransitionType::PageCurl?"page_curl":t.type==TransitionType::PageRoll?"page_roll":t.type==TransitionType::SonyDme?QString("sony_%1").arg(t.sonyDmeCode):"";
        if(name.isEmpty()||(name!=effect&&(effect!="global"||m_config->dmeBackgroundCustom(name))))return;
        const int source=m_config->dmeBackground(name);const QString image=m_config->dmeBackgroundImage(name);
        t.dmeBackground=source;t.dmeBackgroundImage=image;
        send(QString("CALL %1-101 \"BACKGROUND %2\"").arg(activePreviewChannel()).arg(QString::fromLatin1(dmeBackgroundProducer(source,image).toUtf8().toHex())));
    };
    if(m_manual.active&&m_manual.preview)update(m_manual.transition);
    else if(m_previewTakeRunning&&m_takeActive)update(m_previewStyleTransition);
}
bool SwitcherEngine::setDmeBackground(const QString& effect,int source) {
    const int code=effect.section('_',1).toInt();
    const bool preset=m_primitiveSonyAvailable&&effect==QString("sony_%1").arg(code)&&supportedSonyDmes(true,m_spatialSonyAvailable,m_planarSonyAvailable,m_mirrorSonyAvailable,m_frameSonyAvailable,m_edgePageSonyAvailable).contains(code);
    if(source==11||source==23||!m_connected||!m_pageDmeAvailable||(!preset&&!QStringList{"global","move","cube","zoom","page_curl","page_roll"}.contains(effect))||(source!=-1&&dmeBackgroundProducer(source).isEmpty())||sourceWouldFeedback(source,m_activeMe))return false;
    const auto previous=m_config->toJson();m_config->setDmeBackground(effect,source);
    if(!m_config->save()){m_config->fromJson(previous);return false;}
    refreshDmeBackgroundPreview(effect);emit layersChanged();return true;
}
bool SwitcherEngine::setDmeBackgroundScope(const QString& effect,bool custom,bool copyGlobal) {
    const int code=effect.section('_',1).toInt();
    const bool preset=m_primitiveSonyAvailable&&effect==QString("sony_%1").arg(code)&&supportedSonyDmes(true,m_spatialSonyAvailable,m_planarSonyAvailable,m_mirrorSonyAvailable,m_frameSonyAvailable,m_edgePageSonyAvailable).contains(code);
    if(!m_connected||!m_pageDmeAvailable||(!preset&&!QStringList{"move","cube","zoom","page_curl","page_roll"}.contains(effect)))return false;
    const auto previous=m_config->toJson();m_config->setDmeBackgroundScope(effect,custom,copyGlobal);
    const int source=m_config->dmeBackground(effect);const QString image=m_config->dmeBackgroundImage(effect);
    if((source==-3&&!m_staticDmeAvailable)||dmeBackgroundProducer(source,image).isEmpty()||sourceWouldFeedback(source,m_activeMe)||!m_config->save()){m_config->fromJson(previous);return false;}
    refreshDmeBackgroundPreview(effect);emit layersChanged();return true;
}

bool SwitcherEngine::setSuperMixGains(int a,int b){
    if(!m_broadcastMixAvailable||a<0||a>100||b<0||b>100)return false;
    const int oldA=m_config->superMixGainA(),oldB=m_config->superMixGainB();m_config->setSuperMixGains(a,b);
    if(!m_config->save()){m_config->setSuperMixGains(oldA,oldB);return false;}
    auto update=[&](Transition& t){if(t.type!=TransitionType::SuperMix)return;t.videoGainA=a;t.videoGainB=b;send(QString("CALL %1-101 \"A_GAIN %2 B_GAIN %3\"").arg(activePreviewChannel()).arg(a/100.,0,'f',4).arg(b/100.,0,'f',4));};
    if(m_manual.active&&m_manual.preview)update(m_manual.transition);else if(m_previewTakeRunning&&m_takeActive)update(m_previewStyleTransition);
    emit layersChanged();return true;
}

bool SwitcherEngine::setDipColour(const QString& requested){
    const auto colour=opaqueMatteColor(requested);if(colour.isEmpty())return false;
    const auto old=m_config->dipColor();m_config->setDipColor(colour);
    if(!m_config->save()){m_config->setDipColor(old);return false;}
    if(m_manual.active&&m_manual.preview&&m_manual.transition.type==TransitionType::Dip){m_manual.transition.dipColor=colour;send(QString("PLAY %1-100 %2").arg(activePreviewChannel()).arg(colour));}
    else if(m_previewTakeRunning&&m_takeActive&&m_previewStyleTransition.type==TransitionType::Dip){m_previewStyleTransition.dipColor=colour;send(QString("PLAY %1-100 %2").arg(activePreviewChannel()).arg(colour));}
    emit layersChanged();return true;
}

bool SwitcherEngine::setDustMix(int ratio,int size,int flash){
    if(!m_dustMixAvailable||ratio<0||ratio>100||size<1||size>100||flash<0||flash>100)return false;
    const auto old=m_config->toJson();m_config->setDustMix(ratio,size,flash);
    if(!m_config->save()){m_config->fromJson(old);return false;}
    auto update=[&](Transition& t){if(t.type!=TransitionType::DustMix)return;t.dustRatio=ratio;t.dustSize=size;t.dustFlash=flash;send(QString("CALL %1-101 \"DUST_RATIO %2 H_SIZE %3 V_SIZE %3 FLASH_RATE %4\"").arg(activePreviewChannel()).arg(ratio/100.,0,'f',4).arg(size/100.,0,'f',4).arg(flash));};
    if(m_manual.active&&m_manual.preview)update(m_manual.transition);else if(m_previewTakeRunning&&m_takeActive)update(m_previewStyleTransition);
    emit layersChanged();return true;
}

bool SwitcherEngine::setWipeBorderProfile(int side,int inner,int outer){if(!m_dustMixAvailable||side<-1||side>1||inner<-1||inner>100||outer<-1||outer>100)return false;const auto old=m_config->toJson();m_config->setWipeBorderProfile(side,inner,outer);if(!m_config->save()){m_config->fromJson(old);return false;}emit layersChanged();return true;}
