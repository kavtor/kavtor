#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QQueue>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QXmlStreamReader>

class QTcpSocket;
class QTimer;

class AmcpClient : public QObject
{
    Q_OBJECT

public:
    explicit AmcpClient(QObject* parent = nullptr);
    ~AmcpClient() override;

    void connectToHost(const QString& host, int port);
    void disconnectFromHost();
    bool isConnected() const;
    bool isConnecting() const;

    void sendCommand(const QString& command);
    // A failure cancels the unsent remainder of this batch, not unrelated work.
    quint64 sendBatch(const QStringList& commands);
    int queuedCommandCount() const;

    void setReconnectIntervalMs(int ms);
    int reconnectIntervalMs() const { return m_reconnectIntervalMs; }
    void setResponseTimeoutMs(int ms);

    QString host() const { return m_host; }
    int port() const { return m_port; }

    void injectReceivedData(const QByteArray& data);

signals:
    void connected();
    void disconnected();
    void connectionFailed(const QString& error);
    void responseReceived(int code, const QString& status, const QStringList& lines, const QString& command);
    void commandFailed(const QString& command, const QString& error);
    void batchCommandFinished(quint64 batch, const QString& command, bool success);
    void batchFinished(quint64 batch, bool success);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError();
    void onSocketReadyRead();
    void onReconnectTimeout();
    void onResponseTimeout();

private:
    void resetParser();
    void parseLine(const QString& line);
    void processResponse();
    void flushQueue();
    void writeCurrentCommand();
    void failInFlight(const QString& error);
    void clearQueue(const QString& error);
    void scheduleReconnect();
    void cancelQueuedBatch(quint64 batch, const QString& error);
    void completeBatchCommand(quint64 batch, const QString& command, bool success);

    QTcpSocket* m_socket = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_responseTimer = nullptr;
    QString m_host;
    int m_port = 0;
    bool m_connected = false;
    bool m_wantConnected = false;
    int m_reconnectIntervalMs = 10000;
    int m_responseTimeoutMs = 5000;

    struct QueuedCommand { QString text; quint64 batch = 0; };
    QQueue<QueuedCommand> m_queue;
    quint64 m_nextBatch = 0;
    quint64 m_inFlightBatch = 0;
    QHash<quint64, int> m_batchRemaining;
    QSet<quint64> m_failedBatches;
    bool m_dispatchingResponse = false;
    QString m_inFlightCommand;
    bool m_inFlight = false;

    enum class ParserState {
        ExpectingHeader,
        ExpectingTwoline,
        ExpectingMultiline,
        ExpectingXml
    };
    ParserState m_parserState = ParserState::ExpectingHeader;
    int m_responseCode = 0;
    QString m_responseStatus;
    QStringList m_responseLines;
    QXmlStreamReader m_xmlReader;
    QByteArray m_buffer;
};
