#include <QUrlQuery>
#include <QtTest/QtTest>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QtEndian>
#include <cstring>

#include "core/SwitcherEngine.h"
#include "core/PanelProtocol.h"
#include "core/AmcpClient.h"
#include "core/OscListener.h"

#include <limits>
#include "config/Configuration.h"

namespace { QByteArray oscPad(const QByteArray&);QByteArray oscInt32(qint32);QByteArray oscBundle(const QByteArray&); }

QJsonObject nativeScene(const QString& command)
{
    if(!command.startsWith(QStringLiteral("CALL 11-80 SCENE ")))return {};
    return QJsonDocument::fromJson(QByteArray::fromBase64(command.section(' ',-1).toLatin1())).object();
}
bool hasNativeState(const QStringList& commands,const QJsonObject& expected)
{
    for(const auto& cmd:commands){const auto scene=nativeScene(cmd);if(scene.isEmpty())continue;bool match=true;
        for(auto i=expected.begin();i!=expected.end();++i)if(scene.value(i.key())!=i.value())match=false;
        if(match)return true;
    }return false;
}

class FakeCaspar : public QObject
{
    Q_OBJECT

public:
    QString engineVersion="2.5.1 Stable (casparMIX 0.5.0)";
    explicit FakeCaspar(QObject* parent = nullptr)
        : QObject(parent)
    {
        if (!m_server.listen(QHostAddress::LocalHost)) {
            qFatal("FakeCaspar: cannot listen");
        }
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            m_client = m_server.nextPendingConnection();
            connect(m_client, &QTcpSocket::readyRead, this, [this]() {
                m_buffer += m_client->readAll();
                while (true) {
                    const int pos = m_buffer.indexOf("\r\n");
                    if (pos < 0) {
                        break;
                    }
                    const QString command = QString::fromUtf8(m_buffer.left(pos));
                    m_buffer.remove(0, pos + 2);
                    commands.append(command);
                    const bool injectedFailure = failuresRemaining.value(command) > 0;
                    const bool fail = injectedFailure || command.contains(QStringLiteral(" IS_KEY "));
                    if (injectedFailure) {
                        --failuresRemaining[command];
                    }
                    QTcpSocket* peer = m_client;
                    const QByteArray response=command=="VERSION"?("201 VERSION OK\r\n"+engineVersion.toUtf8()+"\r\n"):QByteArray("202 OK\r\n");
                    QTimer::singleShot(responseDelayMs, peer, [peer, fail, response]() {
                        peer->write(fail ? QByteArray("502 COMMAND FAILED\r\n") : response);
                        peer->flush();
                    });
                }
            });
        });
    }

    int port() const { return m_server.serverPort(); }
    QStringList commands;
    int responseDelayMs = 0;
    QHash<QString, int> failuresRemaining;

private:
    QTcpServer m_server;
    QTcpSocket* m_client = nullptr;
    QByteArray m_buffer;
};

class TestSwitcherEngine : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_configRoot;
private slots:
    void frameSonyDmeProtocol() {
        FakeCaspar caspar;caspar.engineVersion="2.5.1 Stable (casparMIX 0.17.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
        engine.connectToCaspar();QTRY_VERIFY(engine.frameSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
        engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);QTRY_COMPARE(client.queuedCommandCount(),0);
        Transition effect;effect.type=TransitionType::SonyDme;effect.sonyDmeCode=1201;effect.durationFrames=25;
        QVERIFY(engine.setManualPosition(2000,effect));QTRY_COMPARE(client.queuedCommandCount(),0);
        bool found=false;for(const auto& command:caspar.commands)found|=command.contains("DMENATIVE 25 SONY_1201 MANUAL 1");QVERIFY(found);
        QString name,direction;QVERIFY(!lookupDmeBySony(1201,&name,&direction,true,true,true,true));
        QVERIFY(lookupDmeBySony(1201,&name,&direction,true,true,true,true,true));QCOMPARE(name,QString("sony"));
    }

    void reconnectRestoresCompositions() {
        FakeCaspar caspar;
        caspar.engineVersion = "2.5.1 Stable (casparMIX 0.16.0)";
        Configuration config;
        config.setOscPort(0);
        config.setCasparHost("127.0.0.1");
        config.setCasparPort(caspar.port());
        AmcpClient client;
        client.setReconnectIntervalMs(0);
        SwitcherEngine engine(&config, &client);
        engine.connectToCaspar();
        QTRY_VERIFY(engine.isConnected());
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        engine.selectPreview(0);
        QTRY_COMPARE(engine.previewSource(), 0);
        engine.executeCut();
        QTRY_COMPARE(engine.programSource(), 0);
        engine.selectPreview(1);
        QTRY_COMPARE(engine.previewSource(), 1);
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        engine.setActiveMe(1);
        engine.hotPunchProgram(2);
        QTRY_COMPARE(engine.programSource(), 2);
        engine.selectPreview(3);
        QTRY_COMPARE(engine.previewSource(), 3);
        engine.setKeySource(0, 1);
        QTRY_COMPARE(engine.keySource(0), 1);
        engine.setKeyOn(0, true);
        QTRY_VERIFY(engine.keyOn(0));
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        engine.toggleNextKey(0);
        engine.toggleNextBackground();
        QVERIFY(!engine.nextBackground());
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        engine.setActiveMe(0);
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        engine.disconnectFromCaspar();
        QTRY_VERIFY(!engine.isConnected());
        caspar.commands.clear();
        engine.connectToCaspar();
        QTRY_VERIFY(engine.isConnected());
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        QCOMPARE(engine.programSource(), 0);
        QCOMPARE(engine.previewSource(), 1);
        const auto pgm = QStringLiteral("PLAY 10-1 route://1-1");
        const auto pvw = QStringLiteral("PLAY 9-1 route://2-1");
        QVERIFY(caspar.commands.contains(pgm));
        QVERIFY(caspar.commands.contains(pvw));
        QVERIFY(caspar.commands.indexOf(pgm) > caspar.commands.indexOf("PLAY 1-1 DECKLINK DEVICE 1"));
        QVERIFY(caspar.commands.contains("PLAY 13-1 route://3-1"));
        QVERIFY(caspar.commands.contains("PLAY 14-1 route://3-1")); // Key-only preview retains background PGM.
        QVERIFY(caspar.commands.contains("PLAY 13-11 route://2-1"));
        QCOMPARE(engine.programSource(1), 2);
        QCOMPARE(engine.previewSource(1), 3);
        QVERIFY(engine.keyOn(1, 0));
        QVERIFY(caspar.commands.contains("CLEAR 18-1"));
        QVERIFY(!engine.isBusy());
        engine.disconnectFromCaspar();
        QTRY_VERIFY(!engine.isConnected());
        caspar.commands.clear();
        caspar.failuresRemaining[pgm] = 1;
        QSignalSpy errors(&engine, &SwitcherEngine::error);
        engine.connectToCaspar();
        QTRY_VERIFY(engine.isConnected());
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        QVERIFY(errors.count() > 0);
        QCOMPARE(caspar.commands.count(pgm), 1);
        QTest::qWait(50);
        QCOMPARE(caspar.commands.count(pgm), 1); // Failed recovery is not retried blindly.
        engine.armMixer();
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        QCOMPARE(caspar.commands.count(pgm), 2);
        QCOMPARE(engine.programSource(), 0);
        QCOMPARE(engine.previewSource(), 1);
        QVERIFY(!engine.isBusy());
    }

    void superSourceInstancesByMe() {
        FakeCaspar peer;Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        SuperSourceLayout layout;layout.id="shared";layout.name="Shared template";
        SuperSourceBox box;box.id="window";box.input=0;layout.boxes={box};QVERIFY(config.setSuperSources({layout}));
        auto source=*config.sourceById(6);source.type=SourceType::SuperSource;source.argument=layout.id;source.enabled=true;config.setSource(source);
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();
        for(int me=0;me<4;++me)QTRY_VERIFY(engine.sourceReady(6,me));
        QCOMPARE(engine.superSourceChannel(6,0),7);QCOMPARE(engine.superSourceChannel(6,1),57);
        engine.selectPreview(6);QTRY_COMPARE(engine.previewSource(),6);engine.executeCut();QTRY_COMPARE(engine.programSource(),6);
        QVERIFY(engine.setSuperSourceInput(6,"window",1));QTRY_VERIFY(engine.sourceReady(6));
        QCOMPARE(engine.superSourceBindings(6,0).value("window"),1);
        engine.setActiveMe(1);QCOMPARE(engine.superSourceBindings(6).value("window",0),0);
        engine.selectPreview(6);QTRY_COMPARE(engine.previewSource(),6);engine.executeCut();QTRY_COMPARE(engine.programSource(),6);
        peer.commands.clear();QVERIFY(engine.setSuperSourceInput(6,"window",2));QTRY_VERIFY(engine.sourceReady(6));QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(peer.commands.contains("PLAY 57-1 route://3-1"));QVERIFY(!peer.commands.contains("PLAY 7-1 route://3-1"));
        QCOMPARE(engine.superSourceBindings(6,0).value("window"),1);QCOMPARE(engine.superSourceBindings(6,1).value("window"),2);
        QVERIFY(!engine.setSuperSourceInput(6,"window",1001)); // M/E 2 cannot consume itself.
        engine.setActiveMe(0);QCOMPARE(engine.superSourceBindings(6).value("window"),1);
        QCOMPARE(engine.programSource(0),6);QCOMPARE(engine.programSource(1),6);
    }

    void testSuperSourceCompositionAndBindings();
    void testSuperSourceBatchFailure();
    void testSuperSourceBusContext();

    void initTestCase() {
        QVERIFY(m_configRoot.isValid());
        qputenv("XDG_CONFIG_HOME", m_configRoot.path().toUtf8());
        QVERIFY(Configuration().configFilePath().startsWith(m_configRoot.path()));
    }

    void sonyDirectPanel() {
        FakeCaspar caspar;caspar.engineVersion="2.5.1 Stable (casparMIX 0.7.0)";
        Configuration config;config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
        AmcpClient amcp;amcp.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&amcp);
        engine.connectToCaspar();QTRY_VERIFY(engine.expandedSonyAvailable());
        QTRY_COMPARE(amcp.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
        engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket peer;
        peer.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(peer.canReadLine());
        const auto state=QJsonDocument::fromJson(peer.readLine()).object();
        QCOMPARE(state["capabilities"].toObject()["sonyWipes"].toArray().size(),18);
        QCOMPARE(state["capabilities"].toObject()["sonyDmes"].toArray().size(),8);peer.readAll();
        auto write=[&](QJsonObject obj){peer.write(QJsonDocument(obj).toJson(QJsonDocument::Compact)+"\n");};
        for(int code:supportedSonyDmes())for(bool reverse:{false,true}) {
            caspar.commands.clear();
            write({{"cmd","manual"},{"type","dme"},{"sony",code},{"reverse",reverse},{"position",2000}});
            QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(amcp.queuedCommandCount(),0);
            QString effect,dir;QVERIFY(lookupDmeBySony(code,&effect,&dir));
            if(reverse)dir=dir=="left"?"right":dir=="right"?"left":dir=="top"?"bottom":"top";
            const QString token=effect.toUpper()+"_"+dir.toUpper();
            QVERIFY(std::any_of(caspar.commands.begin(),caspar.commands.end(),[&](const QString& c){return c.contains("DMENATIVE")&&c.contains(token);}));
            write({{"cmd","manual"},{"type","dme"},{"sony",code},{"position",0}});
            QTRY_VERIFY(!engine.isBusy());peer.readAll();
        }
        write({{"cmd","manual"},{"type","wipe"},{"sony",22},{"position",2000}});
        QTRY_VERIFY(engine.isManualTransition());
        QTRY_VERIFY(std::any_of(caspar.commands.begin(),caspar.commands.end(),[](const QString& c){return c.contains("WIPESONY 1 SONY 22");}));
        write({{"cmd","manual"},{"type","wipe"},{"sony",22},{"position",0}});QTRY_VERIFY(!engine.isBusy());peer.readAll();
        write({{"cmd","manual"},{"type","dme"},{"sony",22},{"position",2000}});
        QByteArray response;QTRY_VERIFY((response+=peer.readAll()).contains("Unknown or invalid Sony DME"));
        QVERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),0);
    }

    void testCasparMixNativeWipesAndRoutes();
    void testManualTransition();
    void testNativeDmeMove();
    void testNativePageDme();
    void staticDmeBackgroundTake() {
        FakeCaspar caspar;caspar.engineVersion="2.5.1 Stable (casparMIX 0.7.1)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
        QVERIFY(config.setDmeBackgroundImage("cube","/tmp/Original image.png"));
        AmcpClient amcp;amcp.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&amcp);engine.connectToCaspar();
        QTRY_VERIFY(engine.pageDmeAvailable());QTRY_VERIFY(!engine.isPreparing());
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
        engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        Transition cube=Transition::mix(20);cube.type=TransitionType::Cube;
        const QString bg=QString::fromLatin1(staticDmeImageProducer("/tmp/Original image.png").toUtf8().toHex());
        caspar.commands.clear();QVERIFY(engine.setManualPosition(2000,cube));QTRY_COMPARE(amcp.queuedCommandCount(),0);
        QVERIFY(caspar.commands.join('\n').contains("BACKGROUND "+bg));
        QVERIFY(config.setDmeBackgroundImage("cube","/tmp/New image.png"));caspar.commands.clear();
        QVERIFY(engine.setManualPosition(2500,cube));QTRY_COMPARE(amcp.queuedCommandCount(),0);
        for(const auto& cmd:caspar.commands)QVERIFY(!cmd.contains("BACKGROUND"));
        QVERIFY(engine.setManualPosition(0,cube));QTRY_VERIFY(!engine.isBusy());
        caspar.commands.clear();engine.executeTransition(cube);
        const QString next=QString::fromLatin1(staticDmeImageProducer("/tmp/New image.png").toUtf8().toHex());
        QTRY_VERIFY(caspar.commands.join('\n').contains("BACKGROUND "+next));
    }

    void dustMixProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.19.0)";
        QTemporaryDir dir;Configuration config;config.setConfigFilePath(dir.filePath("config.json"));config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(engine.dustMixAvailable());QVERIFY(engine.setDustMix(75,3,10));QVERIFY(!engine.setDustMix(75,0,10));
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        Transition t;QVERIFY(Transition::namedMix("dustmix",2,&t));QVERIFY(engine.setManualPosition(2000,t));QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(peer.commands.join('\n').contains("DUSTMIX 1 MANUAL 1 DUST_RATIO 0.7500 H_SIZE 0.0300 V_SIZE 0.0300 FLASH_RATE 10"));
        peer.commands.clear();QVERIFY(engine.setDustMix(20,4,0));QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(!peer.commands.join('\n').contains("DUST_RATIO"));
        QVERIFY(engine.setManualPosition(0,t));QTRY_VERIFY(!engine.isBusy());QVERIFY(engine.setTransitionPreview(true));QVERIFY(engine.setManualPosition(2000,t));QTRY_COMPARE(client.queuedCommandCount(),0);
        peer.commands.clear();QVERIFY(engine.setDustMix(60,5,20));QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(peer.commands.contains("CALL 9-101 \"DUST_RATIO 0.6000 H_SIZE 0.0500 V_SIZE 0.0500 FLASH_RATE 20\""));
        QVERIFY(engine.setManualPosition(0,t));QTRY_VERIFY(!engine.isBusy());
    }

    void broadcastMixProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.11.0)";
        QTemporaryDir dir;Configuration config;config.setConfigFilePath(dir.filePath("config.json"));
        config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
        engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(engine.setSuperMixGains(70,80));QVERIFY(!engine.setSuperMixGains(101,80));
        QCOMPARE(config.superMixGainA(),70);QCOMPARE(config.superMixGainB(),80);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
        engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        Transition t;QVERIFY(Transition::namedMix("supermix",2,&t));
        QVERIFY(engine.setManualPosition(2000,t));QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(peer.commands.join('\n').contains("SUPER_MIX MANUAL 1 REVERSE 0 A_GAIN 0.7000 B_GAIN 0.8000"));
        peer.commands.clear();QVERIFY(engine.setSuperMixGains(20,30));QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(!peer.commands.join('\n').contains("A_GAIN")); // Program take keeps captured gains.
        QVERIFY(engine.setManualPosition(0,t));QTRY_VERIFY(!engine.isBusy());
        QVERIFY(engine.setTransitionPreview(true));peer.commands.clear();
        QVERIFY(engine.setManualPosition(2000,t));QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(peer.commands.join('\n').contains("A_GAIN 0.2000 B_GAIN 0.3000"));
        peer.commands.clear();QVERIFY(engine.setSuperMixGains(40,50));QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(peer.commands.contains("CALL 9-101 \"A_GAIN 0.4000 B_GAIN 0.5000\""));
        QVERIFY(engine.setManualPosition(0,t));QTRY_VERIFY(!engine.isBusy());
        QVERIFY(engine.setTransitionPreview(false));QVERIFY(Transition::namedMix("nam",2,&t));
        peer.commands.clear();engine.executeTransition(t);QTRY_VERIFY(!engine.isBusy());
        QVERIFY(peer.commands.join('\n').contains("NAM MANUAL 0"));
    }

    void nativeLumaKeyProcessing() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.13.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.nativeKeyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        KeyProcessing luma;QVERIFY(luma.update({{"mode","luma"},{"invert",true},{"mask",true},{"maskInvert",true},{"lumaLow",0.25},{"lumaHigh",0.75}}));
        peer.responseDelayMs=10;QVERIFY(engine.setKeyProcessing(0,false,luma));QCOMPARE(config.keyProcessing(0,0).mode,QString("linear"));QTRY_VERIFY(!engine.isBusy());
        QCOMPARE(config.keyProcessing(0,0).toJson(),luma.toJson());QVERIFY(peer.commands.contains(luma.commands(9,11,true).last()));
        Configuration copy;copy.fromJson(config.toJson());QCOMPARE(copy.keyProcessing(0,0).toJson(),luma.toJson());
        engine.setKeySource(0,0);QTRY_VERIFY(!engine.isBusy());engine.toggleNextKey(0);engine.toggleNextBackground();peer.commands.clear();
        engine.executeTransition(Transition::mix(1));QTRY_VERIFY(!engine.isBusy());QVERIFY(engine.keyOn(0));
        QVERIFY(peer.commands.contains(KeyProcessing{}.commands(10,11,true).last())); // Processed preview route is neutral at destination.
        QVERIFY(!peer.commands.contains(luma.commands(10,11,true).last()));
        QVERIFY(engine.setKeyProcessing(1,true,luma));QTRY_VERIFY(!engine.isBusy());QCOMPARE(config.keyProcessing(0,1,true).toJson(),luma.toJson());
        auto invalid=luma;invalid.lumaHigh=.2;QVERIFY(!engine.setKeyProcessing(0,false,invalid));
    }

    void globalDmeBackgroundPreviewIsolation() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.13.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.spatialSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        QVERIFY(engine.setDmeBackground("global",2));QCOMPARE(config.dmeBackground("cube"),2);QVERIFY(!engine.setDmeBackground("global",1000));
        Transition cube;cube.type=TransitionType::Cube;cube.durationFrames=25;
        QVERIFY(engine.setTransitionPreview(true));QVERIFY(engine.setManualPosition(2000,cube));QTRY_COMPARE(client.queuedCommandCount(),0);peer.commands.clear();
        QVERIFY(engine.setDmeBackground("global",0));QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(peer.commands.join('\n').contains("CALL 9-101 \"BACKGROUND "+QString::fromLatin1(engine.dmeBackgroundProducer(0).toUtf8().toHex())));
        QVERIFY(engine.setDmeBackgroundScope("cube",true));QTRY_COMPARE(client.queuedCommandCount(),0);peer.commands.clear();
        QVERIFY(engine.setDmeBackground("global",2));QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(!peer.commands.join('\n').contains("BACKGROUND"));QCOMPARE(config.dmeBackground("cube"),0);
        QVERIFY(engine.setDmeBackgroundScope("cube",true,true));QTRY_COMPARE(client.queuedCommandCount(),0);QCOMPARE(config.dmeBackground("cube"),2);
        QVERIFY(engine.setDmeBackgroundScope("cube",false));QVERIFY(engine.setManualPosition(0,cube));QTRY_VERIFY(!engine.isBusy());QVERIFY(engine.setTransitionPreview(false));
        QVERIFY(engine.setManualPosition(2000,cube));QTRY_COMPARE(client.queuedCommandCount(),0);peer.commands.clear();
        QVERIFY(engine.setDmeBackground("global",0));QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(!peer.commands.join('\n').contains("BACKGROUND"));
        QVERIFY(engine.setManualPosition(0,cube));QTRY_VERIFY(!engine.isBusy());
    }

    void clipTransportMetadataAndPlayCancelsShuttle() {
        const auto paused=oscPad("/channel/2/stage/layer/1/foreground/paused")+oscPad(",T");
        auto parsed=parseOscDatagram(oscBundle(paused));QCOMPARE(parsed.playback.size(),1);QVERIFY(parsed.playback[0].paused);
        const auto fps=oscPad("/channel/2/stage/layer/1/foreground/file/streams/0/fps")+oscPad(",ii")+oscInt32(24000)+oscInt32(1001);
        parsed=parseOscDatagram(oscBundle(fps));QCOMPARE(parsed.playback.size(),1);QVERIFY(qAbs(parsed.playback[0].nativeFps-24000./1001)<.0001);
        FakeCaspar peer;Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());
        engine.ingestPlayback({2,1,true,true,24000./1001});engine.selectDeckSource(1);peer.commands.clear();engine.toggleDeckPause();QTRY_VERIFY(peer.commands.contains("RESUME 2-1"));
        engine.setDeckJogMode(SwitcherEngine::DeckJogMode::Shuttle);engine.jogDeck(4096);QTRY_VERIFY(peer.commands.join('\n').contains("CALL 2-1 SEEK"));
        engine.toggleDeckPause();QTRY_COMPARE(client.queuedCommandCount(),0);peer.commands.clear();QTest::qWait(120);QVERIFY(!peer.commands.join('\n').contains("SEEK"));
        engine.selectDeckSource(0);peer.commands.clear();engine.toggleDeckPause();QTRY_VERIFY(peer.commands.contains("PAUSE 1-1")); // Other source remains playing.
    }

    void mirrorSonyDmeProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.16.0)";Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.mirrorSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());auto state=QJsonDocument::fromJson(socket.readLine()).object();QCOMPARE(state["capabilities"].toObject()["sonyDmes"].toArray().size(),71);socket.readAll();
        for(int code=1355;code<=1358;++code){peer.commands.clear();socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","dme"},{"sony",code},{"position",2000}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(peer.commands.join('\n').contains(QString("SONY_%1 MANUAL 1").arg(code)));socket.write("{\"cmd\":\"manual\",\"type\":\"dme\",\"sony\":"+QByteArray::number(code)+",\"position\":0}\n");QTRY_VERIFY(!engine.isBusy());}
    }

    void planarSonyDmeProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.14.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.planarSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());
        auto state=QJsonDocument::fromJson(socket.readLine()).object();QCOMPARE(state["capabilities"].toObject()["sonyDmes"].toArray().size(),67);socket.readAll();
        QList<int> codes;for(int code=1051;code<=1058;++code)codes.append(code);for(int code=1061;code<=1064;++code)codes.append(code);codes.append(1068);
        for(int code:codes){peer.commands.clear();socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","dme"},{"sony",code},{"position",2000}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(peer.commands.join('\n').contains(QString("SONY_%1 MANUAL 1").arg(code)));
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","dme"},{"sony",code},{"position",0}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(!engine.isBusy());}
    }

    void spatialSonyDmeProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.12.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
        engine.connectToCaspar();QTRY_VERIFY(engine.spatialSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
        engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());
        auto state=QJsonDocument::fromJson(socket.readLine()).object();QCOMPARE(state["capabilities"].toObject()["sonyDmes"].toArray().size(),54);socket.readAll();
        for(int code:QList<int>{1045,1046,1047,1048,1101,1102,1103,1104,1121,1122}){
            peer.commands.clear();socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","dme"},{"sony",code},{"position",2000}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);
            QVERIFY(peer.commands.join('\n').contains(QString("SONY_%1 MANUAL 1").arg(code)));
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","dme"},{"sony",code},{"position",0}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(!engine.isBusy());
            QVERIFY(engine.setDmeBackground(QString("sony_%1").arg(code),-1));
        }
    }

    void sonyDmePrimitiveProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.10.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
        engine.connectToCaspar();QTRY_VERIFY(engine.primitiveSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());
        auto state=QJsonDocument::fromJson(socket.readLine()).object();QCOMPARE(state["capabilities"].toObject()["sonyDmes"].toArray().size(),44);socket.readAll();
        for(int code:supportedSonyDmes(true)){
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","dme"},{"sony",code},{"position",2000}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);
            QVERIFY(peer.commands.join('\n').contains(QString("PLAY 10-1 route://2 RENDERED DMENATIVE 0 SONY_%1").arg(code))||peer.commands.join('\n').contains(QString("SONY_%1 MANUAL 1").arg(code)));
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","dme"},{"sony",code},{"position",0}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(!engine.isBusy());
        }
        QVERIFY(engine.setDmeBackground("sony_2605",-1));
        QVERIFY(!engine.setDmeBackground("sony_1045",-1));
        config.setDmeBackground("sony_2605",1002);
        QCOMPARE(config.dmeBackground("sony_2605"),1002);
        QCOMPARE(config.dmeBackground("sony_2606"),-1);
        config.setAutoDurationFrames(2);peer.commands.clear();socket.write("{\"cmd\":\"dme\",\"sony\":1025}\n");QTRY_VERIFY(peer.commands.join('\n').contains("route://2 RENDERED DMENATIVE 2 SONY_1025"));QTRY_VERIFY(!engine.isBusy());
    }

    void compoundSonyProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.9.4)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
        engine.connectToCaspar();QTRY_VERIFY(engine.compoundSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());
        auto state=QJsonDocument::fromJson(socket.readLine()).object();const auto caps=state["capabilities"].toObject();QCOMPARE(caps["sonyWipes"].toArray().size(),83);QCOMPARE(caps["sonyPendingWipes"].toArray().size(),33);socket.readAll();
        for(int code:QList<int>{250,251,252,253,254,255,256,257,260,261,262,263,264,265,266,267,268,269}){
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","wipe"},{"sony",code},{"position",2000}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(peer.commands.join('\n').contains(QString("SONY %1 ").arg(code)));
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","wipe"},{"sony",code},{"position",0}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(!engine.isBusy());
        }
        peer.commands.clear();socket.write("{\"cmd\":\"manual\",\"type\":\"wipe\",\"sony\":274,\"position\":2000}\n");QTest::qWait(60);QVERIFY(!engine.isManualTransition());QVERIFY(!peer.commands.join('\n').contains("WIPESONY"));
    }

    void mosaicSonyProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.9.3)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
        engine.connectToCaspar();QTRY_VERIFY(engine.mosaicSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
        engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());
        auto state=QJsonDocument::fromJson(socket.readLine()).object();
        QCOMPARE(state["capabilities"].toObject()["sonyWipes"].toArray().size(),65);socket.readAll();
        socket.write("{\"cmd\":\"wipe_style\",\"tileSize\":20}\n");QTRY_COMPARE(config.wipeTileSize(),20);
        for(int code:QList<int>{200,201,202,203,206,207,208,209,210,211,212,213}) {
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","wipe"},{"sony",code},{"position",2000}}).toJson(QJsonDocument::Compact)+"\n");
            QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);
            QVERIFY(peer.commands.join('\n').contains(QString("SONY %1 ").arg(code)));QVERIFY(peer.commands.join('\n').contains("TILESIZE 20"));
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","wipe"},{"sony",code},{"position",0}}).toJson(QJsonDocument::Compact)+"\n");QTRY_VERIFY(!engine.isBusy());
        }
        socket.write("{\"cmd\":\"wipe_style\",\"tileSize\":1}\n");QTest::qWait(40);QCOMPARE(config.wipeTileSize(),20);
    }

    void rotarySonyProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.9.2)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
        engine.connectToCaspar();QTRY_VERIFY(engine.rotarySonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
        engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());
        auto state=QJsonDocument::fromJson(socket.readLine()).object();
        QCOMPARE(state["capabilities"].toObject()["sonyWipes"].toArray().size(),53);socket.readAll();
        for(int code:QList<int>{150,151,156,158,160,162,516,518,604,606,624,661}) {
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","wipe"},{"sony",code},{"position",2000}}).toJson(QJsonDocument::Compact)+"\n");
            QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);
            QVERIFY(peer.commands.join('\n').contains(QString("SONY %1 ").arg(code)));
            socket.write(QJsonDocument(QJsonObject{{"cmd","manual"},{"type","wipe"},{"sony",code},{"position",0}}).toJson(QJsonDocument::Compact)+"\n");
            QTRY_VERIFY(!engine.isBusy());
        }
    }

    void enhancedSonyProtocol() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.9.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.enhancedSonyAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(socket.canReadLine());
        auto state=QJsonDocument::fromJson(socket.readLine()).object();QCOMPARE(state["capabilities"].toObject()["sonyWipes"].toArray().size(),41);socket.readAll();
        socket.write("{\"cmd\":\"wipe_style\",\"vertices\":3,\"rounding\":25}\n");QTRY_COMPARE(config.wipeVertices(),3);
        socket.write("{\"cmd\":\"manual\",\"type\":\"wipe\",\"sony\":49,\"position\":2000}\n");QTRY_VERIFY(engine.isManualTransition());QTRY_COMPARE(client.queuedCommandCount(),0);
        QVERIFY(peer.commands.join('\n').contains("VERTICES 3 ROUNDING 25"));
        socket.write("{\"cmd\":\"manual\",\"type\":\"wipe\",\"sony\":49,\"position\":0}\n");QTRY_VERIFY(!engine.isBusy());
        socket.write("{\"cmd\":\"wipe_style\",\"vertices\":2}\n");QTest::qWait(40);QCOMPARE(config.wipeVertices(),3);
    }

    void manualDmeUsesRenderedDestination() {
        FakeCaspar peer;peer.engineVersion="2.5.1 Stable (casparMIX 0.9.0)";
        Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());
        AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.nativeDmeAvailable());QTRY_COMPARE(client.queuedCommandCount(),0);
        engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
        for(auto type:{TransitionType::Cube,TransitionType::PageCurl,TransitionType::PageRoll}){
            Transition transition=Transition::mix(20);transition.type=type;peer.commands.clear();QVERIFY(engine.setManualPosition(500,transition));QTRY_COMPARE(client.queuedCommandCount(),0);
            QVERIFY(std::any_of(peer.commands.begin(),peer.commands.end(),[](const QString& cmd){return cmd.startsWith("PLAY 10-1 route://2 RENDERED DMENATIVE ");}));
            QVERIFY(engine.setManualPosition(0,transition));QTRY_VERIFY(!engine.isBusy());
        }
    }

    void testDmeBackgrounds();
    void testNativeDmeRequiresPatch();
    void testManualHandoffFailure();
    void testNumberedMultiviewLabels();
    void testMultiviewCascadeDelegation();
    void testManualWipeAndPreview();
    void testManualDelegationAndOwnership();
    void testManualKeyMixRestoresAudio();
    void testKeyProcessingValidationAndPersistence();
    void testKeyProcessingConfirmationAndRoutes();
    void testNativeMixModes();
    void testSonyWipeNamespace();
    void testDmeDirectionsAndValidation();
    void testNotASingleton();
    void testProducerCommands();
    void testLegacySourceMigration();
    void testPreviewCutAndAuto();
    void testSwapAndHotPunch();
    void testUnassignedSourceIsIgnored();
    void testDskCommands();
    void testKeyersAndNextTransition();
    void testStackedMixEffects();
    void testIncrementalSourceArm();
    void testStillMixerFill();
    void testOscFileTimeAndOverlayClock();
    void testSmilHtmlWipeCommand();
    void testSmpteWipeCatalog();
    void testSmpteLookup();
    void testWipeReverseAndBorder();
    void testTransitionLockIgnoresStackedTakes();
    void testFadeToBlack();
    void testNdiOutputsAndCleanFeed();
    void testDeckSourceSelection();
    void testDeckCueUntilProgram();
    void testDeckJogModes();
    void testTakeHoldWaitsForAcknowledgement();
    void testFtbHoldWaitsForAcknowledgement();
    void testDskFadeWaitsForAcknowledgement();
    void testConfigurationSaveFailure();
    void testWipeRequestValidationIsAtomic();
    void testSourceArmingWaitsForSuccess();
    void testSourceFillRetryKeepsAcceptedProducer();
    void testSourceChangeDuringPreparation();
    void testOutputRetryKeepsAcceptedRoute();
    void testFailedTakeDoesNotRunRemainder();
    void testIdenticalBackgroundReplyDoesNotCompletePreview();
    void testDisconnectDoesNotCommitDeferredTake();
    void testPreparedConfigurationKeepsBuses();
    void testKeyStateAndSourceWaitForAcceptedBatch();
    void testAllFourNextKeysAndReset();
    void testDskTwoPreviewSurvivesFadeOut();
    void testKeyerCapabilitiesAndInvalidRequests();
    void testTransitionPreviewPreservesProgramAndKeys();
    void testTransitionPreviewFailureAndDelegation();
    void testKeyRoutesReusePreparedInput();
    void testMetersFollowConfiguredChannels();
    void testSourceBanksAndFeedback();
    void testManagedOutputsAndAux();
    void testSafeAreaPersistence();
};

void TestSwitcherEngine::testMultiviewCascadeDelegation()
{
    FakeCaspar caspar;
    Configuration config;
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setOscPort(0);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    for (int me : {1, 2, 3, 0}) {
        caspar.commands.clear();
        engine.setActiveMe(me);
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        const int clear = caspar.commands.indexOf(QStringLiteral("CLEAR 11-52"));
        QVERIFY(clear >= 0);
        if (me < 3) {
            const QString play = QStringLiteral("PLAY 11-52 route://%1 RENDERED").arg(13 + me * 2);
            QVERIFY(caspar.commands.indexOf(play) > clear);
        } else {
            for (const auto& command : caspar.commands)
                QVERIFY(!command.startsWith(QStringLiteral("PLAY 11-52 ")));
        }
        engine.setMultiviewBank(1);
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        engine.setMultiviewBank(0);
        QTRY_COMPARE(client.queuedCommandCount(), 0);
    }
}

void TestSwitcherEngine::testNumberedMultiviewLabels()
{
    FakeCaspar caspar;
    Configuration config;
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setOscPort(0);
    config.sourceById(0)->name=QStringLiteral("Camera A");
    config.sourceById(1)->name=QStringLiteral("2 - Camera B");
    config.sourceById(2)->name=QStringLiteral("3 -Camera C");
    config.sourceById(3)->name=QStringLiteral("   ");
    config.sourceById(12)->name=QStringLiteral("Bank two");
    AmcpClient client;
    SwitcherEngine engine(&config,&client);
    client.setReconnectIntervalMs(0);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QJsonArray labels;
    QTRY_VERIFY(([&]() {
        for(const auto& cmd:caspar.commands){auto scene=nativeScene(cmd);if(!scene.isEmpty()){labels=scene.value("labels").toArray();return true;}}
        return false;
    })());
    QStringList names;
    for (const auto& label : labels) names.append(label.toString());
    QCOMPARE(names.size(),24);
    QCOMPARE(names[0],QStringLiteral("1 - Camera A"));
    QCOMPARE(names[1],QStringLiteral("2 - Camera B"));
    QCOMPARE(names[2],QStringLiteral("3 - Camera C"));
    QCOMPARE(names[3],QStringLiteral("4 - SRC4"));
    QCOMPARE(names[11],QStringLiteral("12 - M/E 2 PROGRAM"));
    QCOMPARE(names[12],QStringLiteral("13 - Bank two"));
    QCOMPARE(names[23],QStringLiteral("24 - M/E 2 PROGRAM"));
    QCOMPARE(config.sourceName(1),QStringLiteral("2 - Camera B"));
    QCOMPARE(config.sourceName(2),QStringLiteral("3 -Camera C"));
    NativeMultiview mv;mv.names=names;mv.preview=0;mv.program=1;mv.ready=true;mv.meters.resize(33);
    for(auto& pair:mv.meters)pair={750,500};config.setMeters(true);
    mv.vacant=QVector<bool>(24,false);mv.vacant[7]=true;
    mv.clocks.resize(24);mv.clocks[0].elapsed=12.25;mv.clocks[0].duration=3600;
    const auto scene=mv.scene(config);QVERIFY(scene.value("nodes").toArray().size()>100);
    bool elapsed=false,remaining=false,unavailable=false;
    for(const auto& value:scene.value("nodes").toArray()){auto n=value.toObject();
        if(n.value("text")=="00:00:12")elapsed=n.value("font")=="mono"&&n.value("color")=="#ffffff";
        if(n.value("text")=="-00:59:47")remaining=n.value("font")=="mono"&&n.value("color")=="#ffd56a";
        if(n.value("text")=="NO SOURCE")unavailable=true;
    }
    QVERIFY(elapsed);QVERIFY(remaining);QVERIFY(unavailable);
    const auto exportPath=qEnvironmentVariable("KAVTOR_TEST_SCENE");if(!exportPath.isEmpty()){QFile file(exportPath);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(scene).toJson());}

}

void TestSwitcherEngine::testCasparMixNativeWipesAndRoutes()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config,&client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(),0);
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(),0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(),0);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-1 route://1-1")));
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(),1);
    caspar.commands.clear();
    auto wipe = Transition::fromWipePattern(wipePatternById(QStringLiteral("smil_irisWipe_diamond")),15);
    wipe.edge = WipeEdgeMode::Soft;
    wipe.edgeAmount = 25;
    wipe.borderAmount = 10;
    QVERIFY(engine.setManualPosition(2000,wipe));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("CALL 10-1 \"PROGRESS 0.488400\"")));
    QVERIFY(std::any_of(caspar.commands.cbegin(),caspar.commands.cend(),[](const QString& cmd) {
        return cmd.startsWith(QStringLiteral("PLAY 10-1 route://2-1 WIPESONY 15 SONY 23")) &&
            cmd.contains(QStringLiteral("SOFT 25 BORDER 10")) && cmd.endsWith(QStringLiteral("MANUAL 1"));
    }));
    for(const auto& cmd:caspar.commands) QVERIFY(!cmd.contains(QStringLiteral("[HTML]")));
    QVERIFY(engine.setManualPosition(4095,wipe));
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),1);
    QCOMPARE(engine.previewSource(),0);
    caspar.commands.clear();
    QVERIFY(engine.setManualPosition(2000,wipe));
    QVERIFY(engine.setManualPosition(0,wipe));
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),1);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-1 route://2-1")));
    QVERIFY(engine.setTransitionPreview(true));
    QVERIFY(engine.setManualPosition(2000,wipe));
    QTRY_COMPARE(client.queuedCommandCount(),0);
    caspar.commands.clear();
    config.setWipeEdgeMode(WipeEdgeMode::Soft);
    config.setWipeEdgeAmount(32);
    QTRY_VERIFY(std::any_of(caspar.commands.cbegin(),caspar.commands.cend(),[](const QString& cmd) {
        return cmd.startsWith(QStringLiteral("CALL 9-101 ")) && cmd.contains(QStringLiteral("SOFT 32"))
            && cmd.contains(QStringLiteral("PROGRESS 0.488400"));
    }));
    for(const auto& cmd:caspar.commands) QVERIFY(!cmd.startsWith(QStringLiteral("PLAY ")));
    config.setWipeEdgeAmount(0);
    QTRY_VERIFY(std::any_of(caspar.commands.cbegin(),caspar.commands.cend(),[](const QString& cmd) {
        return cmd.startsWith(QStringLiteral("CALL 9-101 ")) && cmd.contains(QStringLiteral("SOFT 0"));
    }));
    QVERIFY(engine.setManualPosition(0,wipe));
    QTRY_VERIFY(!engine.isBusy());
    auto timed=wipe;timed.durationFrames=100;
    engine.executeTransition(timed);
    QTRY_COMPARE(client.queuedCommandCount(),0);
    caspar.commands.clear();
    config.setWipeEdgeAmount(20);
    QTRY_VERIFY(std::any_of(caspar.commands.cbegin(),caspar.commands.cend(),[](const QString& cmd) {
        return cmd.startsWith(QStringLiteral("CALL 9-101 ")) && cmd.contains(QStringLiteral("SOFT 20"))
            && !cmd.contains(QStringLiteral("PROGRESS"));
    }));
    QTRY_VERIFY(!engine.isBusy());
    QVERIFY(engine.setTransitionPreview(false));
    engine.setActiveMe(1);
    QTRY_COMPARE(engine.activeMe(),1);
    engine.selectPreview(2);
    QTRY_COMPARE(engine.previewSource(),2);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(),2);
    engine.setActiveMe(0);
    QTRY_COMPARE(engine.activeMe(),0);
    caspar.commands.clear();
    engine.selectPreview(11);
    QTRY_COMPARE(engine.previewSource(),11);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-1 route://13 RENDERED")));
}

void TestSwitcherEngine::testManualTransition()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(),0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(),0);
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(),1);
    QVERIFY(!engine.setManualPosition(4096,Transition::mix(0)));
    QVERIFY(engine.setManualPosition(1000,Transition::mix(0)));
    QVERIFY(engine.isManualTransition());
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-3 OPACITY 0.244")));
    QCOMPARE(engine.programSource(),0);
    engine.setActiveMe(1); // Freeze M/E 1 without committing its buses.
    QTRY_COMPARE(engine.activeMe(),1);
    QVERIFY(!engine.isManualTransition());
    QCOMPARE(engine.programSource(0),0);
    engine.setActiveMe(0);
    QTRY_COMPARE(engine.activeMe(),0);
    QVERIFY(engine.isManualTransition());
    QCOMPARE(engine.manualPosition(),1000);
    QVERIFY(engine.setManualPosition(2800,Transition::mix(0)));
    QVERIFY(engine.setManualPosition(1800,Transition::mix(0)));
    QVERIFY(engine.setManualPosition(0,Transition::mix(0)));
    QVERIFY(engine.setManualPosition(2000,Transition::mix(0))); // Endpoint is latched.
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),0);
    QCOMPARE(engine.previewSource(),1);
    caspar.commands.clear();
    QVERIFY(engine.setManualPosition(2000,Transition::mix(0)));
    QVERIFY(engine.setManualPosition(4095,Transition::mix(0)));
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),1);
    QCOMPARE(engine.previewSource(),0);
    const int swap = caspar.commands.indexOf(QStringLiteral("SWAP 10-1 10-3 TRANSFORMS"));
    QVERIFY(swap >= 0);
    QVERIFY(caspar.commands.indexOf(QStringLiteral("MIXER 10-1 OPACITY 0.000")) < swap);
    QVERIFY(caspar.commands.indexOf(QStringLiteral("MIXER 10-1 VOLUME 0.000")) < swap);
    QVERIFY(caspar.commands.indexOf(QStringLiteral("CLEAR 10-3"),swap) > swap);
    for (const QString& command : caspar.commands)
        QVERIFY(!command.startsWith(QStringLiteral("PLAY 10-1 ")));

    QVERIFY(engine.setManualPosition(1000,Transition::mix(0))); // Opposite physical stroke, normalized.
    QVERIFY(engine.setManualPosition(4095,Transition::mix(0)));
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),0);
    QCOMPARE(engine.previewSource(),1);
    engine.setActiveMe(1);
    engine.setActiveMe(0);
    QVERIFY(!engine.isManualTransition()); // No stale frozen bank after completion.
}

void TestSwitcherEngine::testManualHandoffFailure()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config,&client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0); QTRY_COMPARE(engine.previewSource(),0);
    engine.executeCut(); QTRY_COMPARE(engine.programSource(),0);
    engine.selectPreview(1); QTRY_COMPARE(engine.previewSource(),1);
    QVERIFY(engine.setManualPosition(2048,Transition::mix(0)));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-3 OPACITY 0.500")));
    QTRY_COMPARE(client.queuedCommandCount(),0);
    caspar.commands.clear();
    caspar.failuresRemaining[QStringLiteral("SWAP 10-1 10-3 TRANSFORMS")]=1;
    QVERIFY(engine.setManualPosition(4095,Transition::mix(0)));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("SWAP 10-1 10-3 TRANSFORMS")));
    QTRY_COMPARE(client.queuedCommandCount(),0);
    QCOMPARE(engine.programSource(),0);
    QCOMPARE(engine.previewSource(),1);
    // A rejected promotion must retain the visible incoming layer, rather than
    // clearing the only picture which is still known to be running.
    QVERIFY(!caspar.commands.contains(QStringLiteral("CLEAR 10-3")));
}

void TestSwitcherEngine::testManualDelegationAndOwnership()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient amcp;
    amcp.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &amcp);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);

    caspar.responseDelayMs = 10;
    QVERIFY(engine.setManualPosition(1000, Transition::mix(0)));
    engine.setActiveMe(1);
    engine.setActiveMe(0); // Cancel the queued delegation before its batch ACK.
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-3 OPACITY 0.244")));
    QTest::qWait(200);
    QCOMPARE(engine.activeMe(), 0);
    QVERIFY(engine.hasManualTransitions());
    QVERIFY(engine.setManualPosition(0, Transition::mix(0)));
    QTRY_VERIFY(!engine.isBusy());
    QVERIFY(!engine.hasManualTransitions());
    caspar.responseDelayMs = 0;

    PanelProtocol panel(&engine);
    QVERIFY(panel.start(0));
    QTcpSocket first, second;
    first.connectToHost(QHostAddress::LocalHost, panel.port());
    second.connectToHost(QHostAddress::LocalHost, panel.port());
    QTRY_VERIFY(first.canReadLine());
    QTRY_VERIFY(second.canReadLine());
    first.readAll(); second.readAll();
    const QByteArray move = "{\"cmd\":\"manual\",\"type\":\"mix\",\"position\":1000}\n";
    first.write(move);
    QTRY_VERIFY(engine.isManualTransition());
    engine.setActiveMe(1);
    QTRY_COMPARE(engine.activeMe(), 1);
    QVERIFY(!engine.isManualTransition());
    QVERIFY(engine.hasManualTransitions()); // Ownership survives a frozen M/E.
    second.readAll();
    second.write(move);
    QByteArray reply;
    QTRY_VERIFY((reply += second.readAll()).contains("owned by another panel"));
    engine.setActiveMe(0);
    QTRY_COMPARE(engine.activeMe(), 0);
    first.write("{\"cmd\":\"manual\",\"type\":\"mix\",\"position\":0}\n");
    QTRY_VERIFY(!engine.hasManualTransitions());
    second.readAll();
    second.write(move); // The first panel is still connected, but no longer owns the lever.
    QTRY_VERIFY(engine.isManualTransition());
    reply.clear();
    QTRY_VERIFY((reply += second.readAll()).contains("\"event\":\"ack\""));
    QVERIFY(!reply.contains("owned by another panel"));
    second.disconnectFromHost();
    QTRY_VERIFY(!engine.hasManualTransitions());
    QCOMPARE(engine.programSource(), 0);
    QCOMPARE(engine.previewSource(), 1);
}

void TestSwitcherEngine::testManualKeyMixRestoresAudio()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient amcp;
    amcp.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &amcp);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.setKeySource(0, 0);
    engine.setKeySource(1, 1);
    QTRY_VERIFY(!engine.isBusy());
    engine.toggleNextKey(0);
    engine.toggleNextKey(1);
    QVERIFY(engine.nextKey(0));
    QVERIFY(engine.nextKey(1));
    QVERIFY(engine.setManualPosition(2048, Transition::mix(0)));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-11 VOLUME 0.500")));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-12 VOLUME 0.500")));
    engine.toggleNextKey(0); // NEXT cannot mutate an in-flight take.
    QVERIFY(engine.nextKey(0));
    QVERIFY(engine.setManualPosition(4095, Transition::mix(0)));
    QTRY_VERIFY(!engine.isBusy());
    QVERIFY(engine.keyOn(0));
    QVERIFY(engine.keyOn(1));
    QCOMPARE(engine.programSource(), 1);
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-3 VOLUME 1.000")));
    QVERIFY(caspar.commands.contains(QStringLiteral("SWAP 10-1 10-3 TRANSFORMS")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-11 VOLUME 1.000")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-12 VOLUME 1.000")));
    caspar.commands.clear();
    QVERIFY(engine.setManualPosition(2048, Transition::mix(0)));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-11 VOLUME 0.500")));
    QVERIFY(engine.setManualPosition(0, Transition::mix(0)));
    QTRY_VERIFY(!engine.isBusy());
    QVERIFY(engine.keyOn(0));
    QVERIFY(engine.keyOn(1));
    QCOMPARE(engine.programSource(), 1);
    QCOMPARE(engine.previewSource(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-1 VOLUME 1.000")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-11 VOLUME 1.000")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-12 VOLUME 1.000")));
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 10-3")));
}

void TestSwitcherEngine::testKeyProcessingValidationAndPersistence()
{
    KeyProcessing settings;
    const auto original=settings.toJson();
    QVERIFY(!settings.update({{"mode","pattern"},{"mask",true}}));
    QVERIFY(!settings.update({{"lumaLow",0.8},{"lumaHigh",0.2}}));
    QVERIFY(!settings.update({{"invert",1}}));
    QVERIFY(!settings.update({{"left",0.8},{"right",0.2}}));
    QVERIFY(!settings.update({{"hue",361}}));
    QVERIFY(!settings.update({{"mask",1}}));
    QVERIFY(!settings.update({{"softness",-0.1}}));
    QVERIFY(!settings.update({{"unsupported",true}}));
    QCOMPARE(settings.toJson(),original);
    QVERIFY(settings.update({{"mode","chroma"},{"mask",true},{"left",0.2},{"right",0.8},{"hue",240}}));
    const auto commands=settings.commands(10,11);
    QVERIFY(commands[0].startsWith("MIXER 10-11 CHROMA 1 240.0000"));
    QCOMPARE(commands[1],QStringLiteral("MIXER 10-11 CLIP 0.2000 0.1000 0.6000 0.8000"));
    Configuration config;
    config.setKeyProcessing(2,3,false,settings);
    config.setKeyProcessing(0,1,true,settings);
    Configuration copy;
    copy.fromJson(config.toJson());
    QCOMPARE(copy.keyProcessing(2,3).toJson(),settings.toJson());
    QCOMPARE(copy.keyProcessing(0,1,true).toJson(),settings.toJson());
    QCOMPARE(copy.keyProcessing(0,3).mode,QStringLiteral("linear"));
    QCOMPARE(copy.keyProcessing(0,0,true).mode,QStringLiteral("linear"));
    const auto serialized=copy.toJson();
    Configuration legacy;
    auto older=serialized;older.remove("meKeyers");older.remove("dsk");
    legacy.fromJson(older);
    QCOMPARE(legacy.keyProcessing(2,3).mode,QStringLiteral("linear"));
    QVERIFY(!legacy.keyProcessing(0,1,true).mask);
}

void TestSwitcherEngine::testKeyProcessingConfirmationAndRoutes()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    AmcpClient amcp;amcp.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config,&amcp);engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());
    KeyProcessing chroma;QVERIFY(chroma.update({{"mode","chroma"},{"mask",true}}));
    caspar.responseDelayMs=10;
    QVERIFY(engine.setKeyProcessing(0,false,chroma));
    QCOMPARE(config.keyProcessing(0,0).mode,QStringLiteral("linear")); // Await all renderer ACKs.
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(config.keyProcessing(0,0).toJson(),chroma.toJson());
    QVERIFY(caspar.commands.contains(chroma.commands(10,11)[0]));
    caspar.responseDelayMs=0;
    KeyProcessing replacement=chroma;replacement.hue=240;
    caspar.failuresRemaining[replacement.commands(9,11)[0]]=1;
    QVERIFY(engine.setKeyProcessing(0,false,replacement));
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(config.keyProcessing(0,0).hue,120.0); // A rejected batch cannot publish new settings.
    engine.setKeySource(0,0);QTRY_VERIFY(!engine.isBusy());
    engine.toggleNextKey(0);
    QTRY_VERIFY(caspar.commands.contains(chroma.commands(9,11)[0]));
    engine.toggleNextBackground();
    caspar.commands.clear();
    engine.executeTransition(Transition::mix(1));QTRY_VERIFY(!engine.isBusy());
    QVERIFY(engine.keyOn(0));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-11 CHROMA 0")));
    QVERIFY(!caspar.commands.contains(chroma.commands(10,11)[0])); // Preview route is already keyed.
    QVERIFY(engine.setKeyProcessing(0,false,replacement));QTRY_VERIFY(!engine.isBusy());
    QVERIFY(caspar.commands.contains(replacement.commands(9,11)[0]));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-11 CLIP 0.0000 0.0000 1.0000 1.0000")));
    engine.setActiveMe(1);QTRY_COMPARE(engine.activeMe(),1);
    QCOMPARE(config.keyProcessing(1,0).mode,QStringLiteral("linear"));
    QVERIFY(engine.setKeyProcessing(1,true,chroma));QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(config.keyProcessing(0,1,true).toJson(),chroma.toJson());
    QVERIFY(caspar.commands.contains(chroma.commands(12,21)[0]));
}

void TestSwitcherEngine::testNativeMixModes()
{
    FakeCaspar caspar;Configuration config;
    config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    AmcpClient amcp;amcp.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&amcp);
    engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
    engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
    Transition transition;
    QVERIFY(!Transition::namedMix("alphafade",1,&transition));
    QVERIFY(!Transition::namedMix("lumafade",1,&transition));
    for(const auto& mode:{QStringLiteral("dip"),QStringLiteral("vfade"),QStringLiteral("fadecut"),QStringLiteral("cutfade")}) {
        QVERIFY(Transition::namedMix(mode,1,&transition));
        const int incoming=engine.previewSource();
        config.setDipColor("#21ABCD");
        const QString expected=QStringLiteral("PLAY 10-1 route://%1-1 %2 1").arg(incoming+1).arg(mode == "dip" ? "VFADE" : mode.toUpper());
        engine.executeTransition(transition);QTRY_VERIFY(!engine.isBusy());
        QCOMPARE(engine.programSource(),incoming);QVERIFY(caspar.commands.contains(expected));
        if (mode == "dip") {
            QVERIFY(caspar.commands.contains("PLAY 10-0 #21ABCD"));
            QTRY_VERIFY(caspar.commands.contains("CLEAR 10-0"));
        }
        transition.durationFrames=0;
        caspar.commands.clear();
        QVERIFY(engine.setManualPosition(2048,transition));
        const QString out=QStringLiteral("MIXER 10-1 OPACITY %1").arg(mode=="fadecut"?"0.500":"0.000");
        QTRY_VERIFY(caspar.commands.contains(out));
        if (mode == "dip") {
            QVERIFY(caspar.commands.contains("PLAY 10-0 #21ABCD"));
            config.setDipColor("#FF0000");
            QVERIFY(engine.setManualPosition(3000,transition));
            QTRY_VERIFY(caspar.commands.contains("MIXER 10-3 OPACITY 0.465"));
            QVERIFY(!caspar.commands.contains("PLAY 10-0 #FF0000"));
        }
        const QString in=QStringLiteral("MIXER 10-3 OPACITY %1").arg(mode=="cutfade"?"0.500":"0.000");
        if (mode != "dip") QTRY_VERIFY(caspar.commands.contains(in));
        QVERIFY(engine.setManualPosition(0,transition));QTRY_VERIFY(!engine.isBusy());
        QCOMPARE(engine.programSource(),incoming);
        QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-1 OPACITY 1.000")));
        QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-1 VOLUME 1.000")));
        QVERIFY(engine.setManualPosition(1000,transition));QVERIFY(engine.setManualPosition(4095,transition));
        const int target=engine.previewSource();QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),target);
    }
    Transition dip;
    QVERIFY(Transition::namedMix("dip", 1, &dip));
    config.setDipColor("#21ABCD");
    QVERIFY(engine.setTransitionPreview(true));
    const int originalProgram = engine.programSource();
    caspar.commands.clear();
    engine.executeTransition(dip); QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(), originalProgram);
    QVERIFY(caspar.commands.contains("PLAY 9-100 #21ABCD"));
    QVERIFY(!caspar.commands.contains("PLAY 10-0 #21ABCD"));
    QTRY_VERIFY(caspar.commands.contains("CLEAR 9-100"));
    caspar.commands.clear();
    QVERIFY(engine.setManualPosition(2048, dip));
    QTRY_VERIFY(caspar.commands.contains("PLAY 9-100 #21ABCD"));
    QVERIFY(engine.setManualPosition(0, dip)); QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(), originalProgram);
    dip.dipColor = "#NOTRGB";
    caspar.commands.clear();
    QVERIFY(!engine.setManualPosition(2048, dip));
    engine.executeTransition(dip);
    QVERIFY(!engine.isBusy());
    QVERIFY(!caspar.commands.contains("PLAY 9-100 #NOTRGB"));
    QVERIFY(engine.setTransitionPreview(true));
    const int saved=engine.programSource();
    caspar.commands.clear();
    QVERIFY(engine.setManualPosition(2048,transition));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-100 #000000")));
    QVERIFY(engine.setManualPosition(4095,transition));QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),saved);
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 9-100")));
    QVERIFY(engine.setTransitionPreview(false));
    engine.setKeySource(0,0);QTRY_VERIFY(!engine.isBusy());engine.toggleNextKey(0);
    QVERIFY(!engine.setManualPosition(1000,transition)); // Do not cut armed keys during a black fade.
}

void TestSwitcherEngine::testDmeDirectionsAndValidation()
{
    FakeCaspar caspar;Configuration config;
    config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());config.setAutoDurationFrames(1);
    AmcpClient amcp;amcp.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&amcp);
    engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
    engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
    PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket peer;
    peer.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(peer.canReadLine());peer.readAll();
    QByteArray response;
    peer.write("{\"cmd\":\"dme\",\"effect\":\"unknown\",\"direction\":\"left\"}\n");
    QTRY_VERIFY((response+=peer.readAll()).contains("Invalid DME"));QCOMPARE(engine.programSource(),0);
    for(const auto& effect:{QStringLiteral("push"),QStringLiteral("slide")})for(const auto& direction:{QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("top"),QStringLiteral("bottom")}) {
        const int source=engine.previewSource();const int before=caspar.commands.size();
        QJsonObject request{{"cmd","dme"},{"effect",effect},{"direction",direction}};
        peer.write(QJsonDocument(request).toJson(QJsonDocument::Compact)+"\n");
        const QString expected=QStringLiteral("PLAY 10-1 route://%1-1 %2 1 FROM%3").arg(source+1).arg(effect.toUpper(),direction.toUpper());
        QTRY_VERIFY(caspar.commands.mid(before).contains(expected));
        QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),source);
    }
    peer.readAll();response.clear();peer.write("{\"cmd\":\"mix\",\"mode\":true}\n");
    QTRY_VERIFY((response+=peer.readAll()).contains("MIX mode must be a string"));
}

void TestSwitcherEngine::testSonyWipeNamespace()
{
    WipePattern pattern;bool reverse=false;
    QVERIFY(lookupWipeBySony(23,&pattern,&reverse));QCOMPARE(pattern.smilType,QStringLiteral("irisWipe"));QCOMPARE(pattern.smilSubtype,QStringLiteral("diamond"));QVERIFY(!reverse);
    QVERIFY(lookupWipeBySony(24,&pattern,&reverse));QCOMPARE(pattern.smilSubtype,QStringLiteral("circle"));
    QVERIFY(lookupWipeBySony(18,&pattern,&reverse));QCOMPARE(pattern.smilSubtype,QStringLiteral("horizontal"));QVERIFY(reverse);
    QVERIFY(lookupWipeBySony(17,&pattern,&reverse));QCOMPARE(pattern.smilSubtype,QStringLiteral("vertical"));QVERIFY(reverse);
    QVERIFY(lookupWipeBySony(5,&pattern,&reverse));QCOMPARE(pattern.smilSubtype,QStringLiteral("topLeft"));
    QVERIFY(lookupWipeBySony(6,&pattern,&reverse));QCOMPARE(pattern.smilSubtype,QStringLiteral("topRight"));
    QVERIFY(lookupWipeBySony(9,&pattern,&reverse));QCOMPARE(pattern.smilType,QStringLiteral("diagonalWipe"));
    QVERIFY(lookupWipeBySmpte(23,&pattern,&reverse));QCOMPARE(pattern.smilSubtype,QStringLiteral("topCenter")); // Legacy namespace unchanged.
    QVERIFY(!lookupWipeBySony(999,&pattern,&reverse));
    FakeCaspar caspar;Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    AmcpClient amcp;amcp.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&amcp);engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
    PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket peer;peer.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_VERIFY(peer.canReadLine());peer.readAll();
    peer.write("{\"cmd\":\"manual\",\"type\":\"wipe\",\"sony\":23,\"position\":2000}\n");
    QTRY_VERIFY(engine.isManualTransition());
    bool diamond=false;QTRY_VERIFY(([&]{for(const auto& command:caspar.commands)if(command.contains("WIPESONY 1 SONY 23"))diamond=true;return diamond;})());
    peer.write("{\"cmd\":\"manual\",\"type\":\"wipe\",\"sony\":23,\"position\":0}\n");QTRY_VERIFY(!engine.isBusy());
    peer.readAll();QByteArray reply;
    peer.write("{\"cmd\":\"wipe\",\"sony\":23,\"smpte\":23}\n");
    QTRY_VERIFY((reply+=peer.readAll()).contains("Unknown or invalid Sony wipe"));QCOMPARE(engine.programSource(),0);
}

void TestSwitcherEngine::testManualWipeAndPreview()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config,&client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(),0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(),0);
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(),1);
    auto wipe=Transition::fromWipePattern(wipePatternById(QStringLiteral("smil_irisWipe_diamond")),0);
    wipe.edge = WipeEdgeMode::Soft;
    wipe.edgeAmount = 8;
    wipe.borderAmount = 6;
    wipe.shadowAmount = 3;
    QVERIFY(engine.setManualPosition(2000,wipe));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-6 KEYER 1")));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-6 #000000")));
    const int guard=caspar.commands.indexOf(QStringLiteral("PLAY 10-6 #000000"));
    const auto mask=std::find_if(caspar.commands.cbegin(),caspar.commands.cend(),[](const QString& command) {
        return command.startsWith(QStringLiteral("PLAY 10-6 route://10-90 "));
    });
    QVERIFY(mask!=caspar.commands.cend());
    QVERIFY(mask->endsWith(QStringLiteral(" MIX 1")));
    QVERIFY(std::distance(caspar.commands.cbegin(),mask)>guard);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("CALL 10-90 \"setProgress(0.488400)\"")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("CALL 10-15 \"setProgress(0.488400)\"")));
    QVERIFY(std::any_of(caspar.commands.cbegin(), caspar.commands.cend(), [](const QString& cmd) {
        return cmd.startsWith("PLAY 10-90 [HTML]") && cmd.contains("autorun=0") && cmd.contains("shadow=3");
    }));
    QVERIFY(engine.setManualPosition(4095,wipe));
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),1);
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 10-15")));
    const int swap = caspar.commands.indexOf(QStringLiteral("SWAP 10-1 10-7 TRANSFORMS"));
    QVERIFY(swap >= 0);
    QVERIFY(caspar.commands.lastIndexOf(QStringLiteral("CLEAR 10-6"),swap) < swap);
    QVERIFY(caspar.commands.indexOf(QStringLiteral("CLEAR 10-7"),swap) > swap);
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-4 KEYER 1")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-4 route://10-90 BUFFER 1 MIX 1")));
    QVERIFY(caspar.commands.contains(QStringLiteral("SWAP 10-1 10-5 TRANSFORMS")));
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 10-4")));
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 10-5")));
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 10-90")));
    QVERIFY(engine.setTransitionPreview(true));
    QVERIFY(engine.setManualPosition(2000,Transition::mix(0)));
    QVERIFY(engine.setManualPosition(4095,Transition::mix(0)));
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(),1);
    QCOMPARE(engine.previewSource(),0);
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 9-103")));
}

void TestSwitcherEngine::testNotASingleton()
{
    Configuration configA;
    Configuration configB;
    AmcpClient clientA;
    AmcpClient clientB;
    SwitcherEngine engineA(&configA, &clientA);
    SwitcherEngine engineB(&configB, &clientB);
    QVERIFY(&engineA != &engineB);
}

void TestSwitcherEngine::testProducerCommands()
{
    Source decklink;
    decklink.type = SourceType::Decklink;
    decklink.argument = QStringLiteral("2");
    QCOMPARE(decklink.producerCommand(), QStringLiteral("DECKLINK DEVICE 2"));

    Source ndi;
    ndi.type = SourceType::Ndi;
    ndi.argument = QStringLiteral("CAM 1");
    QCOMPARE(ndi.producerCommand(), QStringLiteral("[NDI] \"CAM 1\""));
    Source ndiUrl;
    ndiUrl.type = SourceType::Ndi;
    ndiUrl.argument = QStringLiteral("ndi://studio/OBS");
    QCOMPARE(ndiUrl.producerCommand(), QStringLiteral("[NDI] \"studio (OBS)\""));
    Source ndiPrefixed;
    ndiPrefixed.type = SourceType::Ndi;
    ndiPrefixed.argument = QStringLiteral("[NDI] HOST (OBS)");
    QCOMPARE(ndiPrefixed.producerCommand(), QStringLiteral("[NDI] \"HOST (OBS)\""));
    const QStringList discovered = {
        QStringLiteral("CHARLIE (OBS Preview)"),
        QStringLiteral("CHARLIE (OBS)"),
        QStringLiteral("OPPO (NDI HX Camera)")
    };
    QCOMPARE(resolveNdiSourceName(QStringLiteral("OBS"), discovered),
             QStringLiteral("CHARLIE (OBS)"));
    QCOMPARE(resolveNdiSourceName(QStringLiteral("OPPO"), discovered),
             QStringLiteral("OPPO (NDI HX Camera)"));
    QCOMPARE(resolveNdiSourceName(QStringLiteral("NDI HX Camera"), discovered),
             QStringLiteral("OPPO (NDI HX Camera)"));
    QCOMPARE(ndiProducerCommand(QStringLiteral("OBS"), discovered),
             QStringLiteral("[NDI] \"CHARLIE (OBS)\""));
    QCOMPARE(parseNdiListLines({
                 QStringLiteral("200 NDI LIST OK"),
                 QStringLiteral("1 \"CHARLIE (OBS)\" 192.168.1.100:5961"),
                 QStringLiteral("2 \"OPPO (NDI HX Camera)\" 192.168.1.33:5961")
             }),
             QStringList({QStringLiteral("CHARLIE (OBS)"), QStringLiteral("OPPO (NDI HX Camera)")}));

    Source file;
    file.type = SourceType::File;
    file.argument = QStringLiteral("AMB");
    QVERIFY(file.producerCommand().startsWith(QStringLiteral("AMB LOOP VF ")));
    QVERIFY(file.producerCommand().contains(QStringLiteral("force_original_aspect_ratio=decrease")));
    QVERIFY(!file.producerCommand().contains(QStringLiteral("FILTER")));

    Source stream;
    stream.type = SourceType::Ffmpeg;
    stream.argument = QStringLiteral("ffmpeg://rtsp://127.0.0.1/live");
    QVERIFY(stream.producerCommand().startsWith(QStringLiteral("\"rtsp://127.0.0.1/live\" SEEKABLE 0 VF ")));
    QVERIFY(!stream.producerCommand().contains(QStringLiteral("ffmpeg://")));

    Source httpTs;
    httpTs.type = SourceType::Ffmpeg;
    httpTs.argument = QStringLiteral(
        "http://127.0.0.1:6878/ace/getstream?content_id=c53bdb3be55d12c5188d25085f1bb16911c15cea");
    const QString httpCmd = httpTs.producerCommand();
    QVERIFY(httpCmd.startsWith(QStringLiteral(
        "\"http://127.0.0.1:6878/ace/getstream?content_id=c53bdb3be55d12c5188d25085f1bb16911c15cea\" SEEKABLE 0 VF ")));
    QVERIFY(!httpCmd.contains(QStringLiteral("ffmpeg://")));
    QVERIFY(!httpCmd.contains(QStringLiteral("[HTML]")));

    Source still;
    still.type = SourceType::Still;
    still.argument = QStringLiteral("logo.png");
    QCOMPARE(still.producerCommand(), QStringLiteral("logo.png"));

    Source bars;
    bars.type = SourceType::ColorBars;
    bars.enabled = true;
    QCOMPARE(bars.producerCommand(),
             QStringLiteral("COLORBARS EBU75"));
    QVERIFY(bars.isAssigned());
    bars.argument = QStringLiteral("#ff0000");
    QCOMPARE(bars.producerCommand(), QStringLiteral("#FF0000"));
    bars.argument = QStringLiteral("hd");
    QCOMPARE(bars.producerCommand(), QStringLiteral("COLORBARS SMPTEHD"));
    bars.argument = QStringLiteral("pal"); QCOMPARE(bars.producerCommand(), QStringLiteral("COLORBARS EBU75"));
    bars.argument = QStringLiteral("ebu100"); QCOMPARE(bars.producerCommand(), QStringLiteral("COLORBARS EBU100"));
    bars.argument = QStringLiteral("smptesd"); QCOMPARE(bars.producerCommand(), QStringLiteral("COLORBARS SMPTESD"));
    bars.argument = QStringLiteral("invalid"); QVERIFY(bars.producerCommand().isEmpty());

    Source emptyFile;
    emptyFile.type = SourceType::File;
    emptyFile.enabled = true;
    QVERIFY(emptyFile.producerCommand().isEmpty());
    QVERIFY(!emptyFile.isAssigned());

    const QRectF pillar = letterboxFill(4.0 / 3.0, 16.0 / 9.0);
    QCOMPARE(pillar.y(), 0.0);
    QVERIFY(qAbs(pillar.x() - 0.125) < 1e-6);
    QVERIFY(qAbs(pillar.width() - 0.75) < 1e-6);
    QCOMPARE(pillar.height(), 1.0);

    const QRectF letter = letterboxFill(2.35, 16.0 / 9.0);
    QCOMPARE(letter.x(), 0.0);
    QCOMPARE(letter.width(), 1.0);
    QVERIFY(letter.y() > 0.0);
}

void TestSwitcherEngine::testLegacySourceMigration()
{
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    QJsonObject root;
    QJsonArray sources;
    QJsonObject src;
    src.insert(QStringLiteral("id"), 0);
    src.insert(QStringLiteral("name"), QStringLiteral("SDI 1"));
    src.insert(QStringLiteral("amcp"), QStringLiteral("DEVICE 1"));
    src.insert(QStringLiteral("enabled"), true);
    sources.append(src);
    root.insert(QStringLiteral("sources"), sources);
    QVERIFY(config.fromJson(root));
    QCOMPARE(config.sources().first().type, SourceType::Decklink);
    QCOMPARE(config.sources().first().argument, QStringLiteral("1"));
    QCOMPARE(config.sources().first().producerCommand(), QStringLiteral("DECKLINK DEVICE 1"));
}

void TestSwitcherEngine::testPreviewCutAndAuto()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(12);

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);

    QSignalSpy connectedSpy(&engine, &SwitcherEngine::connectionStatusChanged);
    QSignalSpy previewSpy(&engine, &SwitcherEngine::previewSourceChanged);
    QSignalSpy programSpy(&engine, &SwitcherEngine::programSourceChanged);

    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QCOMPARE(connectedSpy.last().at(0).toBool(), true);

    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    QVERIFY(previewSpy.count() >= 1);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-1 route://1-1")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("LOADBG 1-10 DEVICE 1")));

    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    QTRY_COMPARE(engine.previewSource(), 0);
    QVERIFY(programSpy.count() >= 1);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-1 route://1-1")));

    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.executeAuto();
    QTRY_VERIFY(engine.isTransitioning());
    QCOMPARE(engine.previewSource(), 1);
    QCOMPARE(engine.programSource(), 0);
    QTRY_VERIFY(hasNativeState(caspar.commands,{{"preview",1},{"program",0},{"bank",0},{"both",true}}));
    QTRY_COMPARE(engine.programSource(), 1);
    QTRY_COMPARE(engine.previewSource(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-1 route://2-1 MIX 12")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-1 route://1-1")));

    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    QTRY_VERIFY(!engine.isTransitioning());
    config.setWipePatternId(QStringLiteral("wipe_from_right"));
    config.setWipeDirectionMode(WipeDirectionMode::Reverse);
    engine.executeWipe();
    QTRY_COMPARE(engine.programSource(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-1 route://1-1 WIPE 12 FROMRIGHT")));
}

void TestSwitcherEngine::testSwapAndHotPunch()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);

    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 1);
    QTRY_COMPARE(engine.previewSource(), 0);

    engine.hotPunchProgram(2);
    QTRY_COMPARE(engine.programSource(), 2);
    QCOMPARE(engine.previewSource(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-1 route://3-1")));
}

void TestSwitcherEngine::testUnassignedSourceIsIgnored()
{
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    AmcpClient client;
    SwitcherEngine engine(&config, &client);
    QSignalSpy errors(&engine, &SwitcherEngine::error);

    QVERIFY(config.sourceById(6));
    QVERIFY(!config.sourceById(6)->isAssigned());
    engine.selectPreview(6);
    engine.hotPunchProgram(6);
    engine.selectPreview(99);
    engine.hotPunchProgram(99);

    QCOMPARE(engine.previewSource(), -1);
    QCOMPARE(engine.programSource(), -1);
    QCOMPARE(errors.count(), 0);
}

void TestSwitcherEngine::testDskCommands()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setDskSourceId(7);
    Source dsk = *config.sourceById(7);
    dsk.argument = QStringLiteral("logo.png");
    config.setSource(dsk);

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PLAY 11-10 route://12 RENDERED")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 11-9 route://9 RENDERED")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-1 route://10")));

    const int beforeOn = caspar.commands.size();
    engine.setDskOn(true);
    QTRY_VERIFY(engine.isDskOn());
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-20 route://8-1")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 10-20 route://8-1")));
    QVERIFY(!caspar.commands.mid(beforeOn).contains(QStringLiteral("CLEAR 9-20")));

    engine.setDskPreview(true);
    QTRY_VERIFY(engine.isDskPreview());
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-20 route://8-1")));

    engine.setDskOn(false);
    QTRY_VERIFY(!engine.isDskOn());
    QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 12-20")));
    QVERIFY(engine.isDskPreview());
}

void TestSwitcherEngine::testKeyersAndNextTransition()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    Source logo = *config.sourceById(7);
    logo.argument = QStringLiteral("logo.png");
    config.setSource(logo);

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    QSignalSpy errors(&engine, &SwitcherEngine::error);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.setKeySource(0, 0);
    QCOMPARE(config.keySource(0), 0);
    engine.setKeySource(0, 6);
    QCOMPARE(config.keySource(0), 0);
    QCOMPARE(errors.count(), 0);

    const auto sent = [&caspar](const QString& prefix) {
        for (const QString& command : caspar.commands) {
            if (command.startsWith(prefix)) {
                return true;
            }
        }
        return false;
    };
    engine.toggleNextKey(0);
    QVERIFY(engine.nextKey(0));
    QTRY_VERIFY(sent(QStringLiteral("PLAY 9-11 ")));
    engine.toggleNextBackground();
    QVERIFY(!engine.nextBackground());
    engine.toggleNextKey(0);
    QVERIFY(engine.nextKey(0));

    const int programBefore = engine.programSource();
    engine.executeCut();
    QTRY_VERIFY(engine.keyOn(0));
    QCOMPARE(engine.programSource(), programBefore);
    QVERIFY(sent(QStringLiteral("PLAY 10-11 ")));
    QCOMPARE(errors.count(), 0);

    engine.setDskSource(1, 7);
    engine.setDskSlot(1, true, 0);
    QTRY_VERIFY(engine.isDskOn(1));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-21 route://8-1")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-21 route://8-1")));
    QVERIFY(!engine.isDskOn());

    engine.setDskSlot(1, false, 12);
    QVERIFY(engine.isDskMixing(1));
    QTRY_VERIFY(!engine.isDskMixing(1));
    QVERIFY(!engine.isDskOn(1));

    engine.setDskSlot(1, true, 12);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-21 route://8-1 MIX 12 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-21 route://8-1 MIX 12 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 9-21 OPACITY 1.000")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-21 OPACITY 1.000")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("MIXER 12-21 OPACITY 0.000")));

    engine.setKeySource(1, 0);
    engine.toggleNextKey(1);
    engine.toggleNextKey(0);
    QVERIFY(!engine.nextKey(0));
    QVERIFY(engine.nextKey(1));
    QTRY_VERIFY(sent(QStringLiteral("PLAY 9-12 ")));
    engine.executeAuto();
    QTRY_VERIFY(engine.keyOn(1));
    QTRY_VERIFY(!engine.isTransitioning());
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-12 route://9-12 MIX 25 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-12 OPACITY 1.000")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 9-12 OPACITY 0.000")));
    engine.setKeyOn(1, false);
    QTRY_VERIFY(!engine.keyOn(1));
    engine.setKeyOn(1, true);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-12 route://9-12")));
    QTRY_VERIFY(engine.keyOn(1));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 10-12 OPACITY 1.000")));
    const int mixAt = caspar.commands.lastIndexOf(QStringLiteral("PLAY 10-12 route://9-12 MIX 25 LINEAR"));
    QVERIFY(mixAt >= 0);
    QVERIFY(!caspar.commands.mid(mixAt).contains(QStringLiteral("CLEAR 9-12")));
}

void TestSwitcherEngine::testIncrementalSourceArm()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    const auto playCount = [&caspar](const QString& prefix) {
        int count = 0;
        for (const QString& command : caspar.commands) {
            if (command.startsWith(prefix)) {
                ++count;
            }
        }
        return count;
    };

    QTRY_VERIFY(playCount(QStringLiteral("PLAY 1-1 ")) >= 1);
    const int firstClipPlays = playCount(QStringLiteral("PLAY 1-1 "));

    engine.armMixer();
    QTRY_COMPARE(playCount(QStringLiteral("PLAY 1-1 ")), firstClipPlays);

    Source clip = *config.sourceById(1);
    clip.argument = QStringLiteral("OTHER");
    config.setSource(clip);
    engine.armMixer();
    QTRY_VERIFY(playCount(QStringLiteral("PLAY 2-1 ")) >= 2);
    QCOMPARE(playCount(QStringLiteral("PLAY 1-1 ")), firstClipPlays);

    clip.enabled = false;
    config.setSource(clip);
    engine.armMixer();
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("STOP 2-1")));
}

void TestSwitcherEngine::testStillMixerFill()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("still43.png"));
    QImage image(400, 300, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QVERIFY(image.save(path, "PNG"));

    Source still;
    still.type = SourceType::Still;
    still.argument = path;
    QVERIFY(still.producerCommand().contains(QLatin1Char('"')));
    QVERIFY(!still.producerCommand().contains(QStringLiteral("[IMAGE]")));

    const QRectF fill = still.mixerFill(1920, 1080);
    QVERIFY(qAbs(fill.x() - 0.125) < 1e-3);
    QCOMPARE(fill.y(), 0.0);
    QVERIFY(qAbs(fill.width() - 0.75) < 1e-3);
    QCOMPARE(fill.height(), 1.0);
}

namespace {

QByteArray oscPad(const QByteArray& text)
{
    QByteArray out = text;
    out.append('\0');
    while (out.size() % 4) {
        out.append('\0');
    }
    return out;
}

QByteArray oscFloat(float value)
{
    quint32 bits = 0;
    memcpy(&bits, &value, 4);
    bits = qToBigEndian(bits);
    return QByteArray(reinterpret_cast<const char*>(&bits), 4);
}

QByteArray oscInt32(qint32 value)
{
    const qint32 be = qToBigEndian(value);
    return QByteArray(reinterpret_cast<const char*>(&be), 4);
}

QByteArray oscFileTimeMessage(int channel, float elapsed, float duration)
{
    const QByteArray address = QStringLiteral("/channel/%1/stage/layer/1/foreground/file/time")
                                   .arg(channel)
                                   .toLatin1();
    return oscPad(address) + oscPad(",ff") + oscFloat(elapsed) + oscFloat(duration);
}

QByteArray oscBundle(const QByteArray& message)
{
    return oscPad("#bundle") + QByteArray(8, '\0') + oscInt32(message.size()) + message;
}

} // namespace

void TestSwitcherEngine::testOscFileTimeAndOverlayClock()
{
    QCOMPARE(formatClipClock(3661), QStringLiteral("01:01:01"));
    QCOMPARE(formatClipClock(-3), QStringLiteral("00:00:00"));

    const QByteArray packet = oscBundle(oscFileTimeMessage(1, 12.25f, 3600.0f));
    const QVector<OscFileTime> times = parseOscFileTimes(packet);
    QCOMPARE(times.size(), 1);
    QCOMPARE(times.first().channel, 1);
    QCOMPARE(times.first().layer, 1);
    QVERIFY(qAbs(times.first().elapsed - 12.25) < 0.01);
    QVERIFY(qAbs(times.first().duration - 3600.0) < 0.01);

    QVector<OscFileTime> clocks(8);
    clocks[0] = times.first();
    QCOMPARE(meterUnit(0), 0);
    QCOMPARE(meterUnit(-1), 0);
    QCOMPARE(meterUnit(std::numeric_limits<qint32>::max()), 1000);
    const QByteArray audio = oscPad(QByteArrayLiteral("/channel/9/mixer/audio/volume"))
        + oscPad(",ii")
        + oscInt32(std::numeric_limits<qint32>::max())
        + oscInt32(0);
    const QByteArray mixed = packet + oscInt32(audio.size()) + audio;
    const OscDatagram datagram = parseOscDatagram(mixed);
    QCOMPARE(datagram.times.size(), 1);
    QCOMPARE(datagram.peaks.size(), 1);
    QCOMPARE(datagram.peaks.first().channel, 9);
    QCOMPARE(datagram.peaks.first().left, std::numeric_limits<qint32>::max());
    QCOMPARE(datagram.peaks.first().right, 0);
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.ingestFileTime(1, 1, 12.25, 3600.0);
    engine.flushOverlayClocks();
    QTRY_VERIFY(([&](){for(const auto& cmd:caspar.commands){auto scene=nativeScene(cmd);if(scene.isEmpty())continue;
        auto json=QJsonDocument(scene).toJson();if(json.contains("M/E 2 PROGRAM")&&json.contains("00:00:12"))return true;
    }return false;})());
    engine.flushOverlayClocks();

}

void TestSwitcherEngine::testSmilHtmlWipeCommand()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(12);
    config.setWipePatternId(QStringLiteral("smil_clockWipe_clockwiseTwelve"));

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeWipe();
    QTRY_COMPARE(engine.programSource(), 0);

    QString sting;
    for (const QString& command : caspar.commands) {
        if (command.contains(QLatin1String(" STING "))) {
            sting = command;
            break;
        }
    }
    QVERIFY(sting.startsWith(QStringLiteral("PLAY 10-1 route://1-1 STING (MASK=\"[HTML] ")));
    QVERIFY(sting.contains(QStringLiteral("wipe-matte.html")));
    QVERIFY(sting.contains(QStringLiteral("type=clockWipe")));
    QVERIFY(sting.contains(QStringLiteral("subtype=clockwiseTwelve")));
    QVERIFY(sting.contains(QStringLiteral("sense=fwd")));
    QVERIFY(sting.contains(QStringLiteral("autorun=1")));
    QVERIFY(sting.contains(QStringLiteral("t0=")));
    QVERIFY(sting.contains(QStringLiteral("audio_fade_duration=12")));

    QTRY_VERIFY(!engine.isTransitioning());
    engine.setKeySource(1, 1);
    engine.setKeyOn(1, true);
    QTRY_VERIFY(engine.keyOn(1));
    engine.setKeySource(0, 0);
    engine.toggleNextKey(0);
    const int beforeKeyWipe = caspar.commands.size();
    engine.executeWipe();
    QTRY_VERIFY(!engine.isTransitioning());
    bool enteringSting = false;
    bool stayingSting = false;
    bool transparentSeed = false;
    bool quotedHtml = false;
    for (int i = beforeKeyWipe; i < caspar.commands.size(); ++i) {
        const QString command = caspar.commands.at(i);
        if (command.startsWith(QStringLiteral("PLAY 10-11 route://9-11 ")) && command.contains(QLatin1String(" STING "))) {
            enteringSting = true;
        }
        if (command==QStringLiteral("PLAY 10-11 #00000000")) {
            transparentSeed = true;
        }
        if (command.contains(QStringLiteral(" \"[HTML] "))) {
            quotedHtml = true;
        }
        if (command.startsWith(QStringLiteral("PLAY 10-12 ")) && command.contains(QLatin1String(" STING "))) {
            stayingSting = true;
        }
    }
    QVERIFY(enteringSting);
    QVERIFY(transparentSeed);
    QVERIFY(!quotedHtml);
    QVERIFY(!stayingSting);
}

void TestSwitcherEngine::testSmpteWipeCatalog()
{
    const QVector<WipePattern> patterns = builtinWipePatterns();
    QCOMPARE(patterns.size(), 208);

    int smilCount = 0;
    QSet<int> codes;
    QSet<QString> ids;
    for (const WipePattern& pattern : patterns) {
        QVERIFY(!pattern.id.isEmpty());
        QVERIFY(ids.contains(pattern.id) == false);
        ids.insert(pattern.id);
        if (pattern.type != TransitionType::Smil) {
            QVERIFY(pattern.smpte == 0);
            QVERIFY(!pattern.label.startsWith(QStringLiteral("SMPTE ")));
            continue;
        }
        if (pattern.smilType == QStringLiteral("kavtorWipe") || pattern.smilType == QStringLiteral("sonyWipe")) {
            QCOMPARE(pattern.smpte, 0);
            QVERIFY(pattern.label.startsWith(QStringLiteral("kavtor — ")) || (!pattern.implemented&&pattern.label.startsWith(QStringLiteral("Sony "))));
            continue;
        }
        ++smilCount;
        QVERIFY(pattern.smpte > 0);
        QVERIFY(codes.contains(pattern.smpte) == false);
        codes.insert(pattern.smpte);
        if (pattern.smpteReverse > 0) {
            QVERIFY(pattern.label.startsWith(QStringLiteral("SMPTE %1/%2 — ").arg(pattern.smpte).arg(pattern.smpteReverse)));
            QVERIFY(codes.contains(pattern.smpteReverse) == false);
            codes.insert(pattern.smpteReverse);
        } else {
            QVERIFY(pattern.label.startsWith(QStringLiteral("SMPTE %1 — ").arg(pattern.smpte)));
        }
        QVERIFY(!pattern.smilType.isEmpty());
        QVERIFY(!pattern.smilSubtype.isEmpty());
    }
    QCOMPARE(smilCount, 100);

    const WipePattern clock = wipePatternById(QStringLiteral("smil_clockWipe_clockwiseTwelve"));
    QCOMPARE(clock.smpte, 201);
    QCOMPARE(clock.label, QStringLiteral("SMPTE 201 — Clock twelve"));

    const WipePatternLookup box = lookupWipePattern(QStringLiteral("smil_boxWipe_bottomRight"));
    QCOMPARE(box.pattern.id, QStringLiteral("smil_boxWipe_bottomRight"));
    QCOMPARE(box.pattern.smpte, 5);
    QCOMPARE(box.pattern.smpteReverse, 0);
    QVERIFY(!box.reverse);

    QCOMPARE(wipePatternById(QStringLiteral("wipe_from_right")).id, QStringLiteral("wipe_horizontal"));
}

void TestSwitcherEngine::testSmpteLookup()
{
    WipePattern pattern;
    bool reverse = true;
    QVERIFY(lookupWipeBySmpte(0, &pattern, &reverse));
    QCOMPARE(pattern.id, QStringLiteral("wipe_horizontal"));
    QVERIFY(!reverse);
    QVERIFY(lookupWipeBySmpte(3, &pattern, &reverse));
    QCOMPARE(pattern.id, QStringLiteral("smil_boxWipe_topLeft"));
    QVERIFY(!reverse);
    QVERIFY(lookupWipeBySmpte(5, &pattern, &reverse));
    QCOMPARE(pattern.id, QStringLiteral("smil_boxWipe_bottomRight"));
    QVERIFY(!reverse);
    QVERIFY(!lookupWipeBySmpte(17, &pattern, &reverse));
    QVERIFY(!lookupWipeBySmpte(-1, &pattern, &reverse));
}

void TestSwitcherEngine::testWipeReverseAndBorder()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(12);
    config.setWipePatternId(QStringLiteral("smil_clockWipe_clockwiseTwelve"));
    config.setWipeDirectionMode(WipeDirectionMode::Reverse);
    config.setWipeEdgeMode(WipeEdgeMode::Border);
    config.setWipeEdgeAmount(8);
    config.setWipeBorderColor(QStringLiteral("#ffcc00"));

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeWipe();
    QTRY_COMPARE(engine.programSource(), 0);

    QString sting;
    for (const QString& command : caspar.commands) {
        if (command.contains(QLatin1String(" STING "))) {
            sting = command;
            break;
        }
    }
    QVERIFY(sting.contains(QStringLiteral("sense=rev")));
    QVERIFY(!sting.contains(QStringLiteral(" overlay=")));
    QString border;
    for (const QString& command : caspar.commands) {
        if (command.contains(QStringLiteral("PLAY 10-15")) && command.contains(QStringLiteral("[HTML]"))) {
            border = command;
            break;
        }
    }
    QVERIFY(border.contains(QStringLiteral("role=overlay")) || border.contains(QStringLiteral("role%3Doverlay")));
    QVERIFY(border.contains(QStringLiteral("edge=border")));
    QVERIFY(sting.contains(QStringLiteral("t0=")));
    QVERIFY(border.contains(QStringLiteral("t0=")));
    const int stingT0 = sting.indexOf(QStringLiteral("t0="));
    const int borderT0 = border.indexOf(QStringLiteral("t0="));
    QVERIFY(stingT0 >= 0);
    QVERIFY(borderT0 >= 0);
    QCOMPARE(sting.mid(stingT0, 16), border.mid(borderT0, 16));
}

void TestSwitcherEngine::testTransitionLockIgnoresStackedTakes()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(12);
    config.setWipePatternId(QStringLiteral("smil_clockWipe_clockwiseTwelve"));

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeWipe();
    QVERIFY(engine.isTransitioning());
    QCOMPARE(engine.activeTakeName(), QStringLiteral("wipe"));

    auto stingCount = [&caspar]() {
        int count = 0;
        for (const QString& command : caspar.commands) {
            if (command.contains(QLatin1String(" STING "))) {
                ++count;
            }
        }
        return count;
    };
    QTRY_COMPARE(stingCount(), 1);

    engine.executeWipe();
    engine.executeAuto();
    engine.executeCut();
    engine.hotPunchProgram(1);
    QCOMPARE(stingCount(), 1);
    QVERIFY(engine.isTransitioning());

    QTRY_VERIFY(!engine.isTransitioning());
    QCOMPARE(engine.activeTakeName(), QString());
    QCOMPARE(stingCount(), 1);
    QCOMPARE(engine.programSource(), 0);

    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.executeAuto();
    QVERIFY(engine.isTransitioning());
    QCOMPARE(engine.activeTakeName(), QStringLiteral("mix"));
    auto mixCount = [&caspar]() {
        int count = 0;
        for (const QString& command : caspar.commands) {
            if (command.contains(QLatin1String(" MIX 12"))) {
                ++count;
            }
        }
        return count;
    };
    QTRY_COMPARE(mixCount(), 1);
    engine.executeAuto();
    engine.executeWipe();
    engine.executeCut();
    QCOMPARE(mixCount(), 1);
    QTRY_VERIFY(!engine.isTransitioning());
    QCOMPARE(engine.activeTakeName(), QString());
    QCOMPARE(mixCount(), 1);
}

void TestSwitcherEngine::testFadeToBlack()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(12);

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.executeFtb();
    QVERIFY(engine.isFtb());
    QVERIFY(engine.isTransitioning());
    QCOMPARE(engine.activeTakeName(), QString());
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-40 #000000")));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-21 VOLUME 0.000 12 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-40 OPACITY 1.000 12 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-1 VOLUME 0.000 12 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-20 VOLUME 0.000 12 LINEAR")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 10-40 #000000")));
    engine.executeFtb();
    QVERIFY(engine.isTransitioning());
    QTRY_VERIFY(!engine.isTransitioning());
    QVERIFY(engine.isFtb());

    const int beforeUp = caspar.commands.size();
    engine.executeFtb();
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-40 OPACITY 0.000 12 LINEAR")));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-1 VOLUME 1.000 12 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-20 VOLUME 1.000 12 LINEAR")));
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-21 VOLUME 1.000 12 LINEAR")));
    QTRY_VERIFY(!engine.isFtb());
    // Local FTB state changes before the queued AMCP cleanup reaches the peer.
    QTRY_VERIFY(caspar.commands.mid(beforeUp).contains(QStringLiteral("CLEAR 12-40")));
    QCOMPARE(config.autoDurationFrames(), 12);
    engine.executeFtb(40);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("MIXER 12-40 OPACITY 1.000 40 LINEAR")));
    QCOMPARE(config.autoDurationFrames(), 12);
}

void TestSwitcherEngine::testNdiOutputsAndCleanFeed()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-1 route://10")));
    QVERIFY(caspar.commands.contains(QStringLiteral("ADD 12 NDI NAME KAVTOR_PGM")));
    QVERIFY(caspar.commands.contains(QStringLiteral("ADD 10 NDI NAME KAVTOR_CLEAN")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 12-1 route://10-1")));

    const auto addCount = [&caspar](const QString& command) {
        int count = 0;
        for (const QString& item : caspar.commands) {
            if (item == command) {
                ++count;
            }
        }
        return count;
    };
    const int firstPgmAdds = addCount(QStringLiteral("ADD 12 NDI NAME KAVTOR_PGM"));
    engine.armMixer();
    QTRY_COMPARE(addCount(QStringLiteral("ADD 12 NDI NAME KAVTOR_PGM")), firstPgmAdds);
    QCOMPARE(addCount(QStringLiteral("ADD 10 NDI NAME KAVTOR_CLEAN")), firstPgmAdds);

    config.setNdiProgramName(QStringLiteral("KAVTOR MIX"));
    engine.armMixer();
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("REMOVE 12 NDI NAME KAVTOR_PGM")));
    QVERIFY(caspar.commands.contains(QStringLiteral("ADD 12 NDI NAME \"KAVTOR MIX\"")));

    config.setNdiCleanEnabled(false);
    engine.armMixer();
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("REMOVE 10 NDI NAME KAVTOR_CLEAN")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-1 route://10")));

    config.setProgramOutputChannel(10);
    config.setNdiCleanEnabled(true);
    engine.armMixer();
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("STOP 12-1")));
    QVERIFY(caspar.commands.contains(QStringLiteral("ADD 10 NDI NAME \"KAVTOR MIX\"")));
    QCOMPARE(addCount(QStringLiteral("ADD 10 NDI NAME KAVTOR_CLEAN")), firstPgmAdds);

    const QJsonObject json = config.toJson();
    QVERIFY(json.value(QStringLiteral("outputs")).isObject());
    const QJsonObject outputs = json.value(QStringLiteral("outputs")).toObject();
    QCOMPARE(outputs.value(QStringLiteral("programOutputChannel")).toInt(), 10);
    QCOMPARE(outputs.value(QStringLiteral("ndiProgram")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("KAVTOR MIX"));
    QVERIFY(outputs.value(QStringLiteral("ndiClean")).toObject().value(QStringLiteral("enabled")).toBool());
}

void TestSwitcherEngine::testDeckSourceSelection()
{
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    AmcpClient client;
    SwitcherEngine engine(&config, &client);

    QVERIFY(config.sourceById(0)->isClip());
    QVERIFY(config.sourceById(1)->isClip());
    QVERIFY(!config.sourceById(2)->isClip());

    QCOMPARE(engine.deckSource(), -1);
    engine.selectDeckSource(0);
    QCOMPARE(engine.deckSource(), 0);
    engine.selectDeckSource(0);
    QCOMPARE(engine.deckSource(), -1);

    engine.selectDeckSource(0);
    engine.selectDeckSource(1);
    QCOMPARE(engine.deckSource(), 1);

    engine.selectDeckSource(2);
    QCOMPARE(engine.deckSource(), -1);

    engine.selectDeckSource(0);
    engine.selectDeckSource(8);
    QCOMPARE(engine.deckSource(), -1);

    FakeCaspar caspar;
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    client.setReconnectIntervalMs(0);
    SwitcherEngine live(&config, &client);
    live.connectToCaspar();
    QTRY_VERIFY(live.isConnected());

    live.selectDeckSource(0);
    QCOMPARE(live.deckSource(), 0);
    QTRY_VERIFY(hasNativeState(caspar.commands,{{"armed",0},{"cued",-1}}));

    live.toggleDeckPause();
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PAUSE 1-1")));
    live.toggleDeckPause();
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("RESUME 1-1")));

    live.jogDeck(80);
    QVERIFY(!caspar.commands.contains(QStringLiteral("CALL 1-1 SEEK REL 2")));
    live.jogDeck(280);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("CALL 1-1 SEEK 1")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PAUSE 1-1")));

    live.selectDeckSource(2);
    QCOMPARE(live.deckSource(), -1);
    QTRY_VERIFY(hasNativeState(caspar.commands,{{"armed",-1},{"cued",-1}}));
}

void TestSwitcherEngine::testDeckCueUntilProgram()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 1);

    engine.selectDeckSource(0);
    QCOMPARE(engine.deckSource(), 0);
    QCOMPARE(engine.cuedSource(), -1);

    engine.toggleDeckCue();
    QCOMPARE(engine.cuedSource(), 0);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PAUSE 1-1")));
    QTRY_VERIFY(hasNativeState(caspar.commands,{{"armed",0},{"cued",0}}));

    engine.selectDeckSource(0);
    QCOMPARE(engine.deckSource(), -1);
    QCOMPARE(engine.cuedSource(), 0);
    QTRY_VERIFY(hasNativeState(caspar.commands,{{"armed",-1},{"cued",0}}));

    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    QCOMPARE(engine.cuedSource(), -1);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("RESUME 1-1")));
    const int resumeAt = caspar.commands.indexOf(QStringLiteral("RESUME 1-1"));
    int playAt = -1;
    for (int i = resumeAt + 1; i < caspar.commands.size(); ++i) {
        if (caspar.commands.at(i) == QStringLiteral("PLAY 10-1 route://1-1")) {
            playAt = i;
            break;
        }
    }
    QVERIFY(resumeAt >= 0);
    QVERIFY(playAt > resumeAt);

    engine.selectDeckSource(0);
    engine.toggleDeckCue();
    QCOMPARE(engine.cuedSource(), 0);
    engine.hotPunchProgram(0);
    QCOMPARE(engine.cuedSource(), 0);

    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 1);
    QCOMPARE(engine.cuedSource(), 0);

    engine.hotPunchProgram(0);
    QTRY_COMPARE(engine.programSource(), 0);
    QCOMPARE(engine.cuedSource(), -1);
    QVERIFY(hasNativeState(caspar.commands,{{"armed",0},{"cued",-1}}));
}

void TestSwitcherEngine::testDeckJogModes()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());

    engine.selectDeckSource(0);
    engine.jogDeck(360);
    engine.jogDeck(360);
    engine.jogDeck(360);
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("PAUSE 1-1")));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("CALL 1-1 SEEK 1")));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("CALL 1-1 SEEK 3")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("CALL 1-1 SEEK REL 1")));

    engine.setDeckJogMode(SwitcherEngine::DeckJogMode::Scroll);
    const int before = caspar.commands.size();
    engine.jogDeck(36);
    QTRY_VERIFY(caspar.commands.mid(before).contains(QStringLiteral("CALL 1-1 SEEK 4")));

    engine.setDeckJogMode(SwitcherEngine::DeckJogMode::Shuttle);
    const int shuttleAt = caspar.commands.size();
    engine.jogDeck(4096);
    QTRY_VERIFY(caspar.commands.size() > shuttleAt);
    bool sawSeek = false;
    int lastFrame = 0;
    for (int i = shuttleAt; i < caspar.commands.size(); ++i) {
        const QString cmd = caspar.commands.at(i);
        if (cmd.startsWith(QStringLiteral("CALL 1-1 SEEK "))) {
            QVERIFY(!cmd.contains(QStringLiteral(" REL ")));
            lastFrame = cmd.section(QLatin1Char(' '), 3).toInt();
            sawSeek = lastFrame > 4;
        }
    }
    QVERIFY(sawSeek);

    engine.jogDeck(0);
    const int stoppedAt = caspar.commands.size();
    QTest::qWait(80);
    int extra = 0;
    for (int i = stoppedAt; i < caspar.commands.size(); ++i) {
        if (caspar.commands.at(i).startsWith(QStringLiteral("CALL 1-1 SEEK "))) {
            ++extra;
        }
    }
    QVERIFY(extra <= 1);
}

void TestSwitcherEngine::testStackedMixEffects()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    Source logo = *config.sourceById(7);
    logo.argument = QStringLiteral("logo.png");
    config.setSource(logo);

    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    QSignalSpy errors(&engine, &SwitcherEngine::error);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QCOMPARE(engine.activeMe(), 0);

    engine.setKeySource(0, 0);
    engine.setKeyOn(0, true);
    QTRY_VERIFY(engine.keyOn(0));
    engine.toggleNextKey(0);
    engine.setDskSource(0, 7);
    engine.setDskOn(true);
    QTRY_VERIFY(engine.isDskOn());
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-20 route://8-1")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 10-20 route://8-1")));

    engine.selectPreview(11);
    QTRY_COMPARE(engine.previewSource(), 11);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-1 route://13 RENDERED")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 9-1 route://13-1")));

    engine.setActiveMe(1);
    QTRY_COMPARE(engine.activeMe(), 1);
    QCOMPARE(engine.previewSource(), -1);
    QCOMPARE(engine.keyOn(0), false);
    QCOMPARE(engine.keySource(0), -1);
    QCOMPARE(engine.nextKey(0), false);
    QCOMPARE(engine.isDskOn(), true);
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 14-1 route://1-1")));
    engine.setKeySource(0, 1);
    QCOMPARE(engine.keySource(0), 1);

    engine.setActiveMe(0);
    QTRY_COMPARE(engine.activeMe(), 0);
    QCOMPARE(engine.previewSource(), 11);
    QCOMPARE(engine.keyOn(0), true);
    QCOMPARE(engine.keySource(0), 0);
    QCOMPARE(engine.nextKey(0), true);
    QCOMPARE(config.meKeySource(1, 0), 1);

    engine.setActiveMe(3);
    QTRY_COMPARE(engine.activeMe(), 3);
    const int before = caspar.commands.size();
    engine.selectPreview(11);
    QCOMPARE(engine.previewSource(), -1);
    QCOMPARE(caspar.commands.size(), before);
    QCOMPARE(errors.count(), 0);
}


void TestSwitcherEngine::testTakeHoldWaitsForAcknowledgement()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.responseDelayMs = 100;
    QElapsedTimer afterAck;
    connect(&client, &AmcpClient::responseReceived, &engine,
            [&](int, const QString&, const QStringList&, const QString& command) {
        if (command == QStringLiteral("CLEAR 10-15") && !afterAck.isValid()) {
            afterAck.start();
        }
    });
    engine.executeTransition(Transition::mix(5));
    QTest::qWait(150);
    QVERIFY(engine.isTransitioning());
    QCOMPARE(engine.programSource(), 0);
    QTRY_COMPARE(engine.programSource(), 1);
    QVERIFY(afterAck.isValid());
    QVERIFY(afterAck.elapsed() >= 90);
    QVERIFY(!engine.isTransitioning());
    engine.disconnectFromCaspar();
}


void TestSwitcherEngine::testFtbHoldWaitsForAcknowledgement()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.responseDelayMs = 100;
    engine.executeFtb(1);
    QTest::qWait(50);
    QVERIFY(engine.isTransitioning());
    QVERIFY(engine.isFtb());
    QTRY_VERIFY(!engine.isBusy());
    QVERIFY(engine.isFtb());
    engine.disconnectFromCaspar();
}

void TestSwitcherEngine::testDskFadeWaitsForAcknowledgement()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setDskSource(1, 0);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    engine.setDskSlot(1, true, 0);
    QTRY_VERIFY(engine.isDskOn(1));
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.commands.clear();
    caspar.responseDelayMs = 100;
    engine.setDskSlot(1, false, 5);
    QTest::qWait(150);
    QVERIFY(engine.isDskMixing(1));
    QVERIFY(engine.isDskOn(1));
    QTRY_VERIFY(!engine.isDskOn(1));
    QTRY_VERIFY(!engine.isDskMixing(1));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("CLEAR 12-21")));
    engine.disconnectFromCaspar();
}

void TestSwitcherEngine::testConfigurationSaveFailure()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setConfigFilePath(temp.filePath(QStringLiteral("switcher.json")));
    QVERIFY(config.save());
    Configuration loaded;
    loaded.setConfigFilePath(config.configFilePath());
    QVERIFY(loaded.load());
    QCOMPARE(loaded.toJson(), config.toJson());
    // A directory cannot be replaced by a configuration file.
    config.setConfigFilePath(temp.path());
    QVERIFY(!config.save());
    QVERIFY(QFileInfo::exists(loaded.configFilePath()));
}


void TestSwitcherEngine::testWipeRequestValidationIsAtomic()
{
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    AmcpClient amcp;
    SwitcherEngine engine(&config, &amcp);
    PanelProtocol panel(&engine);
    QVERIFY(panel.start(0));
    QTcpSocket peer;
    peer.connectToHost(QHostAddress::LocalHost, panel.port());
    QTRY_VERIFY(peer.canReadLine());
    peer.readAll();
    const QJsonObject original = config.toJson();
    QSignalSpy changed(&config, &Configuration::configurationChanged);
    QByteArray response;
    peer.write("{\"cmd\":\"wipe_style\",\"multi\":2,\"border\":99}\n");
    QTRY_VERIFY((response += peer.readAll()).contains("Wipe border must be"));
    QCOMPARE(config.toJson(), original);
    QCOMPARE(changed.count(), 0);

    response.clear();
    peer.write("{\"cmd\":\"wipe\",\"smpte\":5,\"multi\":4,\"posX\":1001}\n");
    QTRY_VERIFY((response += peer.readAll()).contains("Wipe position must be"));
    QCOMPARE(config.toJson(), original);
    QCOMPARE(changed.count(), 0);

    response.clear();
    peer.write("{\"cmd\":\"wipe\",\"pattern\":\"wipe_vertical\",\"edge\":\"invalid\",\"multi\":4}\n");
    QTRY_VERIFY((response += peer.readAll()).contains("Unknown wipe edge"));
    QCOMPARE(config.toJson(), original);
    QCOMPARE(changed.count(), 0);

    response.clear();
    peer.write("{\"cmd\":\"wipe_style\",\"multi\":2,\"border\":8,\"posX\":250}\n");
    QTRY_VERIFY((response += peer.readAll()).contains("\"event\":\"ack\""));
    QCOMPARE(config.wipeMulti(), 2);
    QCOMPARE(config.wipeBorderAmount(), 8);
    QCOMPARE(config.wipePosX(), 250);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    config.setConfigFilePath(temp.filePath(QStringLiteral("wipe-settings.json")));
    const auto beforeSettings = config.toJson();
    response.clear();
    peer.write(R"({"cmd":"wipe_settings","pattern":"smil_kavtorWipe_checker","direction":"fwd","edge":"soft","amount":12,"color":"#123456","multi":4,"border":7,"shadow":41,"aspectW":4,"aspectH":3})" "\n");
    QTRY_VERIFY((response += peer.readAll()).contains("Wipe shadow must be"));
    QCOMPARE(config.toJson(), beforeSettings);
    response.clear();
    const int countBefore = changed.count();
    peer.write(R"({"cmd":"wipe_settings","pattern":"smil_kavtorWipe_checker","direction":"fwd","edge":"soft","amount":12,"color":"#123456","multi":4,"border":7,"shadow":5,"aspectW":4,"aspectH":3})" "\n");
    QTRY_VERIFY((response += peer.readAll()).contains("\"event\":\"ack\""));
    QCOMPARE(changed.count(), countBefore + 1);
    QCOMPARE(config.wipeShadowAmount(), 5);
    QCOMPARE(config.wipeBorderAmount(), 7);
    Configuration restored;
    restored.setConfigFilePath(config.configFilePath());
    QVERIFY(restored.load());
    QCOMPARE(restored.wipeShadowAmount(), 5);
    config.setConfigFilePath(temp.path());
    response.clear();
    peer.write("{\"cmd\":\"wipe_style\",\"border\":9,\"save\":true}\n");
    QTRY_VERIFY((response += peer.readAll()).contains("could not be saved"));
    QVERIFY(!response.contains("\"event\":\"ack\""));
    QCOMPARE(config.wipeBorderAmount(), 9);
    panel.stop();
}


void TestSwitcherEngine::testSourceArmingWaitsForSuccess()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    const QString firstPlay = QStringLiteral("PLAY 1-1 %1").arg(config.sourceById(0)->producerCommand());
    const QString secondPlay = QStringLiteral("PLAY 2-1 %1").arg(config.sourceById(1)->producerCommand());
    caspar.failuresRemaining[firstPlay] = 1;
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    QSignalSpy errors(&engine, &SwitcherEngine::error);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(!engine.sourceReady(0));
    QVERIFY(engine.sourceReady(1));
    QVERIFY(errors.count() > 0);
    const int unchangedPlayCount = caspar.commands.count(secondPlay);
    caspar.commands.clear();
    engine.armMixer();
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(engine.sourceReady(0));
    QCOMPARE(caspar.commands.count(firstPlay), 1);
    QCOMPARE(caspar.commands.count(secondPlay), 0);
    QCOMPARE(unchangedPlayCount, 1);
    engine.disconnectFromCaspar();
    QVERIFY(!engine.sourceReady(0));
}

void TestSwitcherEngine::testSourceFillRetryKeepsAcceptedProducer()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    // Identify the exact fill command from one successful startup first.
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QString fill;
    for (const QString& command : caspar.commands) {
        if (command.startsWith(QStringLiteral("MIXER 1-1 FILL "))) {
            fill = command;
        }
    }
    QVERIFY(!fill.isEmpty());
    engine.disconnectFromCaspar();
    caspar.failuresRemaining[fill] = 1;
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(!engine.sourceReady(0));
    caspar.commands.clear();
    engine.armMixer();
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(engine.sourceReady(0));
    QCOMPARE(caspar.commands.count(fill), 1);
    for (const QString& command : caspar.commands) {
        QVERIFY(!command.startsWith(QStringLiteral("PLAY 1-1 ")));
    }
}

void TestSwitcherEngine::testOutputRetryKeepsAcceptedRoute()
{
    FakeCaspar caspar;
    const QString pgm = QStringLiteral("ADD 12 NDI NAME KAVTOR_PGM");
    caspar.failuresRemaining[pgm] = 1;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QCOMPARE(caspar.commands.count(pgm), 1);
    QVERIFY(!engine.outputsReady());
    QVERIFY(!caspar.commands.contains(QStringLiteral("ADD 10 NDI NAME KAVTOR_CLEAN")));
    caspar.commands.clear();
    engine.armMixer();
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QCOMPARE(caspar.commands.count(pgm), 1);
    QCOMPARE(caspar.commands.count(QStringLiteral("ADD 10 NDI NAME KAVTOR_CLEAN")), 1);
    QVERIFY(engine.outputsReady());
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 12-1 route://10")));
}

void TestSwitcherEngine::testFailedTakeDoesNotRunRemainder()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.commands.clear();
    caspar.failuresRemaining[QStringLiteral("PLAY 10-1 route://2-1 MIX 5")] = 1;
    QSignalSpy errors(&engine, &SwitcherEngine::error);
    engine.executeTransition(Transition::mix(5));
    QTRY_VERIFY(!engine.isBusy());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(errors.count() > 0);
    QCOMPARE(engine.programSource(), 0);
    QCOMPARE(engine.previewSource(), 1);
    QVERIFY(!caspar.commands.contains(QStringLiteral("CLEAR 10-15")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("CLEAR 10-11")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 9-1 route://1-1")));
}


void TestSwitcherEngine::testSourceChangeDuringPreparation()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(engine.sourceReady(0));
    caspar.responseDelayMs = 50;
    Source changed = *config.sourceById(0);
    changed.argument = QStringLiteral("B");
    const QString playB = QStringLiteral("PLAY 1-1 %1").arg(changed.producerCommand());
    config.setSource(changed);
    engine.armMixer();
    changed.argument = QStringLiteral("C");
    const QString playC = QStringLiteral("PLAY 1-1 %1").arg(changed.producerCommand());
    config.setSource(changed);
    engine.armMixer();
    QVERIFY(!engine.sourceReady(0));
    QTRY_VERIFY_WITH_TIMEOUT(engine.sourceReady(0), 10000);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QCOMPARE(caspar.commands.count(playB), 1);
    QCOMPARE(caspar.commands.count(playC), 1);
}


void TestSwitcherEngine::testIdenticalBackgroundReplyDoesNotCompletePreview()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.responseDelayMs = 100;
    client.sendCommand(QStringLiteral("PLAY 9-1 route://1-1"));
    engine.selectPreview(0);
    QTest::qWait(150);
    QCOMPARE(engine.previewSource(), -1);
    QVERIFY(engine.isBusy());
    QTRY_COMPARE(engine.previewSource(), 0);
    QVERIFY(!engine.isBusy());
}

void TestSwitcherEngine::testDisconnectDoesNotCommitDeferredTake()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(100);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    QTRY_VERIFY(!engine.isBusy());
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.executeAuto();
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(engine.isTransitioning());
    const int commandsBeforeDisconnect = caspar.commands.size();
    engine.disconnectFromCaspar();
    QTRY_VERIFY(!engine.isConnected());
    QCOMPARE(engine.programSource(), 0);
    QCOMPARE(engine.previewSource(), 1);
    QVERIFY(!engine.isBusy());
    QTest::qWait(2200);
    QCOMPARE(engine.programSource(), 0);
    QCOMPARE(engine.previewSource(), 1);
    QCOMPARE(caspar.commands.size(), commandsBeforeDisconnect);
}

void TestSwitcherEngine::testPreparedConfigurationKeepsBuses()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    QTRY_VERIFY(!engine.isBusy());
    config.setMeKeySource(0, 0, 2);
    config.setMeKeySource(1, 0, 3);
    engine.applyPreparedConfiguration();
    QCOMPARE(engine.programSource(), 0);
    QCOMPARE(engine.previewSource(), 1);
    QCOMPARE(engine.keySource(0), 2);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    engine.setActiveMe(1);
    QCOMPARE(engine.keySource(0), 3);
    engine.setActiveMe(0);
    QCOMPARE(engine.programSource(), 0);
    QCOMPARE(engine.previewSource(), 1);
}

void TestSwitcherEngine::testKeyStateAndSourceWaitForAcceptedBatch()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    Source logo = *config.sourceById(7);
    logo.argument = QStringLiteral("logo.png");
    config.setSource(logo);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    engine.setKeySource(0, 7);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.responseDelayMs = 30;
    caspar.failuresRemaining[QStringLiteral("PLAY 10-11 route://9-11")] = 1;
    engine.setKeyOn(0, true);
    QVERIFY(!engine.keyOn(0));
    QVERIFY(engine.isBusy());
    engine.setKeySource(0, 1);
    QCOMPARE(engine.keySource(0), 7);
    QTRY_VERIFY(!engine.isBusy());
    QVERIFY(!engine.keyOn(0));
    engine.setKeyOn(0, true);
    QVERIFY(!engine.keyOn(0));
    QTRY_VERIFY(engine.keyOn(0));
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    const QString failingSource = QStringLiteral("PLAY 10-11 route://1-1");
    caspar.failuresRemaining[failingSource] = 1;
    engine.setKeySource(0, 0);
    QCOMPARE(engine.keySource(0), 7);
    QCOMPARE(config.meKeySource(0, 0), 7);
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.keySource(0), 7);
    QCOMPARE(config.meKeySource(0, 0), 7);
    engine.setKeySource(0, 0);
    QCOMPARE(engine.keySource(0), 7);
    QTRY_COMPARE(engine.keySource(0), 0);
    QCOMPARE(config.meKeySource(0, 0), 0);
    QVERIFY(engine.keyOn(0));
}

void TestSwitcherEngine::testAllFourNextKeysAndReset()
{
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    AmcpClient client;
    SwitcherEngine engine(&config, &client);
    engine.toggleNextKey(3);
    engine.toggleNextBackground();
    QVERIFY(!engine.nextBackground());
    QVERIFY(engine.nextKey(3));
    engine.toggleNextKey(3);
    QVERIFY(engine.nextKey(3)); // At least one next-transition component remains selected.
    engine.toggleNextKey(2);
    engine.resetNextTransition();
    QVERIFY(engine.nextBackground());
    for (int key = 0; key < engine.keyerCount(); ++key) QVERIFY(!engine.nextKey(key));
    engine.setKeySource(0, 0);
    engine.setActiveMe(1);
    engine.setKeySource(0, 1);
    QCOMPARE(engine.keySource(0, 0), 0);
    QCOMPARE(engine.keySource(1, 0), 1);
    QCOMPARE(engine.activeMe(), 1); // Inspecting another bank never changes delegation.
}

void TestSwitcherEngine::testDskTwoPreviewSurvivesFadeOut()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    Source logo = *config.sourceById(7);
    logo.argument = QStringLiteral("logo.png");
    config.setSource(logo);
    config.setDskSource(1, 7);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.commands.clear();
    engine.setDskPreview(1, true);
    QVERIFY(!engine.isDskPreview(1));
    QTRY_VERIFY(engine.isDskPreview(1));
    QVERIFY(!engine.isDskPreview(0));
    QVERIFY(!engine.isDskOn(1));
    QVERIFY(!caspar.commands.contains(QStringLiteral("PLAY 12-21 route://8-1")));
    engine.setDskSlot(1, true, 0);
    QTRY_VERIFY(engine.isDskOn(1));
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.commands.clear();
    engine.setDskSlot(1, false, 5);
    QTRY_VERIFY(!engine.isDskMixing(1));
    QTRY_VERIFY(caspar.commands.contains(QStringLiteral("CLEAR 12-21")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("CLEAR 9-21")));
    QVERIFY(engine.isDskPreview(1));
    QVERIFY(!engine.isDskOn(1));
}

void TestSwitcherEngine::testKeyerCapabilitiesAndInvalidRequests()
{
    QTemporaryDir dir;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setConfigFilePath(dir.filePath(QStringLiteral("keyers.json")));
    AmcpClient amcp;
    SwitcherEngine engine(&config, &amcp);
    PanelProtocol panel(&engine);
    QVERIFY(panel.start(0));
    QTcpSocket peer;
    peer.connectToHost(QHostAddress::LocalHost, panel.port());
    QTRY_VERIFY(peer.canReadLine());
    const QJsonObject state = QJsonDocument::fromJson(peer.readLine()).object();
    QCOMPARE(state.value(QStringLiteral("capabilities")).toObject().value(QStringLiteral("meCount")).toInt(), 4);
    QCOMPARE(state.value(QStringLiteral("mes")).toArray().size(), 4);
    QCOMPARE(state.value(QStringLiteral("keys")).toArray().size(), 4);
    QCOMPARE(state.value(QStringLiteral("dsks")).toArray().size(), 2);
    QTRY_VERIFY(peer.canReadLine());
    peer.readLine(); // Initial tally event.
    for (const QByteArray& request : {QByteArray("{\"cmd\":\"key_on\",\"slot\":4}"),
         QByteArray("{\"cmd\":\"dsk_preview\",\"slot\":-1}"),
         QByteArray("{\"cmd\":\"key_source\",\"source\":0.5}"),
         QByteArray("{\"cmd\":\"next\",\"target\":\"unknown\"}"),
         QByteArray("{\"cmd\":\"dsk\",\"mix\":1}"),
         QByteArray("{\"cmd\":\"key_source\",\"source\":7}"),
         QByteArray("{\"cmd\":\"dsk\",\"mix\":true,\"frames\":0}")}) {
        peer.write(request + "\n");
        peer.flush();
        QTRY_VERIFY(peer.canReadLine());
        const auto reply = QJsonDocument::fromJson(peer.readLine()).object();
        QCOMPARE(reply.value(QStringLiteral("event")).toString(), QStringLiteral("error"));
    }
    QVERIFY(!QFile::exists(config.configFilePath()));
}

void TestSwitcherEngine::testTransitionPreviewPreservesProgramAndKeys()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(5);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 0);
    QTRY_VERIFY(!engine.isBusy());
    engine.selectPreview(1);
    QTRY_COMPARE(engine.previewSource(), 1);
    engine.setKeySource(0, 2);
    engine.toggleNextKey(0);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(engine.setTransitionPreview(true));
    for (int repeat = 0; repeat < 2; ++repeat) {
        caspar.commands.clear();
        engine.executeAuto();
        QVERIFY(engine.isPreviewTransitioning());
        QVERIFY(!engine.setTransitionPreview(false)); // Cannot redirect a running rehearsal to air.
        QTRY_VERIFY(!engine.isBusy());
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        QCOMPARE(engine.programSource(), 0);
        QCOMPARE(engine.previewSource(), 1);
        QVERIFY(!engine.keyOn(0));
        QVERIFY(engine.nextKey(0));
        QVERIFY(engine.isTransitionPreview());
        QVERIFY(!engine.isPreviewTransitioning());
        QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 9-111 CLEAR")));
        QVERIFY(caspar.commands.contains(QStringLiteral("CLEAR 9-101")));
        for (const QString& command : caspar.commands) {
            QVERIFY2(!command.startsWith(QStringLiteral("PLAY 10-")), qPrintable(command));
            QVERIFY2(!command.startsWith(QStringLiteral("CLEAR 10-")), qPrintable(command));
            QVERIFY2(!command.startsWith(QStringLiteral("MIXER 10-")), qPrintable(command));
            QVERIFY2(!command.startsWith(QStringLiteral("PLAY 12-")), qPrintable(command));
            const QString destination = command.section(QLatin1Char(' '), 1, 1);
            QVERIFY2(!destination.startsWith(QStringLiteral("9-")) || destination.mid(2).toInt() >= 100,
                qPrintable(command));
        }
    }
    engine.setKeyOn(0, true);
    QTRY_VERIFY(engine.keyOn(0));
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    engine.toggleNextBackground(); // Key-only rehearsal of removing an on-air key.
    caspar.commands.clear();
    engine.executeAuto();
    QTRY_VERIFY(!engine.isBusy());
    QVERIFY(engine.keyOn(0));
    QCOMPARE(engine.programSource(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("MIXER 9-111 OPACITY 0.000 5 LINEAR")));
    QVERIFY(!caspar.commands.contains(QStringLiteral("CLEAR 10-11")));
    engine.toggleNextBackground();
    engine.executeWipe();
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(), 0);
    QVERIFY(engine.keyOn(0));
    QVERIFY(engine.setTransitionPreview(false));
    engine.executeCut();
    QTRY_COMPARE(engine.programSource(), 1);
    QVERIFY(!engine.keyOn(0));
}

void TestSwitcherEngine::testTransitionPreviewFailureAndDelegation()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0); // Isolate the fake peer from a real renderer on UDP 6250.
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setAutoDurationFrames(5);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    engine.selectPreview(0);
    QTRY_COMPARE(engine.previewSource(), 0);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(engine.setTransitionPreview(true));
    engine.setActiveMe(1);
    QVERIFY(!engine.isTransitionPreview());
    engine.setActiveMe(0);
    QVERIFY(engine.isTransitionPreview());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.commands.clear();
    caspar.failuresRemaining[QStringLiteral("PLAY 9-101 #000000")] = 1;
    engine.executeAuto();
    QTRY_VERIFY(!engine.isBusy());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(!engine.isPreviewTransitioning());
    QCOMPARE(engine.programSource(), -1);
    QCOMPARE(engine.previewSource(), 0);
    for (const QString& command : caspar.commands)
        QVERIFY2(!command.startsWith(QStringLiteral("PLAY 10-")), qPrintable(command));
    QVERIFY(!engine.executeStinger(0, false));
    engine.executeAuto();
    QTRY_VERIFY(!engine.isBusy());
    QCOMPARE(engine.programSource(), -1);
}

void TestSwitcherEngine::testKeyRoutesReusePreparedInput()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    Source input = *config.sourceById(7);
    input.argument = QStringLiteral("key.html");
    input.type = SourceType::Html;
    config.setSource(input);
    config.setMeKeySource(0, 0, 7);
    config.setDskSource(0, 7);
    config.setDskSource(1, 7);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(engine.sourceReady(7));
    caspar.commands.clear();
    engine.setKeyOn(0, true);
    QTRY_VERIFY(engine.keyOn(0));
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 9-11 route://8-1")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 10-11 route://9-11")));
    engine.setDskOn(true);
    QTRY_VERIFY(engine.isDskOn());
    engine.setDskSlot(1, true, 5);
    QTRY_VERIFY(engine.isDskOn(1));
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-20 route://8-1")));
    QVERIFY(caspar.commands.contains(QStringLiteral("PLAY 12-21 route://8-1 MIX 5 LINEAR")));
    for (const QString& command : caspar.commands) {
        QVERIFY2(!command.contains(QStringLiteral("key.html")), qPrintable(command));
        QVERIFY2(!command.startsWith(QStringLiteral("PLAY 8-1 ")), qPrintable(command));
    }
}

void TestSwitcherEngine::testMetersFollowConfiguredChannels()
{
    FakeCaspar caspar;
    Configuration config;
    config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));
    config.setCasparPort(caspar.port());
    config.setMeters(true);
    config.setProgramOutputChannel(config.programChannel()); // Air fallback uses PGM, not a dedicated channel.
    Source input = *config.sourceById(0);
    input.casparChannel = 25;
    config.setSource(input);
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    SwitcherEngine engine(&config, &client);
    engine.connectToCaspar();
    QTRY_VERIFY(engine.isConnected());
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    engine.setActiveMe(3);
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    caspar.commands.clear();
    const auto peak = std::numeric_limits<qint32>::max();
    engine.ingestAudioPeaks({OscAudioPeak{25, peak, 0},
        OscAudioPeak{config.previewChannel(), 0, peak},
        OscAudioPeak{config.programChannel(), peak, peak},
        OscAudioPeak{14, peak, 0}, OscAudioPeak{13, 0, peak},
        OscAudioPeak{16, peak, 0}, OscAudioPeak{15, 0, peak},
        OscAudioPeak{18, peak, 0}, OscAudioPeak{17, 0, peak}});
    engine.flushOverlayMeters();
    int expected[33][2] = {};
    expected[0][0] = 1000;
    expected[24][1] = 1000;
    expected[25][0] = expected[25][1] = 1000;
    for(int me=0;me<3;++me){expected[26+me*2][0]=1000;expected[27+me*2][1]=1000;}
    QJsonObject meterData;for(int i=0;i<32;++i)for(int side=0;side<2;++side)for(int band=0;band<3;++band) {if(i>=12&&i<24)continue;meterData.insert(QString("meter-%1-%2-%3").arg(i).arg(side).arg(band),expected[i][side]?1.:0.);}
    QTRY_VERIFY(std::any_of(caspar.commands.cbegin(), caspar.commands.cend(),
        [&meterData](const QString& command) { return command.startsWith("CALL 11-80 VALUES ")&&QJsonDocument::fromJson(QByteArray::fromBase64(command.section(' ',-1).toLatin1())).object()==meterData; }));
    engine.setActiveMe(0);QTRY_COMPARE(client.queuedCommandCount(),0);caspar.commands.clear();
    engine.ingestAudioPeaks({OscAudioPeak{13, peak, 0}});engine.flushOverlayMeters();
    int cascade[33][2]={};cascade[11][0]=cascade[23][0]=cascade[27][0]=1000;
    QJsonObject cascadeData;for(int i=0;i<32;++i)for(int side=0;side<2;++side)for(int band=0;band<3;++band) {if(i>=12&&i<24)continue;cascadeData.insert(QString("meter-%1-%2-%3").arg(i).arg(side).arg(band),cascade[i][side]?1.:0.);}
    QTRY_VERIFY(std::any_of(caspar.commands.cbegin(),caspar.commands.cend(),
        [&cascadeData](const QString& command){return command.startsWith("CALL 11-80 VALUES ")&&QJsonDocument::fromJson(QByteArray::fromBase64(command.section(' ',-1).toLatin1())).object()==cascadeData;}));
}


void TestSwitcherEngine::testSourceBanksAndFeedback()
{
    FakeCaspar caspar; Configuration config; config.setOscPort(0);
    config.setCasparHost(QStringLiteral("127.0.0.1"));config.setCasparPort(caspar.port());
    QCOMPARE(config.maxSources(),24); QCOMPARE(config.sourceById(12)->casparChannel,23);
    Source me=*config.sourceById(8);me.enabled=true;me.type=SourceType::MeProgram;me.argument="4";config.setSource(me);
    Source upper=*config.sourceById(12);upper.enabled=true;upper.argument="AMB";config.setSource(upper);
    Source back=*config.sourceById(9);back.enabled=true;back.type=SourceType::MeProgram;back.argument="1";config.setSource(back);
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);
    engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(caspar.commands.contains("PLAY 11-9 route://9 RENDERED"));
    QVERIFY(caspar.commands.contains("PLAY 11-10 route://12 RENDERED"));
    for(int me=1;me<4;++me) {
        QVERIFY(caspar.commands.contains(QString("PLAY 11-%1 route://%2 RENDERED").arg(60+me).arg(12+me*2)));
        QVERIFY(caspar.commands.contains(QString("PLAY 11-%1 route://%2 RENDERED").arg(64+me).arg(11+me*2)));
    }
    // The native 2304x1080 multiview must preserve the 16:9 bus raster
    // for the two main tiles and all six side-column preview/program tiles.
    int busTiles = 0;
    for (const auto& command : caspar.commands) {
        const auto fields = command.split(' ');
        if (fields.size() != 7 || fields[0] != "MIXER" || fields[2] != "FILL") continue;
        const int layer = fields[1].section('-',1).toInt();
        if (!fields[1].startsWith("11-") || !(layer==9 || layer==10 || (layer>=61&&layer<=63) || (layer>=65&&layer<=67))) continue;
        const double aspect = 2304. * fields[5].toDouble() / (1080. * fields[6].toDouble());
        QVERIFY2(qAbs(aspect-16./9.)<0.00002,qPrintable(command));
        ++busTiles;
    }
    QCOMPARE(busTiles,8);
    QVERIFY(engine.sourceSelectable(8));QVERIFY(!engine.sourceSelectable(9));
    engine.selectPreview(8);QTRY_COMPARE(engine.previewSource(),8);QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(caspar.commands.contains("PLAY 9-1 route://17 RENDERED"));
    caspar.commands.clear();
    engine.setActiveMe(3);QTRY_COMPARE(client.queuedCommandCount(),0);
    for(const auto& command:caspar.commands)QVERIFY(!command.startsWith("PLAY 11-9 ")&&!command.startsWith("PLAY 11-10 "));
    QVERIFY(!engine.sourceSelectable(8));QVERIFY(!engine.sourceSelectable(11));QVERIFY(!engine.sourceSelectable(23));
    engine.setActiveMe(1);engine.selectPreview(9);QTRY_COMPARE(engine.previewSource(),9);QTRY_COMPARE(client.queuedCommandCount(),0);
    engine.setActiveMe(0);QVERIFY(!engine.sourceSelectable(11)); // M/E 2 already depends on M/E 1.
    QTRY_COMPARE(client.queuedCommandCount(),0);caspar.commands.clear();
    engine.setMultiviewBank(1);QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(caspar.commands.contains("PLAY 11-41 route://23-1"));
    for(const auto& command:caspar.commands)QVERIFY(!command.startsWith("PLAY 10-")&&!command.startsWith("PLAY 9-"));
    QCOMPARE(engine.multiviewBank(),1);
}
void TestSwitcherEngine::testManagedOutputsAndAux()
{
    FakeCaspar caspar;Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    OutputDestination aux;aux.enabled=true;aux.id=0;aux.name="AUX monitor";aux.aux=1;aux.source=1200;
    OutputDestination display;display.enabled=true;display.id=1;display.type="screen";display.name="Clean display";display.source=1003;display.device=2;
    QVERIFY(config.setDestinations({aux,display}));
    Configuration restored;QVERIFY(restored.fromJson(config.toJson()));QCOMPARE(restored.destinations().size(),2);QCOMPARE(restored.destinations()[0].aux,1);
    OutputDestination duplicate=aux;duplicate.id=2;QVERIFY(!config.setDestinations({aux,duplicate}));QCOMPARE(config.destinations().size(),2);
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();
    QTRY_VERIFY(engine.outputsReady());QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(caspar.commands.contains("PLAY 35-1 route://11"));
    QVERIFY(caspar.commands.contains("ADD 35-1000 NDI NAME \"AUX monitor\""));
    QVERIFY(caspar.commands.contains("ADD 17-1001 SCREEN 2 FULLSCREEN NAME \"Clean display\""));
    caspar.commands.clear();QVERIFY(engine.setAuxSource(1,1003));QTRY_VERIFY(engine.outputsReady());QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(caspar.commands.contains("PLAY 35-1 route://17"));
    for(const auto& command:caspar.commands)QVERIFY(!command.startsWith("ADD ")&&!command.startsWith("REMOVE "));
    QVERIFY(!engine.setAuxSource(2,0));QVERIFY(!engine.setAuxSource(1,11));
    QVERIFY(config.setDestinations({}));engine.applyPreparedConfiguration();QTRY_VERIFY(engine.outputsReady());
    QTRY_COMPARE(client.queuedCommandCount(),0);QVERIFY(caspar.commands.contains("REMOVE 35-1000"));QVERIFY(caspar.commands.contains("REMOVE 17-1001"));
}
void TestSwitcherEngine::testSafeAreaPersistence()
{
    Configuration config;config.setSafePreviewAspect("9:16");config.setSafeProgramAspect("4:3");config.setSafePreset("legacy");
    Configuration restored;QVERIFY(restored.fromJson(config.toJson()));QCOMPARE(restored.safePreviewAspect(),QString("9:16"));QCOMPARE(restored.safeProgramAspect(),QString("4:3"));QCOMPARE(restored.safePreset(),QString("legacy"));
    restored.setSafePreviewAspect("invalid");QCOMPARE(restored.safePreviewAspect(),QString("9:16"));
    auto old=config.toJson();old.remove("multiview");Configuration legacy;QVERIFY(legacy.fromJson(old));QCOMPARE(legacy.safePreviewAspect(),QString("16:9"));
}


void TestSwitcherEngine::testSuperSourceCompositionAndBindings() {
    FakeCaspar peer;Configuration config;config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());config.setOscPort(0);
    for(int id=0;id<2;++id){auto src=*config.sourceById(id);src.type=SourceType::Matte;src.argument=id?"#00ff00":"#ff0000";config.setSource(src);}
    SuperSourceLayout layout;layout.id="ss";layout.name="Two-up";SuperSourceBox a;a.id="left";a.input=0;a.rect={0,0,.5,1};a.audio=true;auto b=a;b.id="right";b.input=1;b.rect={.5,0,.5,1};b.audio=false;layout.boxes={a,b};config.setSuperSources({layout});
    auto ss=*config.sourceById(6);ss.type=SourceType::SuperSource;ss.argument="ss";ss.enabled=true;config.setSource(ss);
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();
    QTRY_VERIFY(engine.sourceReady(6));QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(peer.commands.contains("PLAY 7-1 route://1-1"));QVERIFY(peer.commands.contains("PLAY 7-2 route://2-1"));
    QVERIFY(peer.commands.contains("MIXER 7-1 VOLUME 1"));QVERIFY(peer.commands.contains("MIXER 7-2 VOLUME 0"));
    engine.selectPreview(6);QTRY_COMPARE(engine.previewSource(),6);QTRY_VERIFY(peer.commands.contains("PLAY 9-1 route://7 RENDERED"));
    peer.commands.clear();QVERIFY(engine.setSuperSourceInput(6,"left",1));QTRY_VERIFY(engine.sourceReady(6));QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(peer.commands.contains("PLAY 7-1 route://2-1"));QVERIFY(!peer.commands.contains("CLEAR 7"));
    QVERIFY(!engine.setSuperSourceInput(6,"left",6)); // Self-cycle rejected.
    QVERIFY(!engine.setSuperSourceInput(6,"unknown",0));
    auto me=*config.sourceById(10);me.type=SourceType::MeProgram;me.argument="1";me.enabled=true;config.setSource(me);
    QVERIFY(!engine.setSuperSourceInput(6,"left",10)); // Composition cannot feed its own current ME.
    engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);
    QVERIFY(engine.setSuperSourceInput(6,"left",10));QTRY_VERIFY(engine.sourceReady(6));
    QVERIFY(!engine.sourceSelectable(6)); // Disabled on ME 1, available elsewhere.
    QVERIFY(engine.setSuperSourceInput(6,"left",-2));QTRY_VERIFY(engine.sourceReady(6));
    QVERIFY(engine.sourceSelectable(6));
    engine.selectPreview(6);QTRY_COMPARE(engine.previewSource(),6);
    QVERIFY(!engine.setSuperSourceInput(6,"left",1000)); // Own M/E must not feed itself.
    peer.commands.clear();QVERIFY(engine.setSuperSourceInput(6,"left",1001));
    QTRY_VERIFY(engine.sourceReady(6));QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(peer.commands.contains(QString("PLAY 7-1 route://13 RENDERED")));
    QVERIFY(!engine.setSuperSourceInput(6,"left",1004));
    QVERIFY(engine.setSuperSourceInput(6,"left",-2));
}
void TestSwitcherEngine::testSuperSourceBatchFailure() {
    FakeCaspar peer;Configuration config;config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());config.setOscPort(0);
    auto src=*config.sourceById(0);src.type=SourceType::Matte;src.argument="#ff0000";config.setSource(src);
    SuperSourceLayout layout;layout.id="ss";layout.name="One";SuperSourceBox box;box.id="box";box.input=0;layout.boxes={box};config.setSuperSources({layout});
    auto ss=*config.sourceById(6);ss.type=SourceType::SuperSource;ss.argument="ss";ss.enabled=true;config.setSource(ss);
    peer.failuresRemaining["PLAY 7-1 route://1-1"]=1;
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();
    QTRY_VERIFY(peer.commands.contains("PLAY 7-1 route://1-1"));QTRY_COMPARE(client.queuedCommandCount(),0);
    QVERIFY(!engine.sourceReady(6));
    engine.armMixer();QTRY_VERIFY(engine.sourceReady(6));
}


void TestSwitcherEngine::testSuperSourceBusContext(){
    FakeCaspar peer;Configuration config;config.setCasparHost("127.0.0.1");config.setCasparPort(peer.port());config.setOscPort(0);
    auto src=*config.sourceById(0);src.type=SourceType::Matte;src.argument="#ff0000";config.setSource(src);
    SuperSourceLayout layout;layout.id="ss";layout.name="SS";SuperSourceBox box;box.id="window";box.input=0;box.keyButton=0;layout.boxes={box};config.setSuperSources({layout});
    auto ss=*config.sourceById(6);ss.type=SourceType::SuperSource;ss.argument="ss";ss.enabled=true;config.setSource(ss);
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.sourceReady(6));engine.selectPreview(6);QTRY_COMPARE(engine.previewSource(),6);
    PanelProtocol panel(&engine);QVERIFY(panel.start(0));QTcpSocket remote;remote.connectToHost(QHostAddress::LocalHost,panel.port());QTRY_COMPARE(remote.state(),QAbstractSocket::ConnectedState);
    QByteArray buffer;connect(&remote,&QTcpSocket::readyRead,this,[&]{buffer+=remote.readAll();});QTest::qWait(20);buffer.clear();
    auto request=[&](QJsonObject object){buffer.clear();remote.write(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n');};
    QJsonObject message{{"cmd","supersource_input"},{"source",6},{"box","window"},{"input",-1},{"bus","program"},{"me",0},{"button",0}};
    request(message);QTRY_VERIFY(buffer.contains("error"));QVERIFY(!engine.superSourceBindings(6).contains("window"));
    message["bus"]="preview";message["button"]=1;request(message);QTRY_VERIFY(buffer.contains("error"));QVERIFY(!engine.superSourceBindings(6).contains("window"));
    message["button"]=0;request(message);QTRY_VERIFY(buffer.contains("supersource_binding"));QTRY_COMPARE(engine.superSourceBindings(6).value("window",-2),-1);
}

QTEST_GUILESS_MAIN(TestSwitcherEngine)
#include "test_switchengine.moc"

void TestSwitcherEngine::testNativeDmeMove() {
    FakeCaspar caspar;Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    SuperSourceLayout layout;layout.id="move";layout.name="Move target";SuperSourceBox box;box.id="left";box.input=0;box.keyButton=0;box.rect={0,0,.5,1};box.audio=true;layout.boxes={box};config.setSuperSources({layout});
    auto ss=*config.sourceById(6);ss.enabled=true;ss.type=SourceType::SuperSource;ss.argument="move";config.setSource(ss);
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.sourceReady(6));QTRY_VERIFY(!engine.isBusy());
    engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);
    engine.selectPreview(6);QTRY_COMPARE(engine.previewSource(),6);caspar.commands.clear();
    Transition move=Transition::mix(10);move.type=TransitionType::Move;
    QVERIFY(engine.setManualPosition(1800,move));QTRY_VERIFY(caspar.commands.join('\n').contains("DMENATIVE 10 MOVE MANUAL 1 REVERSE 0 SCENE"));
    QTRY_VERIFY(caspar.commands.join('\n').contains("PROGRESS 0.439560"));
    QVERIFY(engine.setManualPosition(4095,move));QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),6);QCOMPARE(engine.previewSource(),0);
    for(auto& command:caspar.commands)QVERIFY(!command.startsWith("MIXER 7-")); // Never animate the shared original.
    caspar.commands.clear();engine.executeTransition(move);QTRY_VERIFY(caspar.commands.join('\n').contains("DMENATIVE 10 MOVE MANUAL 0 REVERSE 0 SCENE"));QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),0);
    engine.setTransitionPreview(true);caspar.commands.clear();engine.executeTransition(move);QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),0);QVERIFY(caspar.commands.join('\n').contains("PLAY 9-101 route://7 RENDERED DMENATIVE"));
    engine.setTransitionPreview(false);QTRY_VERIFY(!engine.isBusy());
    Transition cube=move;cube.type=TransitionType::Cube;caspar.commands.clear();QVERIFY(engine.setManualPosition(1000,cube));QTRY_VERIFY(caspar.commands.join('\n').contains("DMENATIVE 10 CUBE MANUAL 1"));QVERIFY(engine.setManualPosition(0,cube));QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),0);
}

void TestSwitcherEngine::testNativeDmeRequiresPatch() {
    FakeCaspar caspar;caspar.engineVersion="2.5.1 Stable (casparMIX 0.4.2)";
    Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.isConnected());QTRY_VERIFY(!engine.isPreparing());
    QVERIFY(!engine.nativeDmeAvailable());QSignalSpy errors(&engine,&SwitcherEngine::error);
    Transition move=Transition::mix(25);move.type=TransitionType::Move;caspar.commands.clear();engine.executeTransition(move);QVERIFY(!engine.isBusy());QVERIFY(!errors.isEmpty());
    QVERIFY(!engine.setManualPosition(1000,move));
    for(auto& c:caspar.commands)QVERIFY(!c.contains("DMENATIVE"));
}

void TestSwitcherEngine::testNativePageDme() {
    FakeCaspar caspar;caspar.engineVersion="2.5.1 Stable (casparMIX 0.6.0)";
    Configuration config;config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.pageDmeAvailable());QTRY_VERIFY(!engine.isPreparing());
    engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
    for(auto type:{TransitionType::PageCurl,TransitionType::PageRoll}) {
        Transition page=Transition::mix(10);page.type=type;page.reverse=true;caspar.commands.clear();
        QVERIFY(engine.setManualPosition(2000,page));QTRY_VERIFY(caspar.commands.join('\n').contains(type==TransitionType::PageCurl?"PAGE_CURL MANUAL 1 REVERSE 1":"PAGE_ROLL MANUAL 1 REVERSE 1"));
        QVERIFY(engine.setManualPosition(0,page));QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),0);
        engine.setTransitionPreview(true);caspar.commands.clear();engine.executeTransition(page);QTRY_VERIFY(!engine.isBusy());QCOMPARE(engine.programSource(),0);QVERIFY(caspar.commands.join('\n').contains("PLAY 9-101 route://2 RENDERED DMENATIVE"));engine.setTransitionPreview(false);QTRY_VERIFY(!engine.isBusy());
    }
}

void TestSwitcherEngine::testDmeBackgrounds() {
    QTemporaryDir dir;FakeCaspar caspar;caspar.engineVersion="2.5.1 Stable (casparMIX 0.6.0)";
    Configuration config;config.setConfigFilePath(dir.filePath("settings.json"));config.setOscPort(0);config.setCasparHost("127.0.0.1");config.setCasparPort(caspar.port());
    for(int id=0;id<3;++id){auto source=*config.sourceById(id);source.type=SourceType::Matte;source.argument=id==2?"#0000ff":"#ff0000";source.enabled=true;config.setSource(source);}
    AmcpClient client;client.setReconnectIntervalMs(0);SwitcherEngine engine(&config,&client);engine.connectToCaspar();QTRY_VERIFY(engine.pageDmeAvailable());QTRY_VERIFY(!engine.isPreparing());
    QVERIFY(engine.setDmeBackground("cube",2));QVERIFY(!engine.setDmeBackground("cube",1000));QVERIFY(!engine.setDmeBackground("unknown",0));
    Configuration restored;restored.setConfigFilePath(config.configFilePath());QVERIFY(restored.load());QCOMPARE(restored.dmeBackground("cube"),2);
    engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
    Transition cube=Transition::mix(20);cube.type=TransitionType::Cube;caspar.commands.clear();engine.executeTransition(cube);
    const QString hex=QString::fromLatin1(QByteArray("route://3 RENDERED").toHex());QTRY_VERIFY(caspar.commands.join('\n').contains("BACKGROUND "+hex));
    unsigned before=caspar.commands.size();QVERIFY(engine.setDmeBackground("cube",0));for(auto& command:caspar.commands.mid(before))QVERIFY(!command.startsWith("CALL 10-1"));QTRY_VERIFY(!engine.isBusy());
    engine.setTransitionPreview(true);QVERIFY(engine.setManualPosition(2000,cube));QTRY_VERIFY(engine.isManualTransition());QVERIFY(engine.setDmeBackground("cube",2));QTRY_VERIFY(caspar.commands.join('\n').contains("CALL 9-101 \"BACKGROUND "+hex));QCOMPARE(engine.programSource(),1);
    QVERIFY(engine.setManualPosition(0,cube));QTRY_VERIFY(!engine.isBusy());engine.setTransitionPreview(false);QTRY_VERIFY(!engine.isBusy());
    engine.setActiveMe(1);QTRY_COMPARE(engine.activeMe(),1);QVERIFY(engine.setDmeBackground("cube",1000));engine.selectPreview(0);QTRY_COMPARE(engine.previewSource(),0);engine.executeCut();QTRY_COMPARE(engine.programSource(),0);engine.selectPreview(1);QTRY_COMPARE(engine.previewSource(),1);
    QVERIFY(engine.setManualPosition(2000,cube));engine.setActiveMe(0);QTRY_COMPARE(engine.activeMe(),0);QVERIFY(!engine.setDmeBackground("cube",1001)); // Frozen M/E 2 already depends on M/E 1.
}
