#include "SwitcherEngine.h"
#include "config/Configuration.h"
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

bool SwitcherEngine::hasManualTransitions() const
{
    if (m_manual.active) return true;
    for (int me = 0; me < kMeCount; ++me)
        if (me != m_activeMe && m_manualBanks[me].active) return true;
    return false;
}

// The incoming picture occupies private layers above the outgoing background.
// Position changes never restart producers or launch a timed transition.
bool SwitcherEngine::setManualPosition(int position, const Transition& requested)
{
    Transition transition = requested;
    if (transition.type == TransitionType::Dip) {
        transition.dipColor = opaqueMatteColor(transition.dipColor.isEmpty() ? m_config->dipColor() : transition.dipColor);
        if (transition.dipColor.isEmpty()) return false;
    }
    if (position < 0 || position > 4095 || !m_connected) return false;
    if (m_manual.active) {
        // Once an endpoint has been received, complete it before accepting the
        // return stroke. Both transports keep only the latest intermediate value.
        if (!m_manual.endpoint) {
            m_manual.position = position;
            m_manual.endpoint = position == 0 || position == 4095;
        }
        return true;
    }
    if (position == 0) return true;
    if (isBusy() || !nextTransitionWouldChange()) return false;
    if (!transition.alternateMix() && transition.type != TransitionType::Mix && transition.type != TransitionType::Smil
        && transition.type != TransitionType::Wipe && transition.type != TransitionType::Move
        && transition.type != TransitionType::Cube && transition.type != TransitionType::Zoom
        && transition.type != TransitionType::Push && transition.type != TransitionType::Slide
        && transition.type != TransitionType::PageCurl && transition.type != TransitionType::PageRoll && transition.type != TransitionType::SonyDme && transition.type != TransitionType::Nam && transition.type != TransitionType::SuperMix) {
        emit error(tr("This transition does not support manual operation yet"));
        return false;
    }
    transition.videoGainA=m_config->superMixGainA();transition.videoGainB=m_config->superMixGainB();
    if(transition.alternateMix()&&!m_nextBackground)return false;
    for (int key = 0; key < kKeyerCount; ++key) {
        if (m_nextKey[key] && transition.type != TransitionType::Mix) {
            emit error(tr("Manual key transitions currently require MIX"));
            return false;
        }
        if (m_nextKey[key] && !m_keyOn[key] && !keyProducerReady(m_keySource[key])) return false;
    }
    if((transition.type==TransitionType::Move||transition.type==TransitionType::Cube||transition.type==TransitionType::Zoom||transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll||transition.type==TransitionType::SonyDme||transition.type==TransitionType::Nam||transition.type==TransitionType::SuperMix)&&nativeDmeSuffix(transition,true).isEmpty())return false;
    if(transition.type==TransitionType::Smil&&transition.smilType=="sonyWipe"&&!nativeSonyCode(transition))return false;
    if(transition.type==TransitionType::Move||transition.type==TransitionType::Cube||transition.type==TransitionType::Zoom||transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll||transition.type==TransitionType::SonyDme){const QString effect=transition.type==TransitionType::Move?"move":transition.type==TransitionType::Cube?"cube":transition.type==TransitionType::Zoom?"zoom":transition.type==TransitionType::SonyDme?QString("sony_%1").arg(transition.sonyDmeCode):transition.type==TransitionType::PageCurl?"page_curl":"page_roll";if(transition.dmeBackground==-2){transition.dmeBackground=m_config->dmeBackground(effect);transition.dmeBackgroundImage=m_config->dmeBackgroundImage(effect);}if((transition.dmeBackground==-3&&!m_staticDmeAvailable)||dmeBackgroundProducer(transition.dmeBackground,transition.dmeBackgroundImage).isEmpty()||sourceWouldFeedback(transition.dmeBackground,m_activeMe))return false;}
    m_manual.transition = transition;
    if (transition.type == TransitionType::Wipe) {
        const bool vertical = transition.direction == TransitionDirection::FromTop
            || transition.direction == TransitionDirection::FromBottom;
        m_manual.transition.type = TransitionType::Smil;
        m_manual.transition.smilType = QStringLiteral("barWipe");
        m_manual.transition.smilSubtype = vertical ? QStringLiteral("topToBottom") : QStringLiteral("leftToRight");
        m_manual.transition.reverse = transition.direction == TransitionDirection::FromRight
            || transition.direction == TransitionDirection::FromBottom;
    }
    const bool shaped = m_manual.transition.type == TransitionType::Smil;
    const bool dme=transition.type==TransitionType::Move||transition.type==TransitionType::Cube||transition.type==TransitionType::Zoom||transition.type==TransitionType::Push||transition.type==TransitionType::Slide||transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll||transition.type==TransitionType::SonyDme||transition.type==TransitionType::Nam||transition.type==TransitionType::SuperMix;
    if((transition.type==TransitionType::PageCurl||transition.type==TransitionType::PageRoll)&&!m_pageDmeAvailable){emit error(tr("Page DME requires casparMIX 0.6.0 or newer"));return false;}
    if(dme&&!m_nativeDmeAvailable){emit error(tr("Native DME requires casparMIX 0.5.0 or newer"));return false;}
    m_manual.native = dme || (shaped && nativeSonyCode(m_manual.transition) != 0);
    if(dme&&!m_nextBackground)return false;
    if (shaped && !m_manual.native && resolveWipeMattePath().isEmpty()) return false;
    m_manual.preview = m_transitionPreview;
    m_manual.background = m_nextBackground;
    const int channel = m_manual.preview ? activePreviewChannel() : activeProgramChannel();
    const int offset = m_manual.preview ? 100 : 0;
    QStringList commands;
    if (m_manual.preview) {
        commands = previewCleanupCommands();
        for(int layer:{100,101,111,112,113,114,115,120,121})
            commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(channel).arg(layer));
        commands.append(QStringLiteral("PLAY %1-100 #000000").arg(channel));
        commands.append(QStringLiteral("PLAY %1-101 #000000").arg(channel));
        if (m_programSource >= 0)
            commands.append(QStringLiteral("PLAY %1-101 route://%2-1").arg(channel).arg(activeProgramChannel()));
        for (int key = 0; key < kKeyerCount; ++key) if (m_keyOn[key])
            commands.append(QStringLiteral("PLAY %1-%2 route://%3-%4")
                .arg(channel).arg(111 + key).arg(activeProgramChannel()).arg(kKeyLayer0 + key));
        if (m_activeMe == 0) for (int slot = 0; slot < dskCount(); ++slot)
            if (isDskOn(slot) || isDskPreview(slot))
                commands.append(QStringLiteral("PLAY %1-%2 route://%1-%3").arg(channel).arg(120+slot).arg(kDskLayer+slot));
    }
    if (m_manual.native) {
        if (m_manual.background) {
            playCuedIfNeeded(m_previewSource, &commands);
            QString route = backgroundRoute(channel, m_previewSource, Transition::cut());
            if(dme)route=QString("PLAY %1-1 route://%2 RENDERED").arg(channel).arg(sourceChannel(m_previewSource));
            route.replace(QStringLiteral("%1-1 ").arg(channel), QStringLiteral("%1-%2 ").arg(channel).arg(offset+1));
            commands.append(route + (dme?nativeDmeSuffix(m_manual.transition,true):nativeWipeSuffix(m_manual.transition, true)));
        }
        m_manual.keys = {};
        m_manual.keyBefore = {};
        m_manual.revision = m_wipeRevision;
        m_manual.active = true;
        m_manual.ready = false;
        m_manual.endpoint = position == 4095;
        m_manual.position = position;
        m_manual.sent = -1;
        m_pendingWipeFlip = false;
        beginTake(m_manual.transition);
        m_manualTimer->start();
        beginPending(PendingKind::ManualStart, commands, m_previewSource, m_programSource);
        return m_manual.active;
    }
    if (transition.type == TransitionType::Dip) {
        commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(channel).arg(offset));
        commands.append(QStringLiteral("PLAY %1-%2 %3").arg(channel).arg(offset).arg(transition.dipColor));
    }
    commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+2));
    commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+3));
    if (m_manual.background) {
        const int incoming = sourceChannel(m_previewSource);
        if (incoming < 0) return false;
        playCuedIfNeeded(m_previewSource, &commands);
        commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(channel).arg(offset+(shaped ? 7 : 3)));
        commands.append(opacityCommand(channel, offset+(shaped ? 7 : 3), 0.0, 0));
        commands.append(layerVolumeCommand(channel, offset+(shaped ? 7 : 3), 0.0, 0));
        QString route = backgroundRoute(channel, m_previewSource, Transition::cut());
        route.replace(QStringLiteral("%1-1 ").arg(channel), QStringLiteral("%1-%2 ").arg(channel).arg(offset+(shaped ? 7 : 3)));
        commands.append(route);
        if (shaped) {
            QUrl url(wipeMatteUrl(m_manual.transition, QStringLiteral("packed")));
            QUrlQuery query(url);
            query.removeAllQueryItems(QStringLiteral("autorun"));
            query.addQueryItem(QStringLiteral("autorun"), QStringLiteral("0"));
            query.addQueryItem(QStringLiteral("progress"), QStringLiteral("0"));
            query.addQueryItem(QStringLiteral("width"), QStringLiteral("1920"));
            query.addQueryItem(QStringLiteral("height"), QStringLiteral("540"));
            url.setQuery(query);
            // Same-channel routing delivers the same CEF frame to both views.
            commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(channel).arg(offset+90));
            commands.append(opacityCommand(channel, offset+90, 0.0, 0));
            commands.append(QStringLiteral("PLAY %1-%2 [HTML] \"%3\"")
                .arg(channel).arg(offset+90).arg(url.toString(QUrl::FullyEncoded)));
            commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(channel).arg(offset+6));
            commands.append(QStringLiteral("MIXER %1-%2 KEYER 1").arg(channel).arg(offset+6));
            commands.append(QStringLiteral("MIXER %1-%2 FILL 0 0 2 1").arg(channel).arg(offset+6));
            commands.append(QStringLiteral("PLAY %1-%2 #000000").arg(channel).arg(offset+6));
            commands.append(QStringLiteral("PLAY %1-%2 route://%1-%3 BUFFER 1 MIX 1")
                .arg(channel).arg(offset+6).arg(offset+90));
            // A solid background shows only through the gap between the keys.
            commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(channel).arg(offset));
            commands.append(QStringLiteral("PLAY %1-%2 %3").arg(channel).arg(offset)
                .arg(m_config->wipeBorderColor()));
            commands.append(QStringLiteral("MIXER %1-%2 CLEAR").arg(channel).arg(offset+4));
            commands.append(QStringLiteral("MIXER %1-%2 KEYER 1").arg(channel).arg(offset+4));
            commands.append(QStringLiteral("MIXER %1-%2 FILL -1 0 2 1").arg(channel).arg(offset+4));
            commands.append(QStringLiteral("PLAY %1-%2 #ffffff").arg(channel).arg(offset+4));
            commands.append(QStringLiteral("PLAY %1-%2 route://%1-%3 BUFFER 1 MIX 1")
                .arg(channel).arg(offset+4).arg(offset+90));
            commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+kWipeBorderLayer));
            // Keep the old background visible throughout graph preparation.
            commands.append(QStringLiteral("SWAP %1-%2 %1-%3 TRANSFORMS")
                .arg(channel).arg(offset+1).arg(offset+5));
        }
    }
    for (int key = 0; key < kKeyerCount; ++key) {
        m_manual.keys[key] = m_nextKey[key];
        m_manual.keyBefore[key] = m_keyOn[key];
        if (m_manual.keys[key] && !m_keyOn[key]) {
            const int layer = offset+kKeyLayer0+key;
            commands.append(opacityCommand(channel, layer, 0.0, 0));
            commands.append(layerVolumeCommand(channel, layer, 0.0, 0));
            commands.append(QStringLiteral("PLAY %1-%2 route://%3-1")
                .arg(channel).arg(layer).arg(sourceChannel(m_keySource[key])));
            const auto* source = m_config->sourceById(m_keySource[key]);
            commands.append(mixerFillCommand(channel, layer, *source));
            commands+=m_config->keyProcessing(m_activeMe,key).commands(channel,layer,m_nativeKeyAvailable);
        }
    }
    m_manual.revision = m_wipeRevision;
    m_manual.active = true;
    m_manual.ready = false;
    m_manual.endpoint = position == 4095;
    m_manual.position = position;
    m_manual.sent = -1;
    m_pendingWipeFlip = false;
    beginTake(m_manual.transition);
    m_manualTimer->start();
    beginPending(PendingKind::ManualStart, commands, m_previewSource, m_programSource);
    return m_manual.active;
}

void SwitcherEngine::flushManualPosition()
{
    if(!m_manual.active && m_previewTakeRunning && nativeSonyCode(m_previewStyleTransition)) {
        if(m_pendingKind!=PendingKind::None || m_previewStyleRevision==m_wipeRevision)return;
        Transition current=m_previewStyleTransition;
        current.edge=m_config->wipeEdgeMode();
        current.edgeAmount=m_config->wipeEdgeAmount();
        current.borderAmount=m_config->wipeBorderAmount();
        current.aspectW=m_config->wipeAspectW();current.aspectH=m_config->wipeAspectH();
        current.multi=m_config->wipeMulti();current.posX=m_config->wipePosX();current.posY=m_config->wipePosY();current.vertices=m_config->wipeVertices();current.rounding=m_config->wipeRounding();current.tileSize=m_config->wipeTileSize();
        const QString options=nativeWipeOptions(current)+QStringLiteral(" BORDERCOLOR %1").arg(m_config->wipeBorderColor());
        m_previewStyleRevision=m_wipeRevision;
        // Do not send PROGRESS here: a modifier edit must preserve the timed clock.
        send(QStringLiteral("CALL %1-101 \"%2\"").arg(activePreviewChannel()).arg(options));
        return;
    }
    if(!m_manual.active && !m_previewTakeRunning){m_manualTimer->stop();return;}
    if (m_requestedMe >= 0 || !m_manual.active || !m_manual.ready || m_pendingKind != PendingKind::None) return;
    const bool liveStyle = m_manual.preview && m_manual.revision != m_wipeRevision
        && m_manual.transition.type == TransitionType::Smil;
    if (m_manual.sent == m_manual.position && !liveStyle) return;
    m_manual.revision = m_wipeRevision;
    const double t = double(m_manual.position)/4095.0;
    const int channel = m_manual.preview ? activePreviewChannel() : activeProgramChannel();
    const int offset = m_manual.preview ? 100 : 0;
    QStringList commands;
    if (m_manual.native) {
        QString options;
        if (liveStyle) {
            Transition current = m_manual.transition;
            current.edge = m_config->wipeEdgeMode();
            current.edgeAmount = m_config->wipeEdgeAmount();
            current.borderAmount = m_config->wipeBorderAmount();
            current.aspectW = m_config->wipeAspectW();
            current.aspectH = m_config->wipeAspectH();
            current.multi = m_config->wipeMulti();
            current.posX = m_config->wipePosX();
            current.posY = m_config->wipePosY();current.vertices=m_config->wipeVertices();current.rounding=m_config->wipeRounding();current.tileSize=m_config->wipeTileSize();
            options = nativeWipeOptions(current) + QStringLiteral(" BORDERCOLOR %1 ")
                .arg(m_config->wipeBorderColor());
        }
        commands.append(QStringLiteral("CALL %1-%2 \"%3PROGRESS %4\"")
            .arg(channel).arg(offset+1).arg(options).arg(t,0,'f',6));
        m_manual.sent = m_manual.position;
        beginPending(PendingKind::ManualPosition, commands, m_previewSource, m_programSource);
        return;
    }
    if (m_manual.background) {
        if (m_manual.transition.type == TransitionType::Smil) {
            QString call = QStringLiteral("setProgress(%1)").arg(t,0,'f',6);
            if (liveStyle) {
                call = QStringLiteral("update({edge:'%1',amount:%2,border:%3,shadow:%4,color:'%5',aw:%6,ah:%7,multi:%8,px:%9,py:%10,progress:%11})")
                    .arg(wipeEdgeModeToString(m_config->wipeEdgeMode())).arg(m_config->wipeEdgeAmount())
                    .arg(m_config->wipeBorderAmount()).arg(m_config->wipeShadowAmount()).arg(m_config->wipeBorderColor())
                    .arg(m_config->wipeAspectW()).arg(m_config->wipeAspectH()).arg(m_config->wipeMulti())
                    .arg(m_config->wipePosX()).arg(m_config->wipePosY()).arg(t,0,'f',6);
            }
            commands.append(QStringLiteral("CALL %1-%2 \"%3\"").arg(channel).arg(offset+90).arg(call));
            if (liveStyle) commands.append(QStringLiteral("PLAY %1-%2 %3")
                .arg(channel).arg(offset).arg(m_config->wipeBorderColor()));
            commands.append(opacityCommand(channel, offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3), 1.0, 0));
        } else if(m_manual.transition.alternateMix()) {
            const auto kind=m_manual.transition.type;
            const bool dip = kind == TransitionType::VFade || kind == TransitionType::Dip;
            const double outgoing=dip?qMax(0.0,1.0-2*t):kind==TransitionType::FadeCut?1.0-t:t==0?1.0:0.0;
            const double incoming=dip?qMax(0.0,2*t-1.0):kind==TransitionType::FadeCut?t==1?1.0:0.0:t;
            commands.append(opacityCommand(channel,offset+1,outgoing,0));
            commands.append(opacityCommand(channel,offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3),incoming,0));
            commands.append(layerVolumeCommand(channel,offset+1,outgoing,0));
            commands.append(layerVolumeCommand(channel,offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3),incoming,0));
        } else commands.append(opacityCommand(channel, offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3), t, 0));
        if(!m_manual.transition.alternateMix()) {
            commands.append(layerVolumeCommand(channel, offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3), t, 0));
            commands.append(layerVolumeCommand(channel, offset+(m_manual.transition.type == TransitionType::Smil ? 5 : 1), 1.0-t, 0));
        }
    }
    for (int key = 0; key < kKeyerCount; ++key) if (m_manual.keys[key]) {
        const double amount = m_manual.keyBefore[key] ? 1.0-t : t;
        commands.append(opacityCommand(channel, offset+kKeyLayer0+key, amount, 0));
        commands.append(layerVolumeCommand(channel, offset+kKeyLayer0+key, amount, 0));
    }
    m_manual.sent = m_manual.position;
    beginPending(PendingKind::ManualPosition, commands, m_previewSource, m_programSource);
}

void SwitcherEngine::finishManualTransition()
{
    const int channel = m_manual.preview ? activePreviewChannel() : activeProgramChannel();
    const int offset = m_manual.preview ? 100 : 0;
    const bool commit = m_manual.position == 4095 && !m_manual.preview;
    QStringList commands;
    if (m_manual.native) {
        if (!m_manual.preview && m_manual.background)
            commands.append(backgroundRoute(channel, commit ? m_previewSource : m_programSource, Transition::cut()));
        if (m_manual.preview) commands += previewCleanupCommands();
        beginPending(PendingKind::ManualFinish, commands, m_previewSource, m_programSource);
        return;
    }
    if (m_manual.transition.type == TransitionType::Smil) {
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset));
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+4));
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+6));
        if (!commit) commands.append(QStringLiteral("SWAP %1-%2 %1-%3 TRANSFORMS")
            .arg(channel).arg(offset+1).arg(offset+5));
    }
    if (commit && m_manual.background) {
        // Keep the already-running incoming producer. A fresh PLAY followed by
        // CLEAR can remove the covering layer before the new route has a frame.
        // At the endpoint the incoming layer is fully opaque with unity gain.
        // Remove the wipe key before promoting that layer, then hide outgoing
        // video/audio so it cannot reappear above the new background after SWAP.
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+2));
        commands.append(opacityCommand(channel, offset+(m_manual.transition.type == TransitionType::Smil ? 5 : 1), 0.0, 0));
        commands.append(layerVolumeCommand(channel, offset+(m_manual.transition.type == TransitionType::Smil ? 5 : 1), 0.0, 0));
        commands.append(QStringLiteral("SWAP %1-%2 %1-%3 TRANSFORMS")
            .arg(channel).arg(offset+1).arg(offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3)));
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3)));
    } else {
        if (m_manual.background) {
            commands.append(layerVolumeCommand(channel, offset+1, 1.0, 0));
            commands.append(opacityCommand(channel, offset+1, 1.0, 0));
        }
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+2));
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+(m_manual.transition.type == TransitionType::Smil ? 7 : 3)));
    }
    for (int key = 0; key < kKeyerCount; ++key) if (m_manual.keys[key]) {
        const bool on = commit ? !m_manual.keyBefore[key] : m_manual.keyBefore[key];
        const int layer = offset+kKeyLayer0+key;
        if (on) {
            commands.append(opacityCommand(channel, layer, 1.0, 0));
            commands.append(layerVolumeCommand(channel, layer, 1.0, 0));
        } else commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(layer));
    }
    if (m_manual.transition.type == TransitionType::Smil) {
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+5));
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+7));
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+kWipeBorderLayer));
        commands.append(QStringLiteral("CLEAR %1-%2").arg(channel).arg(offset+90));
    }
    if (!m_manual.preview && m_manual.transition.type == TransitionType::Dip)
        commands.append(QStringLiteral("CLEAR %1-0").arg(channel));
    if (m_manual.preview) commands += previewCleanupCommands();
    beginPending(PendingKind::ManualFinish, commands, m_previewSource, m_programSource);
}
