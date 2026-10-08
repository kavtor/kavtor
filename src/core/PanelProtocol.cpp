#include "Logging.h"
#include "PanelProtocol.h"
#include "SwitcherEngine.h"
#include "config/Configuration.h"

#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QDebug>
#include <QtCore/QRegularExpression>
#include <QtCore/QSignalBlocker>

namespace {

QString oppositeEntry(const QString& direction) {
    return direction=="left"?"right":direction=="right"?"left":direction=="top"?"bottom":"top";
}

bool knownAspect(int width, int height)
{
    return width >= 1 && width <= 1000 && height >= 1 && height <= 1000;
}

bool validateWipeExtras(Configuration* config, const QJsonObject& obj, QString* error)
{
    const QStringList fields = {QStringLiteral("multi"), QStringLiteral("shadow"), QStringLiteral("border"),
        QStringLiteral("aspectW"), QStringLiteral("aspectH"), QStringLiteral("posX"), QStringLiteral("posY"),QStringLiteral("vertices"),QStringLiteral("rounding"),QStringLiteral("tileSize")};
    for (const QString& field : fields) {
        if (obj.contains(field) && (!obj.value(field).isDouble()
            || obj.value(field).toDouble() != obj.value(field).toInt(-1))) {
            *error = QStringLiteral("Wipe %1 must be an integer").arg(field);
            return false;
        }
    }
    if((obj.contains("vertices")&&(obj.value("vertices").toInt()<3||obj.value("vertices").toInt()>64))||(obj.contains("rounding")&&(obj.value("rounding").toInt()<0||obj.value("rounding").toInt()>50))) {*error="Invalid polygon vertices or corner rounding";return false;}
    if(obj.contains("tileSize")&&(obj.value("tileSize").toInt()<2||obj.value("tileSize").toInt()>50)){*error="Tile size must be 2..50 percent of raster height";return false;}
    const int multi = obj.value(QStringLiteral("multi")).toInt(config->wipeMulti());
    const int border = obj.value(QStringLiteral("border")).toInt(config->wipeBorderAmount());
    const int width = obj.value(QStringLiteral("aspectW")).toInt(config->wipeAspectW());
    const int height = obj.value(QStringLiteral("aspectH")).toInt(config->wipeAspectH());
    const int x = obj.value(QStringLiteral("posX")).toInt(config->wipePosX());
    const int y = obj.value(QStringLiteral("posY")).toInt(config->wipePosY());
    if (multi != 1 && multi != 2 && multi != 4 && multi != 9 && multi != 16) {
        *error = QStringLiteral("Wipe multi must be 1, 2, 4, 9 or 16");
    } else if (obj.contains("shadow") && (obj.value("shadow").toInt() < 0 || obj.value("shadow").toInt() > 40)) {
        *error = QStringLiteral("Wipe shadow must be 0..40");
    } else if (border < 0 || border > 40) {
        *error = QStringLiteral("Wipe border must be 0..40");
    } else if (!knownAspect(width, height)) {
        *error = QStringLiteral("Unknown wipe aspect");
    } else if (x < 0 || x > 1000 || y < 0 || y > 1000) {
        *error = QStringLiteral("Wipe position must be 0..1000");
    } else {
        return true;
    }
    return false;
}

bool applyWipeExtras(Configuration* config, const QJsonObject& obj, QString* error)
{
    // Validate the complete request before changing any live configuration.
    if (!validateWipeExtras(config, obj, error)) {
        return false;
    }
    if(obj.contains("tileSize"))config->setWipeTileSize(obj.value("tileSize").toInt());
    if(obj.contains("vertices")||obj.contains("rounding"))config->setWipeGeometry(obj.value("vertices").toInt(config->wipeVertices()),obj.value("rounding").toInt(config->wipeRounding()));
    if (obj.contains("shadow")) config->setWipeShadowAmount(obj.value("shadow").toInt());
    if (obj.contains(QStringLiteral("multi"))) {
        config->setWipeMulti(obj.value(QStringLiteral("multi")).toInt());
    }
    if (obj.contains(QStringLiteral("border"))) {
        config->setWipeBorderAmount(obj.value(QStringLiteral("border")).toInt());
    }
    if (obj.contains(QStringLiteral("aspectW")) || obj.contains(QStringLiteral("aspectH"))) {
        config->setWipeAspect(obj.value(QStringLiteral("aspectW")).toInt(config->wipeAspectW()),
                              obj.value(QStringLiteral("aspectH")).toInt(config->wipeAspectH()));
    }
    if (obj.contains(QStringLiteral("posX")) || obj.contains(QStringLiteral("posY"))) {
        config->setWipePos(obj.value(QStringLiteral("posX")).toInt(config->wipePosX()),
                           obj.value(QStringLiteral("posY")).toInt(config->wipePosY()));
    }
    return true;
}

} // namespace

PanelProtocol::PanelProtocol(SwitcherEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
{
    Q_ASSERT(m_engine);
    m_server = new QTcpServer(this);

    connect(m_server, &QTcpServer::newConnection, this, &PanelProtocol::onNewConnection);
    connect(m_engine,&SwitcherEngine::superSourceBindingChanged,this,[this](int source,const QString& box,int input){broadcast(QJsonObject{{"event","supersource_binding"},{"source",source},{"box",box},{"input",input}});});
    connect(m_engine, &SwitcherEngine::previewSourceChanged, this, &PanelProtocol::onPreviewChanged);
    connect(m_engine, &SwitcherEngine::programSourceChanged, this, &PanelProtocol::onProgramChanged);
    connect(m_engine, &SwitcherEngine::connectionStatusChanged, this, &PanelProtocol::onConnectionChanged);
    connect(m_engine, &SwitcherEngine::transitioningChanged, this, &PanelProtocol::onTransitioningChanged);
    connect(m_engine, &SwitcherEngine::dskOnChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine, &SwitcherEngine::dskPreviewChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine, &SwitcherEngine::layersChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine, &SwitcherEngine::wipeSenseChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine, &SwitcherEngine::deckSourceChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine, &SwitcherEngine::deckCueChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine->configuration(), &Configuration::configurationChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine, &SwitcherEngine::error, this, &PanelProtocol::onEngineError);
    connect(m_engine, &SwitcherEngine::renderReadinessChanged, this, &PanelProtocol::onDskChanged);
    connect(m_engine, &SwitcherEngine::keyerConfigurationCommitted, this, [this] {
        if (!m_engine->configuration()->save())
            onEngineError(QStringLiteral("Keyer settings were accepted but could not be saved"));
    });
}

PanelProtocol::~PanelProtocol()
{
    stop();
}

bool PanelProtocol::start(quint16 port)
{
    if (m_server->isListening()) {
        if (m_server->serverPort() == port) {
            return true;
        }
        // Bind before closing the working endpoint and its panel clients.
        auto* replacement = new QTcpServer(this);
        if (!replacement->listen(QHostAddress::Any, port)) {
            qWarning() << "PanelProtocol: cannot change port" << port << replacement->errorString();
            delete replacement;
            return false;
        }
        stop();
        QTcpServer* previous = m_server;
        m_server = replacement;
        connect(m_server, &QTcpServer::newConnection, this, &PanelProtocol::onNewConnection);
        previous->deleteLater();
        return true;
    }
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "PanelProtocol: cannot listen on port" << port << m_server->errorString();
        return false;
    }
    qCDebug(kavtorLog) << "PanelProtocol: listening on" << m_server->serverPort();
    return true;
}

void PanelProtocol::stop()
{
    if (m_manualOwner) m_engine->setManualPosition(0, Transition::mix(0));
    m_manualOwner = nullptr;
    for (QTcpSocket* client : m_clients) {
        client->disconnect();
        client->deleteLater();
    }
    m_clients.clear();
    m_server->close();
}

bool PanelProtocol::isListening() const
{
    return m_server->isListening();
}

quint16 PanelProtocol::port() const
{
    return m_server->serverPort();
}

void PanelProtocol::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* client = m_server->nextPendingConnection();
        client->setParent(this);
        connect(client, &QTcpSocket::disconnected, this, &PanelProtocol::onClientDisconnected);
        connect(client, &QTcpSocket::readyRead, this, &PanelProtocol::onClientReadyRead);
        m_clients.append(client);
        sendTo(client, stateObject());
        sendTo(client, tallyObject());
    }
}

void PanelProtocol::onClientDisconnected()
{
    auto* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) {
        return;
    }
    if (m_manualOwner == client) {
        m_manualOwner = nullptr;
        m_engine->setManualPosition(0, Transition::mix(0));
    }
    m_clients.removeAll(client);
    client->deleteLater();
}

void PanelProtocol::onClientReadyRead()
{
    auto* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) {
        return;
    }

    while (client->canReadLine()) {
        const QString line = QString::fromUtf8(client->readLine()).trimmed();
        if (!line.isEmpty()) {
            handleLine(client, line);
        }
    }
}

void PanelProtocol::onPreviewChanged(int)
{
    broadcast(tallyObject());
    broadcast(stateObject());
}

void PanelProtocol::onProgramChanged(int)
{
    broadcast(tallyObject());
    broadcast(stateObject());
}

void PanelProtocol::onConnectionChanged(bool)
{
    broadcast(stateObject());
}

void PanelProtocol::onTransitioningChanged(bool)
{
    // Frozen M/Es retain ownership; an idle engine must not lock out a new panel.
    if (!m_engine->hasManualTransitions()) m_manualOwner = nullptr;
    broadcast(tallyObject());
    broadcast(stateObject());
}

void PanelProtocol::onDskChanged()
{
    broadcast(stateObject());
}

void PanelProtocol::onEngineError(const QString& message)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("event"), QStringLiteral("error"));
    obj.insert(QStringLiteral("message"), message);
    broadcast(obj);
}

void PanelProtocol::handleLine(QTcpSocket* client, const QString& line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject err;
        err.insert(QStringLiteral("event"), QStringLiteral("error"));
        err.insert(QStringLiteral("message"), QStringLiteral("Invalid JSON"));
        sendTo(client, err);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString cmd = obj.value(QStringLiteral("cmd")).toString().toLower();

    QJsonObject ack;
    ack.insert(QStringLiteral("event"), QStringLiteral("ack"));
    ack.insert(QStringLiteral("cmd"), cmd);

    const auto saveConfiguration = [&]() {
        if (m_engine->configuration()->save()) {
            return true;
        }
        QJsonObject err;
        err.insert(QStringLiteral("event"), QStringLiteral("error"));
        err.insert(QStringLiteral("cmd"), cmd);
        err.insert(QStringLiteral("message"), QStringLiteral("Configuration changed in memory but could not be saved"));
        sendTo(client, err);
        return false;
    };

    const auto rejectKeyerRequest = [&](const QString& reason) {
        QJsonObject error;
        error.insert(QStringLiteral("event"), QStringLiteral("error"));
        error.insert(QStringLiteral("cmd"), cmd);
        error.insert(QStringLiteral("message"), reason);
        sendTo(client, error);
    };
    const auto validIndex = [&](const QString& name, int limit, bool required) {
        if (!obj.contains(name)) return !required;
        const QJsonValue value = obj.value(name);
        const double number = value.toDouble(-1);
        return value.isDouble() && number >= 0 && number < limit && number == static_cast<int>(number);
    };
    const bool upstream = cmd == QLatin1String("key_on") || cmd == QLatin1String("key_source");
    const bool downstream = cmd == QLatin1String("dsk") || cmd == QLatin1String("dsk_source")
        || cmd == QLatin1String("dsk_preview") || cmd == QLatin1String("dskpvw");
    if (upstream || downstream || cmd == QLatin1String("next")) {
        const int limit = downstream ? m_engine->dskCount() : m_engine->keyerCount();
        if (!validIndex(QStringLiteral("slot"), limit, false)
            || (obj.contains(QStringLiteral("on")) && !obj.value(QStringLiteral("on")).isBool())) {
            rejectKeyerRequest(QStringLiteral("Invalid keyer slot or on state"));
            return;
        }
        if ((cmd == QLatin1String("key_source") || cmd == QLatin1String("dsk_source"))
            && !validIndex(QStringLiteral("source"), m_engine->configuration()->maxSources(), true)) {
            rejectKeyerRequest(QStringLiteral("Invalid keyer source"));
            return;
        }
        for (const QString& field : {QStringLiteral("mix"), QStringLiteral("reset")}) {
            if (obj.contains(field) && !obj.value(field).isBool()) {
                rejectKeyerRequest(QStringLiteral("%1 must be boolean").arg(field));
                return;
            }
        }
        if (cmd == QLatin1String("next") && !obj.value(QStringLiteral("reset")).toBool(false)
            && obj.value(QStringLiteral("target")).toString() != QLatin1String("key")
            && obj.value(QStringLiteral("target")).toString() != QLatin1String("background")) {
            rejectKeyerRequest(QStringLiteral("NEXT target must be background or key"));
            return;
        }
        if (cmd == QLatin1String("key_source") || cmd == QLatin1String("dsk_source")) {
            const auto* source = m_engine->configuration()->sourceById(obj.value(QStringLiteral("source")).toInt());
            if (!source || !source->enabled || !source->isAssigned()
                || source->producerCommand(m_engine->configuration()->videoWidth(), m_engine->configuration()->videoHeight()).isEmpty()) {
                rejectKeyerRequest(QStringLiteral("Keyer source is not assigned and enabled"));
                return;
            }
        }
        if (m_engine->isBusy()) {
            rejectKeyerRequest(QStringLiteral("Mixer is busy; retry after the current operation"));
            return;
        }
        if (cmd == QLatin1String("dsk") && obj.value(QStringLiteral("mix")).toBool(false)) {
            if (!validIndex(QStringLiteral("frames"), 1001, true) || obj.value(QStringLiteral("frames")).toInt() < 1) {
                rejectKeyerRequest(QStringLiteral("DSK mix duration must be 1..1000 frames"));
                return;
            }
        }
    }

    if (cmd == QLatin1String("key_processing")) {
        const bool dsk=obj.value("target").toString()=="dsk";
        if((obj.value("target").toString()!="key"&&!dsk)
            ||!validIndex("slot",dsk?2:4,true)||!obj.value("settings").isObject()) {
            rejectKeyerRequest(QStringLiteral("Invalid key processing target, slot or settings"));return;
        }
        const int slot=obj.value("slot").toInt();
        auto processing=m_engine->configuration()->keyProcessing(m_engine->activeMe(),slot,dsk);
        QString error;
        if(!processing.update(obj.value("settings").toObject(),&error)) {rejectKeyerRequest(error);return;}
        if(!m_engine->setKeyProcessing(slot,dsk,processing)) {rejectKeyerRequest(QStringLiteral("Key processing is unavailable while the mixer is busy"));return;}
        sendTo(client,ack);return;
    }
    if (cmd == QLatin1String("manual")) {
        if (!validIndex(QStringLiteral("position"), 4096, true)) {
            rejectKeyerRequest(QStringLiteral("Manual position must be an integer 0..4095"));
            return;
        }
        const QString kind = obj.value(QStringLiteral("type")).toString();
        Transition transition = Transition::mix(0);
        if(obj.contains("mode")&&!obj.value("mode").isString()) {rejectKeyerRequest(QStringLiteral("MIX mode must be a string"));return;}
        if(kind=="mix"&&!Transition::namedMix(obj.value("mode").toString("mix"),0,&transition)) {
            rejectKeyerRequest(QStringLiteral("Unknown MIX mode"));return;
        }
        if (kind == QLatin1String("wipe")) {
            WipePattern pattern;
            bool inherentReverse = false;
            const QString direction = obj.value(QStringLiteral("dir")).toString(QStringLiteral("fwd"));
            const bool sony=obj.contains("sony");
            const QString codeField=sony?QStringLiteral("sony"):QStringLiteral("smpte");
            if ((sony&&obj.contains("smpte"))||!validIndex(codeField, 1000, true)
                || !(sony?(supportedSonyWipes(m_engine->expandedSonyAvailable(),m_engine->enhancedSonyAvailable(),m_engine->rotarySonyAvailable(),m_engine->mosaicSonyAvailable(),m_engine->compoundSonyAvailable()).contains(obj.value(codeField).toInt())&&lookupWipeBySony(obj.value(codeField).toInt(), &pattern, &inherentReverse)):lookupWipeBySmpte(obj.value(codeField).toInt(), &pattern, &inherentReverse))
                || (direction != QLatin1String("fwd") && direction != QLatin1String("rev"))
                || (obj.contains(QStringLiteral("amount")) && !validIndex(QStringLiteral("amount"),41,true))) {
                rejectKeyerRequest(QStringLiteral("Invalid manual wipe pattern, direction or softness"));
                return;
            }
            transition = Transition::fromWipePattern(pattern, 0, inherentReverse != (direction == QLatin1String("rev")));
            const auto* config = m_engine->configuration();
            transition.edge = obj.value(QStringLiteral("amount")).toInt() > 0 ? WipeEdgeMode::Soft : WipeEdgeMode::Hard;
            transition.edgeAmount = obj.value(QStringLiteral("amount")).toInt();
            transition.borderAmount = config->wipeBorderAmount();
            transition.borderColor = config->wipeBorderColor();
            transition.shadowAmount = config->wipeShadowAmount();
            transition.multi = config->wipeMulti();
            transition.aspectW = config->wipeAspectW();
            transition.aspectH = config->wipeAspectH();
            transition.posX = config->wipePosX();
            transition.posY = config->wipePosY();transition.vertices=config->wipeVertices();transition.rounding=config->wipeRounding();transition.tileSize=config->wipeTileSize();
        } else if(kind=="dme") {
            QString effect=obj.value("effect").toString();
            QString sonyDirection;
            if(obj.contains("sony")) {
                if(obj.contains("effect")||obj.contains("direction")||!validIndex("sony",4000,true)||!lookupDmeBySony(obj.value("sony").toInt(),&effect,&sonyDirection,m_engine->primitiveSonyAvailable(),m_engine->spatialSonyAvailable(),m_engine->planarSonyAvailable(),m_engine->mirrorSonyAvailable(),m_engine->frameSonyAvailable())){rejectKeyerRequest("Unknown or invalid Sony DME");return;}
                if(effect!="sony"&&obj.value("reverse").toBool())sonyDirection=oppositeEntry(sonyDirection);
            }
            if(effect!="move"&&effect!="cube"&&effect!="zoom"&&effect!="page_curl"&&effect!="page_roll"&&effect!="push"&&effect!="slide"&&effect!="sony"){rejectKeyerRequest("Manual DME effect not available");return;}
            transition.type=effect=="move"?TransitionType::Move:effect=="cube"?TransitionType::Cube:effect=="sony"?TransitionType::SonyDme:effect=="push"?TransitionType::Push:effect=="slide"?TransitionType::Slide:effect=="page_curl"?TransitionType::PageCurl:effect=="page_roll"?TransitionType::PageRoll:TransitionType::Zoom;transition.reverse=obj.value("reverse").toBool(false);transition.sonyDmeCode=obj.value("sony").toInt();
            if(effect=="push"||effect=="slide"){auto direction=sonyDirection.isEmpty()?obj.value("direction").toString():sonyDirection;if(direction!="left"&&direction!="right"&&direction!="top"&&direction!="bottom"){rejectKeyerRequest("Invalid manual DME direction");return;}transition.direction=direction=="left"?TransitionDirection::FromLeft:direction=="right"?TransitionDirection::FromRight:direction=="top"?TransitionDirection::FromTop:TransitionDirection::FromBottom;}
        } else if (kind != QLatin1String("mix")) {
            rejectKeyerRequest(QStringLiteral("Manual type must be mix or wipe"));
            return;
        }
        if (!m_engine->hasManualTransitions()) m_manualOwner = nullptr;
        if (m_manualOwner && m_manualOwner != client) {
            rejectKeyerRequest(QStringLiteral("Manual transition is owned by another panel"));
            return;
        }
        if (!m_engine->setManualPosition(obj.value(QStringLiteral("position")).toInt(), transition)) {
            rejectKeyerRequest(QStringLiteral("Manual transition is not available for this NEXT TRANSITION"));
            return;
        }
        if (m_engine->isManualTransition()) m_manualOwner = client;
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("trans_preview")) {
        if (!obj.value(QStringLiteral("on")).isBool()) {
            rejectKeyerRequest(QStringLiteral("TRANS PREVIEW requires boolean on"));
            return;
        }
        if (m_engine->setTransitionPreview(obj.value(QStringLiteral("on")).toBool())) sendTo(client, ack);
        return;
    }
    if(cmd=="supersource_state"){
        QJsonArray sources;for(auto& source:m_engine->configuration()->sources())if(source.enabled&&source.type==SourceType::SuperSource){auto layout=m_engine->configuration()->superSource(source.argument);if(!layout)continue;auto bindings=m_engine->superSourceBindings(source.id);QJsonArray boxes;
            for(auto& box:layout->boxes)if(box.kind=="input")boxes.append(QJsonObject{{"id",box.id},{"name",box.name},{"button",box.keyButton},{"input",bindings.value(box.id,box.input)}});
            sources.append(QJsonObject{{"source",source.id},{"layout",layout->id},{"name",layout->name},{"prepared",m_engine->sourceReady(source.id)},{"boxes",boxes}});
        }sendTo(client,QJsonObject{{"event","supersource_state"},{"sources",sources}});return;
    }
    if(cmd=="dme_background") {
        const auto effect=obj.value("effect").toString();bool ok=false;
        const bool context=!obj.contains("me")||(obj.value("me").isDouble()&&obj.value("me").toDouble()==m_engine->activeMe());
        if(context&&obj.contains("custom")){
            if(obj.value("custom").isBool()&&(!obj.contains("copyGlobal")||obj.value("copyGlobal").isBool())&&!obj.contains("source"))ok=m_engine->setDmeBackgroundScope(effect,obj.value("custom").toBool(),obj.value("copyGlobal").toBool(false));
        }else if(context){const auto value=obj.value("source");ok=value.isDouble()&&value.toDouble()==value.toInt(-999)&&m_engine->setDmeBackground(effect,value.toInt(-999));}
        if(!ok)sendTo(client,QJsonObject{{"event","error"},{"cmd",cmd},{"message","DME background unavailable or feedback route"}});else sendTo(client,ack);return;
    }
    if(cmd=="supersource_input") {
        // supersource_input_context: pin the request to its explicitly delegated bus.
        int target=obj.value("source").toInt(-1);
        if(obj.contains("bus")) {
            auto bus=obj.value("bus").toString();int selected=bus=="preview"?m_engine->previewSource():bus=="program"?m_engine->programSource():-1;
            if(target!=selected||selected<0||(obj.contains("me")&&obj.value("me").toInt(-1)!=m_engine->activeMe())){sendTo(client,QJsonObject{{"event","error"},{"cmd",cmd},{"message","SuperSource bus changed; select its current window"}});return;}
        }
        if(obj.contains("button")){auto source=m_engine->configuration()->sourceById(target);auto layout=source?m_engine->configuration()->superSource(source->argument):nullptr;bool match=false;if(layout)for(auto& box:layout->boxes)if(box.id==obj.value("box").toString()&&box.keyButton==obj.value("button").toInt(-2))match=true;if(!match){sendTo(client,QJsonObject{{"event","error"},{"cmd",cmd},{"message","SuperSource window mapping changed"}});return;}}
        bool ok=m_engine->setSuperSourceInput(obj.value("source").toInt(-1),obj.value("box").toString(),obj.value("reset").toBool(false)?-2:obj.value("input").toInt(-3));
        if(!ok)sendTo(client,QJsonObject{{"event","error"},{"cmd",cmd},{"message","Invalid SuperSource binding or feedback route"}});else sendTo(client,ack);
        return;
    }
    if (cmd == QLatin1String("pvw") || cmd == QLatin1String("preview")) {
        m_engine->selectPreview(obj.value(QStringLiteral("source")).toInt(-1));
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("cut")) {
        m_engine->executeCut();
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("ftb")) {
        int frames = -1;
        if (obj.contains(QStringLiteral("frames"))) {
            frames = obj.value(QStringLiteral("frames")).toInt(-1);
            if (frames < 1 || frames > 1000) {
                QJsonObject err;
                err.insert(QStringLiteral("event"), QStringLiteral("error"));
                err.insert(QStringLiteral("message"), QStringLiteral("Rate must be 1..1000 frames"));
                sendTo(client, err);
                return;
            }
        }
        m_engine->executeFtb(frames);
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("auto") || cmd == QLatin1String("mix")) {
        Transition transition;
        if(obj.contains("mode")&&!obj.value("mode").isString()) {rejectKeyerRequest(QStringLiteral("MIX mode must be a string"));return;}
        if(!Transition::namedMix(obj.value("mode").toString("mix"),m_engine->configuration()->autoDurationFrames(),&transition)) {
            rejectKeyerRequest(QStringLiteral("Unknown MIX mode"));return;
        }
        m_engine->executeTransition(transition);
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("dme")) {
        QString effect=obj.value("effect").toString();
        QString direction=obj.value("direction").toString();
        if(obj.contains("sony")) {
            if(obj.contains("effect")||obj.contains("direction")||!validIndex("sony",4000,true)||!lookupDmeBySony(obj.value("sony").toInt(),&effect,&direction,m_engine->primitiveSonyAvailable(),m_engine->spatialSonyAvailable(),m_engine->planarSonyAvailable(),m_engine->mirrorSonyAvailable(),m_engine->frameSonyAvailable())) {rejectKeyerRequest("Unknown or invalid Sony DME");return;}
            if(effect!="sony"&&obj.value("reverse").toBool())direction=oppositeEntry(direction);
        }
        if((effect!="push"&&effect!="slide"&&effect!="move"&&effect!="cube"&&effect!="zoom"&&effect!="page_curl"&&effect!="page_roll"&&effect!="sony")||((effect=="push"||effect=="slide")&&(direction!="left"&&direction!="right"&&direction!="top"&&direction!="bottom"))) {
            rejectKeyerRequest(QStringLiteral("Invalid DME effect or entry direction"));return;
        }
        if((effect=="page_curl"||effect=="page_roll")&&!m_engine->pageDmeAvailable()){rejectKeyerRequest("Page DME requires casparMIX 0.6.0 or newer");return;}
        if((effect=="move"||effect=="cube"||effect=="zoom")&&!m_engine->nativeDmeAvailable()){rejectKeyerRequest("Native DME requires casparMIX 0.5.0 or newer");return;}
        bool keys=false;for(int key=0;key<4;++key)keys|=m_engine->nextKey(key);
        if(keys||!m_engine->nextBackground()) {rejectKeyerRequest(QStringLiteral("DME currently requires background-only NEXT TRANSITION"));return;}
        Transition transition=Transition::mix(m_engine->configuration()->autoDurationFrames());
        transition.type=effect=="sony"?TransitionType::SonyDme:effect=="push"?TransitionType::Push:effect=="slide"?TransitionType::Slide:effect=="move"?TransitionType::Move:effect=="cube"?TransitionType::Cube:effect=="page_curl"?TransitionType::PageCurl:effect=="page_roll"?TransitionType::PageRoll:TransitionType::Zoom;transition.reverse=obj.value("reverse").toBool(false);transition.sonyDmeCode=obj.value("sony").toInt();
        transition.direction=direction=="left"?TransitionDirection::FromLeft:direction=="right"?TransitionDirection::FromRight:direction=="top"?TransitionDirection::FromTop:TransitionDirection::FromBottom;
        m_engine->executeTransition(transition);sendTo(client,ack);return;
    }
    if (cmd == QLatin1String("wipe")) {
        Configuration* config = m_engine->configuration();
        QString validationError;
        WipePattern pattern;
        bool inherentReverse = false;
        QString patternId = config->wipePatternId();
        WipeDirectionMode direction = config->wipeDirectionMode();
        WipeEdgeMode edge = config->wipeEdgeMode();
        int amount = config->wipeEdgeAmount();
        QString color = config->wipeBorderColor();
        if(obj.contains("sony")) {
            if(obj.contains("smpte")||!validIndex("sony",1000,true)||!supportedSonyWipes(m_engine->expandedSonyAvailable(),m_engine->enhancedSonyAvailable(),m_engine->rotarySonyAvailable(),m_engine->mosaicSonyAvailable(),m_engine->compoundSonyAvailable()).contains(obj.value("sony").toInt())||!lookupWipeBySony(obj.value("sony").toInt(),&pattern,&inherentReverse))validationError=QStringLiteral("Unknown or invalid Sony wipe");
            else {patternId=pattern.id;direction=WipeDirectionMode::Forward;}
        } else if (obj.contains(QStringLiteral("smpte"))) {
            const int code = obj.value(QStringLiteral("smpte")).toInt(-1);
            if (!lookupWipeBySmpte(code, &pattern, &inherentReverse)) {
                validationError = QStringLiteral("Unknown SMPTE wipe");
            } else {
                patternId = pattern.id;
                direction = WipeDirectionMode::Forward;
            }
        } else if (obj.contains(QStringLiteral("pattern"))) {
            patternId = obj.value(QStringLiteral("pattern")).toString();
            bool found = false;
            for (const WipePattern& available : builtinWipePatterns()) {
                found |= available.id == patternId;
            }
            if (!found) {
                validationError = QStringLiteral("Unknown wipe pattern");
            }
        }
        if (obj.contains(QStringLiteral("dir")) &&
            !wipeDirectionModeFromString(obj.value(QStringLiteral("dir")).toString(), &direction)) {
            validationError = QStringLiteral("Unknown wipe direction");
        }
        if (inherentReverse && direction != WipeDirectionMode::PingPong) {
            direction = direction == WipeDirectionMode::Reverse ? WipeDirectionMode::Forward : WipeDirectionMode::Reverse;
        }
        if (obj.contains(QStringLiteral("edge")) &&
            !wipeEdgeModeFromString(obj.value(QStringLiteral("edge")).toString(), &edge)) {
            validationError = QStringLiteral("Unknown wipe edge mode");
        }
        if (obj.contains(QStringLiteral("amount"))) {
            amount = obj.value(QStringLiteral("amount")).toInt(-1);
            if (amount < 0 || amount > 40) {
                validationError = QStringLiteral("Wipe amount must be 0..40");
            }
        }
        if (obj.contains(QStringLiteral("color"))) {
            color = obj.value(QStringLiteral("color")).toString();
            static const QRegularExpression rgb(QStringLiteral("^#[0-9a-fA-F]{3}([0-9a-fA-F]{3})?$"));
            if (!rgb.match(color).hasMatch()) {
                validationError = QStringLiteral("Wipe color must be #RGB or #RRGGBB");
            }
        }
        if (!validationError.isEmpty() || !validateWipeExtras(config, obj, &validationError)) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), validationError);
            sendTo(client, err);
            return;
        }
        // Publish one coherent configuration rather than intermediate values
        // that would trigger repeated multiview rebuilds and panel snapshots.
        {
            QSignalBlocker blocker(config);
            config->setWipePatternId(patternId);
            config->setWipeEdgeMode(edge);
            config->setWipeEdgeAmount(amount);
            config->setWipeBorderColor(color);
            applyWipeExtras(config, obj, &validationError);
            m_engine->setWipeDirectionMode(direction);
        }
        emit config->configurationChanged();
        m_engine->executeWipe();
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("stinger")) {
        const int slot = obj.value(QStringLiteral("slot")).toInt(-1);
        const bool reverse = obj.value(QStringLiteral("reverse")).toBool(false);
        if (!m_engine->executeStinger(slot, reverse)) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), QStringLiteral("Stinger is not available"));
            sendTo(client, err);
            return;
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("stingers")) {
        const QJsonArray rows = obj.value(QStringLiteral("slots")).toArray();
        if (rows.size() != 10) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), QStringLiteral("Stingers require 10 slots"));
            sendTo(client, err);
            return;
        }
        QList<StingerSlot> entries;
        for (const QJsonValue& value : rows) {
            if (!value.isObject()) {
                QJsonObject err;
                err.insert(QStringLiteral("event"), QStringLiteral("error"));
                err.insert(QStringLiteral("message"), QStringLiteral("Invalid stinger slot"));
                sendTo(client, err);
                return;
            }
            const QJsonObject row = value.toObject();
            StingerSlot entry;
            entry.media = row.value(QStringLiteral("media")).toString();
            entry.reverse = row.value(QStringLiteral("reverse")).toString();
            entry.cutFrames = row.value(QStringLiteral("cutFrames")).toInt(12);
            entry.lengthFrames = row.value(QStringLiteral("lengthFrames")).toInt(50);
            entries.append(entry);
        }
        if (!m_engine->configuration()->replaceStingers(entries)) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), QStringLiteral("Invalid stinger configuration"));
            sendTo(client, err);
            return;
        }
        if (!saveConfiguration()) {
            return;
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("wipe_settings")) {
        Configuration* config = m_engine->configuration();
        const QString id = obj.value("pattern").toString();
        bool found = false;
        for (const auto& pattern : builtinWipePatterns()) found |= pattern.id == id;
        WipeDirectionMode direction;
        WipeEdgeMode edge;
        QString error;
        const QString color = obj.value("color").toString();
        if (!found || !wipeDirectionModeFromString(obj.value("direction").toString(), &direction)
            || !wipeEdgeModeFromString(obj.value("edge").toString(), &edge)
            || !validIndex("amount", 41, true)
            || !QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{6}$")).match(color).hasMatch()
            || !validateWipeExtras(config, obj, &error)) {
            rejectKeyerRequest(error.isEmpty() ? QStringLiteral("Invalid wipe settings") : error);
            return;
        }
        // One validated request avoids partially applying a web form through
        // several asynchronous adapter commands. This never executes a take.
        {
            QSignalBlocker blocker(config);
            config->setWipePatternId(id);
            config->setWipeDirectionMode(direction);
            config->setWipeEdgeMode(edge);
            config->setWipeEdgeAmount(obj.value("amount").toInt());
            config->setWipeBorderColor(color);
            applyWipeExtras(config, obj, &error);
        }
        emit config->configurationChanged();
        if (!saveConfiguration()) return;
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("wipe_style")) {
        QString extraError;
        if (!applyWipeExtras(m_engine->configuration(), obj, &extraError)) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), extraError);
            sendTo(client, err);
            return;
        }
        m_engine->setPreviewCursor(obj.value(QStringLiteral("cursor")).toBool(false));
        if (obj.value(QStringLiteral("save")).toBool(false)) {
            if (!saveConfiguration()) {
                return;
            }
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("wipe_presets")) {
        const QJsonArray rows = obj.value(QStringLiteral("presets")).toArray();
        QList<int> presets;
        for (const QJsonValue& value : rows) {
            presets.append(value.toInt(-1));
        }
        if (!m_engine->configuration()->replaceWipePresets(presets)) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), QStringLiteral("Wipe presets require 10 codes from 0 to 999"));
            sendTo(client, err);
            return;
        }
        if (!saveConfiguration()) {
            return;
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("wipe_pattern")) {
        const QString id = obj.value(QStringLiteral("pattern")).toString();
        bool found = false;
        for (const WipePattern& pattern : builtinWipePatterns()) {
            if (pattern.id == id) {
                found = true;
                break;
            }
        }
        if (!found) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), QStringLiteral("Unknown wipe pattern"));
            sendTo(client, err);
            return;
        }
        m_engine->configuration()->setWipePatternId(id);
        if (!saveConfiguration()) {
            return;
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("catalog")) {
        QJsonArray patterns;
        for (const WipePattern& pattern : builtinWipePatterns()) {
            QJsonObject row;
            row.insert(QStringLiteral("id"), pattern.id);
            row.insert(QStringLiteral("label"), pattern.label);
            row.insert(QStringLiteral("smpte"), pattern.smpte);
            row.insert(QStringLiteral("smpteReverse"), pattern.smpteReverse);
            QJsonArray sonyAliases;
            for(int code:supportedSonyWipes(m_engine->expandedSonyAvailable(),m_engine->enhancedSonyAvailable(),m_engine->rotarySonyAvailable(),m_engine->mosaicSonyAvailable(),m_engine->compoundSonyAvailable())) {
                WipePattern mapped;bool reverse=false;
                if(lookupWipeBySony(code,&mapped,&reverse)&&mapped.id==pattern.id)
                    sonyAliases.append(QJsonObject{{"code",code},{"reverse",reverse}});
            }
            row.insert(QStringLiteral("sony"),sonyAliases);
            patterns.append(row);
        }
        QJsonObject catalog;
        catalog.insert(QStringLiteral("event"), QStringLiteral("catalog"));
        catalog.insert(QStringLiteral("patterns"), patterns);
        sendTo(client, catalog);
        return;
    }
    if (cmd == QLatin1String("wipe_dir") || cmd == QLatin1String("wipe_direction")) {
        WipeDirectionMode direction = WipeDirectionMode::Forward;
        if (!wipeDirectionModeFromString(obj.value(QStringLiteral("mode")).toString(), &direction)) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), QStringLiteral("Unknown wipe direction"));
            sendTo(client, err);
            return;
        }
        m_engine->setWipeDirectionMode(direction);
        if (!saveConfiguration()) {
            return;
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("wipe_edge")) {
        WipeEdgeMode edge = WipeEdgeMode::Hard;
        if (wipeEdgeModeFromString(obj.value(QStringLiteral("mode")).toString(), &edge)) {
            m_engine->configuration()->setWipeEdgeMode(edge);
        }
        if (obj.contains(QStringLiteral("amount"))) {
            m_engine->configuration()->setWipeEdgeAmount(obj.value(QStringLiteral("amount")).toInt());
        }
        if (obj.contains(QStringLiteral("color"))) {
            m_engine->configuration()->setWipeBorderColor(obj.value(QStringLiteral("color")).toString());
        }
        if (!saveConfiguration()) {
            return;
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("me")) {
        m_engine->setActiveMe(obj.value(QStringLiteral("slot")).toInt(0));
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("pgm") || cmd == QLatin1String("program")) {
        m_engine->hotPunchProgram(obj.value(QStringLiteral("source")).toInt(-1));
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("dsk")) {
        const int slot = obj.value(QStringLiteral("slot")).toInt(0);
        const bool mix = obj.value(QStringLiteral("mix")).toBool(false);
        const int frames = obj.value(QStringLiteral("frames")).toInt(0);
        const bool on = obj.contains(QStringLiteral("on"))
            ? obj.value(QStringLiteral("on")).toBool()
            : !m_engine->isDskOn(slot);
        if (mix && frames > 0) {
            m_engine->setDskSlot(slot, on, frames);
        } else if (slot == 0) {
            m_engine->setDskOn(on);
        } else {
            m_engine->setDskSlot(slot, on, 0);
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("dsk_source")) {
        m_engine->setDskSource(obj.value(QStringLiteral("slot")).toInt(0), obj.value(QStringLiteral("source")).toInt(-1));
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("key_on")) {
        const int slot = obj.value(QStringLiteral("slot")).toInt(0);
        const bool on = obj.contains(QStringLiteral("on"))
            ? obj.value(QStringLiteral("on")).toBool()
            : !m_engine->keyOn(slot);
        m_engine->setKeyOn(slot, on);
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("key_source")) {
        m_engine->setKeySource(obj.value(QStringLiteral("slot")).toInt(0), obj.value(QStringLiteral("source")).toInt(-1));
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("next")) {
        const QString target = obj.value(QStringLiteral("target")).toString();
        if (obj.value(QStringLiteral("reset")).toBool(false)) {
            m_engine->resetNextTransition();
        } else if (target == QLatin1String("key")) {
            m_engine->toggleNextKey(obj.value(QStringLiteral("slot")).toInt(0));
        } else {
            m_engine->toggleNextBackground();
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("dsk_preview") || cmd == QLatin1String("dskpvw")) {
        const int slot = obj.value(QStringLiteral("slot")).toInt(0);
        const bool on = obj.contains(QStringLiteral("on"))
            ? obj.value(QStringLiteral("on")).toBool()
            : !m_engine->isDskPreview(slot);
        m_engine->setDskPreview(slot, on);
        sendTo(client, ack);
        return;
    }
    if(cmd=="dip_color"){if(!obj.value("color").isString()||!m_engine->setDipColour(obj.value("color").toString())){rejectKeyerRequest("Invalid DIP colour or failed save");return;}sendTo(client,ack);return;}
    if(cmd=="mix_params"){
        int a=obj.value("aGain").toInt(m_engine->configuration()->superMixGainA()),b=obj.value("bGain").toInt(m_engine->configuration()->superMixGainB());
        for(const auto& field:QStringList{"aGain","bGain"})if(obj.contains(field)&&(!obj.value(field).isDouble()||obj.value(field).toDouble()!=obj.value(field).toInt(-1))){rejectKeyerRequest("Mix gains must be integers");return;}
        if(!m_engine->setSuperMixGains(a,b)){rejectKeyerRequest("SUPER MIX gains unavailable or invalid");return;}sendTo(client,ack);return;
    }
    if (cmd == QLatin1String("rate")) {
        const int frames = obj.value(QStringLiteral("frames")).toInt(-1);
        if (frames < 1 || frames > 1000) {
            QJsonObject err;
            err.insert(QStringLiteral("event"), QStringLiteral("error"));
            err.insert(QStringLiteral("message"), QStringLiteral("Rate must be 1..1000 frames"));
            sendTo(client, err);
            return;
        }
        m_engine->configuration()->setAutoDurationFrames(frames);
        if (!saveConfiguration()) {
            return;
        }
        sendTo(client, ack);
        return;
    }
    if (cmd == QLatin1String("state")) {
        sendTo(client, stateObject());
        return;
    }
    if (cmd == QLatin1String("aux")) {
        const int role=obj.value("role").toInt(-1), source=obj.value("source").toInt(-1);
        if (!m_engine->setAuxSource(role, source)) { rejectKeyerRequest(QStringLiteral("AUX route unavailable")); return; }
        sendTo(client, ack); return;
    }
    if (cmd == QLatin1String("mv_bank")) {
        const double bank = obj.value(QStringLiteral("bank")).toDouble(-1);
        if (bank != 0 && bank != 1) { rejectKeyerRequest(QStringLiteral("Invalid multiview bank")); return; }
        m_engine->setMultiviewBank(int(bank)); sendTo(client, ack); return;
    }
    if (cmd == QLatin1String("layout")) {
        Configuration* config = m_engine->configuration();
        for (const QString& field : {QStringLiteral("safePreviewAspect"), QStringLiteral("safeProgramAspect")}) {
            if (obj.contains(field) && (!obj.value(field).isString() || !Configuration::validSafeAspect(obj.value(field).toString()))) {
                rejectKeyerRequest(QStringLiteral("Invalid safe-area aspect")); return;
            }
        }
        if (obj.contains(QStringLiteral("safePreset")) && obj.value(QStringLiteral("safePreset")) != QJsonValue(QStringLiteral("ebu-r95"))
            && obj.value(QStringLiteral("safePreset")) != QJsonValue(QStringLiteral("legacy"))) {
            rejectKeyerRequest(QStringLiteral("Invalid safe-area preset")); return;
        }
        if (obj.contains(QStringLiteral("safePreviewAspect"))) config->setSafePreviewAspect(obj.value(QStringLiteral("safePreviewAspect")).toString());
        if (obj.contains(QStringLiteral("safeProgramAspect"))) config->setSafeProgramAspect(obj.value(QStringLiteral("safeProgramAspect")).toString());
        if (obj.contains(QStringLiteral("safePreset"))) config->setSafePreset(obj.value(QStringLiteral("safePreset")).toString());
        if (obj.contains(QStringLiteral("programLeft"))) {
            config->setProgramLeft(obj.value(QStringLiteral("programLeft")).toBool());
        }
        if (obj.contains(QStringLiteral("nameEdge"))) {
            config->setNameEdge(obj.value(QStringLiteral("nameEdge")).toString());
        }
        if (obj.contains(QStringLiteral("nameAlign"))) {
            config->setNameAlign(obj.value(QStringLiteral("nameAlign")).toString());
        }
        if (obj.contains(QStringLiteral("clockEdge"))) {
            config->setClockEdge(obj.value(QStringLiteral("clockEdge")).toString());
        }
        if (obj.contains(QStringLiteral("clockAlign"))) {
            config->setClockAlign(obj.value(QStringLiteral("clockAlign")).toString());
        }
        if (obj.contains(QStringLiteral("safePreview"))) {
            config->setSafePreview(obj.value(QStringLiteral("safePreview")).toBool());
        }
        if (obj.contains(QStringLiteral("safeProgram"))) {
            config->setSafeProgram(obj.value(QStringLiteral("safeProgram")).toBool());
        }
        if (obj.contains(QStringLiteral("meters"))) {
            config->setMeters(obj.value(QStringLiteral("meters")).toBool());
        }
        if (!saveConfiguration()) {
            return;
        }
        sendTo(client, ack);
        return;
    }

    QJsonObject err;
    err.insert(QStringLiteral("event"), QStringLiteral("error"));
    err.insert(QStringLiteral("message"), QStringLiteral("Unknown command"));
    sendTo(client, err);
}

void PanelProtocol::broadcast(const QJsonObject& object)
{
    for (QTcpSocket* client : m_clients) {
        sendTo(client, object);
    }
}

void PanelProtocol::sendTo(QTcpSocket* client, const QJsonObject& object)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    client->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    client->write("\n");
}

QJsonObject PanelProtocol::stateObject() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("event"), QStringLiteral("state"));
    obj.insert(QStringLiteral("connected"), m_engine->isConnected());
    obj.insert(QStringLiteral("autoFrames"), m_engine->configuration()->autoDurationFrames());
    obj.insert(QStringLiteral("me"), m_engine->activeMe());
    obj.insert(QStringLiteral("preview"), m_engine->previewSource());
    obj.insert(QStringLiteral("program"), m_engine->programSource());
    obj.insert(QStringLiteral("transitioning"), m_engine->isTransitioning());
    obj.insert(QStringLiteral("manual"), m_engine->isManualTransition());
    obj.insert(QStringLiteral("position"), m_engine->manualPosition());
    obj.insert(QStringLiteral("previewTransitioning"), m_engine->isPreviewTransitioning());
    obj.insert(QStringLiteral("take"), m_engine->activeTakeName());
    obj.insert(QStringLiteral("wipeLit"), m_engine->activeTakeName() == QLatin1String("wipe"));
    obj.insert(QStringLiteral("transitionPreview"), m_engine->isTransitionPreview());
    obj.insert(QStringLiteral("mixLit"), m_engine->activeTakeName() == QLatin1String("mix"));
    obj.insert(QStringLiteral("dsk"), m_engine->isDskOn());
    obj.insert(QStringLiteral("ftb"), m_engine->isFtb());
    obj.insert(QStringLiteral("dskPreview"), m_engine->isDskPreview());
    QJsonArray supersources;
    for(int sourceId:{m_engine->previewSource(),m_engine->programSource()}){
        QJsonObject entry{{"source",-1},{"boxes",QJsonArray{}}};auto source=m_engine->configuration()->sourceById(sourceId);
        if(source&&source->enabled&&source->type==SourceType::SuperSource){auto layout=m_engine->configuration()->superSource(source->argument);if(layout){auto bindings=m_engine->superSourceBindings(sourceId);QJsonArray boxes;
            for(auto& box:layout->boxes)if(box.kind=="input")boxes.append(QJsonObject{{"id",box.id},{"name",box.name},{"button",box.keyButton},{"input",bindings.value(box.id,box.input)}});
            entry=QJsonObject{{"source",sourceId},{"name",source->name},{"prepared",m_engine->sourceReady(sourceId)},{"boxes",boxes}};
        }}supersources.append(entry);
    }
    obj.insert("supersources",supersources);
    obj.insert("dmeBackgrounds",m_engine->configuration()->dmeBackgrounds());obj.insert("dmeBackgroundScopes",m_engine->configuration()->dmeBackgroundScopes());
    QJsonArray colors;for(auto& source:m_engine->configuration()->sources())if(source.type==SourceType::Matte&&source.isAssigned()&&source.id!=11&&source.id!=23)colors.append(source.id);obj.insert("colorSources",colors);
    QJsonArray keys;
    QJsonArray nextKeys;
    for (int i = 0; i < m_engine->keyerCount(); ++i) {
        QJsonObject key;
        key.insert(QStringLiteral("source"), m_engine->keySource(i));
        key.insert(QStringLiteral("on"), m_engine->keyOn(i));
        key.insert(QStringLiteral("processing"),m_engine->configuration()->keyProcessing(m_engine->activeMe(),i).toJson());
        keys.append(key);
        nextKeys.append(m_engine->nextKey(i));
    }
    obj.insert(QStringLiteral("keys"), keys);
    QJsonObject capabilities;
    capabilities.insert("supersources",true);
    capabilities.insert(QStringLiteral("meCount"), m_engine->meCount());
    capabilities.insert(QStringLiteral("keyersPerMe"), m_engine->keyerCount());
    capabilities.insert(QStringLiteral("dskCount"), m_engine->dskCount());
    capabilities.insert(QStringLiteral("keyMode"), QStringLiteral("linear-alpha"));
    QJsonArray keyModes{"linear","chroma"};if(m_engine->nativeKeyAvailable())keyModes.append("luma");
    capabilities.insert("keyModes",keyModes);capabilities.insert("keyInversion",m_engine->nativeKeyAvailable());capabilities.insert("maskInversion",m_engine->nativeKeyAvailable());
    capabilities.insert(QStringLiteral("keyMask"),true);
    QJsonArray mixes{"mix","dip","vfade","fadecut","cutfade"};if(m_engine->broadcastMixAvailable()){mixes.append("nam");mixes.append("supermix");}capabilities.insert("mixModes",mixes);
    QJsonArray dmeEffects{"push","slide"};if(m_engine->nativeDmeAvailable()){dmeEffects.append("move");dmeEffects.append("cube");dmeEffects.append("zoom");}if(m_engine->pageDmeAvailable()){dmeEffects.append("page_curl");dmeEffects.append("page_roll");}capabilities.insert("dmeEffects",dmeEffects);capabilities.insert("dmeBackground",m_engine->pageDmeAvailable());capabilities.insert("dmeBackgroundScopes",m_engine->pageDmeAvailable());capabilities.insert("dmeBackgroundImages",m_engine->staticDmeAvailable());capabilities.insert("mixPreparation",true);capabilities.insert("broadcastMixes",m_engine->broadcastMixAvailable());capabilities.insert("sonyDmeBackground",m_engine->primitiveSonyAvailable());capabilities.insert("sonyGeometry",m_engine->enhancedSonyAvailable());capabilities.insert("sonyMosaic",m_engine->mosaicSonyAvailable());
    QJsonArray sonyWipes;for(int code:supportedSonyWipes(m_engine->expandedSonyAvailable(),m_engine->enhancedSonyAvailable(),m_engine->rotarySonyAvailable(),m_engine->mosaicSonyAvailable(),m_engine->compoundSonyAvailable()))sonyWipes.append(code);capabilities.insert("sonyWipes",sonyWipes);
    QJsonArray pending;for(int code:pendingSonyWipes())pending.append(code);capabilities.insert("sonyPendingWipes",pending);
    QJsonArray sonyDmes;if(m_engine->nativeDmeAvailable())for(int code:supportedSonyDmes(m_engine->primitiveSonyAvailable(),m_engine->spatialSonyAvailable(),m_engine->planarSonyAvailable(),m_engine->mirrorSonyAvailable(),m_engine->frameSonyAvailable()))sonyDmes.append(code);capabilities.insert("sonyDmes",sonyDmes);
    QJsonArray pendingDmes;for(int code:pendingSonyDmes(m_engine->spatialSonyAvailable(),m_engine->planarSonyAvailable(),m_engine->mirrorSonyAvailable(),m_engine->frameSonyAvailable()))pendingDmes.append(code);capabilities.insert("sonyPendingDmes",pendingDmes);
    obj.insert(QStringLiteral("capabilities"), capabilities);
    QJsonArray banks;
    for (int me = 0; me < m_engine->meCount(); ++me) {
        QJsonObject bank;
        bank.insert(QStringLiteral("program"), m_engine->programSource(me));
        bank.insert(QStringLiteral("preview"), m_engine->previewSource(me));
        bank.insert(QStringLiteral("nextBackground"), m_engine->nextBackground(me));
        QJsonArray bankKeys;
        for (int slot = 0; slot < m_engine->keyerCount(); ++slot) {
            QJsonObject key;
            key.insert(QStringLiteral("source"), m_engine->keySource(me, slot));
            key.insert(QStringLiteral("on"), m_engine->keyOn(me, slot));
            key.insert(QStringLiteral("next"), m_engine->nextKey(me, slot));
            key.insert(QStringLiteral("processing"),m_engine->configuration()->keyProcessing(me,slot).toJson());
            bankKeys.append(key);
        }
        bank.insert(QStringLiteral("keys"), bankKeys);
        banks.append(bank);
    }
    obj.insert(QStringLiteral("mes"), banks);
    QJsonArray dsks;
    for (int i = 0; i < m_engine->dskCount(); ++i) {
        QJsonObject entry;
        entry.insert(QStringLiteral("source"), m_engine->configuration()->dskSource(i));
        entry.insert(QStringLiteral("on"), m_engine->isDskOn(i));
        entry.insert(QStringLiteral("mixing"), m_engine->isDskMixing(i));
        entry.insert(QStringLiteral("preview"), m_engine->isDskPreview(i));
        entry.insert(QStringLiteral("processing"),m_engine->configuration()->keyProcessing(0,i,true).toJson());
        dsks.append(entry);
    }
    obj.insert(QStringLiteral("dsks"), dsks);
    QJsonObject next;
    next.insert(QStringLiteral("background"), m_engine->nextBackground());
    next.insert(QStringLiteral("keys"), nextKeys);
    obj.insert(QStringLiteral("next"), next);
    obj.insert(QStringLiteral("wipePattern"), m_engine->configuration()->wipePatternId());
    obj.insert(QStringLiteral("wipeDir"), wipeDirectionModeToString(m_engine->configuration()->wipeDirectionMode()));
    obj.insert(QStringLiteral("wipeSense"), m_engine->wipeSenseForward() ? QStringLiteral("fwd") : QStringLiteral("rev"));
    obj.insert(QStringLiteral("wipeEdge"), wipeEdgeModeToString(m_engine->configuration()->wipeEdgeMode()));
    obj.insert(QStringLiteral("wipeEdgeAmount"), m_engine->configuration()->wipeEdgeAmount());
    obj.insert(QStringLiteral("wipeBorderColor"), m_engine->configuration()->wipeBorderColor());
    QJsonArray presets;
    for (int code : m_engine->configuration()->wipePresets()) {
        presets.append(code);
    }
    obj.insert(QStringLiteral("wipePresets"), presets);
    obj.insert(QStringLiteral("wipeMulti"), m_engine->configuration()->wipeMulti());
    obj.insert("superMixGainA",m_engine->configuration()->superMixGainA());obj.insert("superMixGainB",m_engine->configuration()->superMixGainB());obj.insert("dipColor",m_engine->configuration()->dipColor());obj.insert("wipeTileSize",m_engine->configuration()->wipeTileSize());obj.insert("wipeVertices",m_engine->configuration()->wipeVertices());obj.insert("wipeRounding",m_engine->configuration()->wipeRounding());
    obj.insert(QStringLiteral("wipeBorder"), m_engine->configuration()->wipeBorderAmount());
    obj.insert(QStringLiteral("wipeShadow"), m_engine->configuration()->wipeShadowAmount());
    obj.insert(QStringLiteral("wipeAspectW"), m_engine->configuration()->wipeAspectW());
    obj.insert(QStringLiteral("wipeAspectH"), m_engine->configuration()->wipeAspectH());
    obj.insert(QStringLiteral("wipePosX"), m_engine->configuration()->wipePosX());
    obj.insert(QStringLiteral("wipePosY"), m_engine->configuration()->wipePosY());
    QJsonArray ready;
    QJsonArray assigned;
    for (int i = 0; i < m_engine->configuration()->maxSources(); ++i) {
        const Source* source = m_engine->configuration()->sourceById(i);
        assigned.append(m_engine->sourceSelectable(i));
        ready.append(m_engine->sourceReady(i));
    }
    obj.insert(QStringLiteral("assigned"), assigned);
    obj.insert(QStringLiteral("ready"), ready);
    obj.insert(QStringLiteral("outputsReady"), m_engine->outputsReady());
    obj.insert(QStringLiteral("deck"), m_engine->deckSource());
    obj.insert(QStringLiteral("cued"), m_engine->cuedSource());
    QJsonArray aux;
    for (int role=1; role<=4; ++role) {
        int source=-1; for(const auto& out : m_engine->configuration()->destinations()) if(out.enabled && out.aux==role) source=out.source;
        aux.append(source);
    }
    obj.insert(QStringLiteral("aux"), aux);
    obj.insert(QStringLiteral("layout"), m_engine->configuration()->multiviewLayoutJson());
    obj.insert(QStringLiteral("stingers"), m_engine->configuration()->stingersJson());
    return obj;
}

QJsonObject PanelProtocol::tallyObject() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("event"), QStringLiteral("tally"));
    obj.insert(QStringLiteral("preview"), m_engine->previewSource());
    obj.insert(QStringLiteral("program"), m_engine->programSource());
    obj.insert(QStringLiteral("wipeLit"), m_engine->activeTakeName() == QLatin1String("wipe"));
    obj.insert(QStringLiteral("transitionPreview"), m_engine->isTransitionPreview());
    obj.insert(QStringLiteral("mixLit"), m_engine->activeTakeName() == QLatin1String("mix"));
    obj.insert("supersources",stateObject().value("supersources"));
    return obj;
}
