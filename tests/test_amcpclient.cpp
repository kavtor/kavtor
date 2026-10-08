#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>

#include "core/AmcpClient.h"

class TestAmcpClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testConnectAndDisconnect();
    void testSendCommand();
    void testCommandQueue();
    void testParseOnelineResponse();
    void testParseTwolineResponse();
    void testParseMultilineResponse();
    void testParseXmlResponse();
    void testMalformedXmlRetiresSession();
    void testXmlReplyKeepsQueueBlocked();
    void testTimeoutRetiresSession();
    void testSwitchEndpoint();
    void testRejectMultipleCommands();
    void testReentrantResponse();
    void testFailedBatchCancelsOnlyItsRemainder();

private:
    QTcpServer* m_testServer = nullptr;
    int m_testPort = 0;
};

void TestAmcpClient::initTestCase()
{
    m_testServer = new QTcpServer(this);
    QVERIFY(m_testServer->listen(QHostAddress::LocalHost));
    m_testPort = m_testServer->serverPort();
}

void TestAmcpClient::cleanupTestCase()
{
    delete m_testServer;
    m_testServer = nullptr;
}

void TestAmcpClient::testConnectAndDisconnect()
{
    AmcpClient client;
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    QSignalSpy disconnectedSpy(&client, &AmcpClient::disconnected);

    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QVERIFY(client.isConnected());
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* serverSocket = m_testServer->nextPendingConnection();
    QVERIFY(serverSocket);

    client.disconnectFromHost();
    QTRY_VERIFY(!client.isConnected());
    QVERIFY(disconnectedSpy.count() >= 1);
    serverSocket->deleteLater();
}

void TestAmcpClient::testSendCommand()
{
    AmcpClient client;
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* serverSocket = m_testServer->nextPendingConnection();
    QVERIFY(serverSocket);

    QSignalSpy readySpy(serverSocket, &QTcpSocket::readyRead);
    client.sendCommand(QStringLiteral("INFO"));
    QVERIFY(readySpy.wait(1000));
    const QByteArray data = serverSocket->readAll();
    QVERIFY(data.endsWith("\r\n"));
    QVERIFY(data.contains("INFO"));
    serverSocket->deleteLater();
}

void TestAmcpClient::testCommandQueue()
{
    AmcpClient client;
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    QSignalSpy responseSpy(&client, &AmcpClient::responseReceived);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* serverSocket = m_testServer->nextPendingConnection();
    QVERIFY(serverSocket);

    client.sendCommand(QStringLiteral("INFO"));
    client.sendCommand(QStringLiteral("VERSION"));
    QVERIFY(serverSocket->waitForReadyRead(1000));
    QCOMPARE(QString::fromUtf8(serverSocket->readAll()).trimmed(), QStringLiteral("INFO"));

    serverSocket->write("202 INFO OK\r\n");
    serverSocket->flush();
    QVERIFY(responseSpy.wait(1000));
    QCOMPARE(responseSpy.takeFirst().at(3).toString(), QStringLiteral("INFO"));

    QVERIFY(serverSocket->waitForReadyRead(1000));
    QCOMPARE(QString::fromUtf8(serverSocket->readAll()).trimmed(), QStringLiteral("VERSION"));
    serverSocket->write("202 VERSION OK\r\n");
    serverSocket->flush();
    QVERIFY(responseSpy.wait(1000));
    QCOMPARE(responseSpy.takeFirst().at(3).toString(), QStringLiteral("VERSION"));
    serverSocket->deleteLater();
}

void TestAmcpClient::testParseOnelineResponse()
{
    AmcpClient client;
    QSignalSpy spy(&client, &AmcpClient::responseReceived);
    client.injectReceivedData("202 PLAY OK\r\n");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 202);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("PLAY OK"));
}

void TestAmcpClient::testParseTwolineResponse()
{
    AmcpClient client;
    QSignalSpy spy(&client, &AmcpClient::responseReceived);
    client.injectReceivedData("201 VERSION OK\r\n2.5.0\r\n");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 201);
    const QStringList lines = spy.at(0).at(2).toStringList();
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(1), QStringLiteral("2.5.0"));
}

void TestAmcpClient::testParseMultilineResponse()
{
    AmcpClient client;
    QSignalSpy spy(&client, &AmcpClient::responseReceived);
    client.injectReceivedData("200 CLS OK\r\nclip1\r\nclip2\r\n\r\n");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 200);
    const QStringList lines = spy.at(0).at(2).toStringList();
    QVERIFY(lines.contains(QStringLiteral("clip1")));
    QVERIFY(lines.contains(QStringLiteral("clip2")));
}



void TestAmcpClient::testParseXmlResponse()
{
    AmcpClient client;
    QSignalSpy spy(&client, &AmcpClient::responseReceived);
    client.injectReceivedData("201 INFO OK\r\n<?xml version=\"1.0\"?>\r\n<channel>\r\n");
    QCOMPARE(spy.count(), 0);
    client.injectReceivedData("\r\n  <stage><layer><layer_1><foreground>\r\n");
    QCOMPARE(spy.count(), 0);
    client.injectReceivedData("<producer>route</producer></foreground></layer_1></layer></stage>\r\n</channel>\r\n\r\n202 OK\r\n");
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toInt(), 201);
    QVERIFY(spy.at(0).at(2).toStringList().join(QLatin1Char('\n')).contains(QStringLiteral("</channel>")));
    QCOMPARE(spy.at(1).at(0).toInt(), 202);

    // Native CasparCG pretty XML uses LF, followed by an AMCP CRLF terminator.
    client.injectReceivedData("201 INFO OK\r\n<?xml version=\"1.0\"?>\n<channel>\n<format>1080p5000</format>\n</channel>\n\r\n");
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(2).at(0).toInt(), 201);
}

void TestAmcpClient::testXmlReplyKeepsQueueBlocked()
{
    AmcpClient client;
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    QSignalSpy responseSpy(&client, &AmcpClient::responseReceived);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* peer = m_testServer->nextPendingConnection();
    client.sendCommand(QStringLiteral("INFO 10"));
    client.sendCommand(QStringLiteral("VERSION"));
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("INFO 10\r\n"));
    peer->write("201 INFO OK\r\n<?xml version=\"1.0\"?>\r\n<channel>\r\n");
    peer->flush();
    QTest::qWait(30);
    QCOMPARE(responseSpy.count(), 0);
    QCOMPARE(peer->bytesAvailable(), qint64(0));
    peer->write("<format>1080p5000</format>\r\n</channel>\r\n\r\n");
    peer->flush();
    QTRY_COMPARE(responseSpy.count(), 1);
    QCOMPARE(responseSpy.first().at(3).toString(), QStringLiteral("INFO 10"));
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("VERSION\r\n"));
    peer->write("201 VERSION OK\r\n2.5.1\r\n");
    peer->flush();
    QTRY_COMPARE(responseSpy.count(), 2);
    QCOMPARE(responseSpy.last().at(3).toString(), QStringLiteral("VERSION"));
    peer->deleteLater();
}

void TestAmcpClient::testMalformedXmlRetiresSession()
{
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    client.setResponseTimeoutMs(100);
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    QSignalSpy responseSpy(&client, &AmcpClient::responseReceived);
    QSignalSpy disconnectedSpy(&client, &AmcpClient::disconnected);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* peer = m_testServer->nextPendingConnection();
    client.sendCommand(QStringLiteral("INFO 10"));
    client.sendCommand(QStringLiteral("CLEAR 10"));
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("INFO 10\r\n"));
    peer->write("201 INFO OK\r\n<channel>\r\n</wrong>\r\n");
    peer->flush();
    QTRY_COMPARE(disconnectedSpy.count(), 1);
    QCOMPARE(responseSpy.count(), 0);
    QCOMPARE(peer->readAll(), QByteArray());
    QCOMPARE(client.queuedCommandCount(), 0);
    peer->deleteLater();
}

void TestAmcpClient::testTimeoutRetiresSession()
{
    AmcpClient client;
    client.setReconnectIntervalMs(0);
    client.setResponseTimeoutMs(100);
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    QSignalSpy failedSpy(&client, &AmcpClient::commandFailed);
    QSignalSpy disconnectedSpy(&client, &AmcpClient::disconnected);
    QSignalSpy responseSpy(&client, &AmcpClient::responseReceived);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* peer = m_testServer->nextPendingConnection();
    client.sendCommand(QStringLiteral("PLAY 10-1 route://1"));
    client.sendCommand(QStringLiteral("PLAY 10-1 route://2"));
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("PLAY 10-1 route://1\r\n"));
    // A partial response must not survive into a fresh connection either.
    peer->write("202 PLAY");
    peer->flush();
    QTRY_COMPARE(disconnectedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 2);
    QCOMPARE(failedSpy.at(0).at(0).toString(), QStringLiteral("PLAY 10-1 route://1"));
    QVERIFY(failedSpy.at(0).at(1).toString().contains(QStringLiteral("unknown")));
    QCOMPARE(failedSpy.at(1).at(0).toString(), QStringLiteral("PLAY 10-1 route://2"));
    QCOMPARE(responseSpy.count(), 0);
    QCOMPARE(client.queuedCommandCount(), 0);
    QVERIFY(!client.isConnected());
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    QCOMPARE(peer->readAll(), QByteArray());
    peer->deleteLater();

    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    peer = m_testServer->nextPendingConnection();
    client.sendCommand(QStringLiteral("VERSION"));
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("VERSION\r\n"));
    peer->write("201 VERSION OK\r\n2.5.0\r\n");
    peer->flush();
    QTRY_COMPARE(responseSpy.count(), 1);
    QCOMPARE(responseSpy.at(0).at(3).toString(), QStringLiteral("VERSION"));
    client.disconnectFromHost();
    peer->deleteLater();
}

void TestAmcpClient::testSwitchEndpoint()
{
    QTcpServer other;
    QVERIFY(other.listen(QHostAddress::LocalHost));
    AmcpClient client;
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* original = m_testServer->nextPendingConnection();
    client.connectToHost(QStringLiteral("127.0.0.1"), other.serverPort());
    QTRY_COMPARE(connectedSpy.count(), 2);
    QTRY_VERIFY(other.hasPendingConnections());
    QTcpSocket* replacement = other.nextPendingConnection();
    client.sendCommand(QStringLiteral("VERSION"));
    QTRY_VERIFY(replacement->bytesAvailable() > 0);
    QCOMPARE(replacement->readAll(), QByteArray("VERSION\r\n"));
    QTRY_COMPARE(original->state(), QAbstractSocket::UnconnectedState);
    client.disconnectFromHost();
    original->deleteLater();
    replacement->deleteLater();
}

void TestAmcpClient::testRejectMultipleCommands()
{
    AmcpClient client;
    QSignalSpy failedSpy(&client, &AmcpClient::commandFailed);
    QSignalSpy connectedSpy(&client, &AmcpClient::connected);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connectedSpy.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* peer = m_testServer->nextPendingConnection();
    client.sendCommand(QStringLiteral("INFO\r\nCLEAR 10"));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(client.queuedCommandCount(), 0);
    QCOMPARE(peer->bytesAvailable(), qint64(0));
    client.disconnectFromHost();
    peer->deleteLater();
}

void TestAmcpClient::testReentrantResponse()
{
    AmcpClient client;
    QSignalSpy responseSpy(&client, &AmcpClient::responseReceived);
    bool injected = false;
    connect(&client, &AmcpClient::responseReceived, &client, [&]() {
        if (!injected) {
            injected = true;
            client.injectReceivedData("202 PLAY OK\r\n");
        }
    });
    client.injectReceivedData("201 VERSION OK\r\n2.5.0\r\n");
    QCOMPARE(responseSpy.count(), 2);
    QCOMPARE(responseSpy.at(0).at(0).toInt(), 201);
    QCOMPARE(responseSpy.at(1).at(0).toInt(), 202);
}


void TestAmcpClient::testFailedBatchCancelsOnlyItsRemainder()
{
    AmcpClient client;
    QSignalSpy connected(&client, &AmcpClient::connected);
    QSignalSpy finished(&client, &AmcpClient::batchFinished);
    client.connectToHost(QStringLiteral("127.0.0.1"), m_testPort);
    QVERIFY(connected.wait(1000));
    QTRY_VERIFY(m_testServer->hasPendingConnections());
    QTcpSocket* peer = m_testServer->nextPendingConnection();
    const quint64 batch = client.sendBatch({QStringLiteral("PLAY 10-1 bad"), QStringLiteral("CLEAR 10-11")});
    QVERIFY(batch != 0);
    client.sendCommand(QStringLiteral("VERSION"));
    connect(&client, &AmcpClient::responseReceived, &client,
            [&client](int code, const QString&, const QStringList&, const QString&) {
        if (code >= 400) {
            client.sendCommand(QStringLiteral("INFO"));
        }
    });
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("PLAY 10-1 bad\r\n"));
    peer->write("502 PLAY FAILED\r\n");
    peer->flush();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toULongLong(), batch);
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("VERSION\r\n"));
    peer->write("201 VERSION OK\r\n2.5.0\r\n");
    peer->flush();
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    QCOMPARE(peer->readAll(), QByteArray("INFO\r\n"));
    peer->write("202 INFO OK\r\n");
    peer->flush();
    QTRY_COMPARE(client.queuedCommandCount(), 0);
    QVERIFY(client.isConnected());
    client.disconnectFromHost();
    peer->deleteLater();
}

QTEST_MAIN(TestAmcpClient)
#include "test_amcpclient.moc"
