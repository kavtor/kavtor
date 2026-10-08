#include "Logging.h"
#include "AmcpClient.h"

#include <QTcpSocket>
#include <QTimer>
#include <QDebug>
#include <QScopedValueRollback>

AmcpClient::AmcpClient(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &AmcpClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &AmcpClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &AmcpClient::onSocketError);
    connect(m_socket, &QTcpSocket::readyRead, this, &AmcpClient::onSocketReadyRead);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &AmcpClient::onReconnectTimeout);

    m_responseTimer = new QTimer(this);
    m_responseTimer->setSingleShot(true);
    connect(m_responseTimer, &QTimer::timeout, this, &AmcpClient::onResponseTimeout);
}

AmcpClient::~AmcpClient() = default;

void AmcpClient::connectToHost(const QString& host, int port)
{
    if ((m_connected || isConnecting()) && (host != m_host || port != m_port)) {
        disconnectFromHost();
    }
    m_host = host;
    m_port = port;
    m_wantConnected = true;
    m_reconnectTimer->stop();

    if (m_connected || m_socket->state() == QAbstractSocket::ConnectingState
        || m_socket->state() == QAbstractSocket::HostLookupState) {
        return;
    }

    m_socket->connectToHost(host, port);
}

void AmcpClient::disconnectFromHost()
{
    m_wantConnected = false;
    m_reconnectTimer->stop();
    clearQueue(QStringLiteral("Disconnected"));
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    if (m_connected) {
        m_connected = false;
        emit disconnected();
    }
}

bool AmcpClient::isConnected() const
{
    return m_connected;
}

bool AmcpClient::isConnecting() const
{
    return m_wantConnected && !m_connected;
}

void AmcpClient::sendCommand(const QString& command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!m_connected && !m_wantConnected) {
        emit commandFailed(trimmed, QStringLiteral("Not connected"));
        return;
    }
    if (trimmed.contains(QLatin1Char('\r')) || trimmed.contains(QLatin1Char('\n'))) {
        emit commandFailed(trimmed, QStringLiteral("AMCP command must be a single line"));
        return;
    }
    m_queue.enqueue({trimmed, 0});
    flushQueue();
}

quint64 AmcpClient::sendBatch(const QStringList& commands)
{
    if (commands.isEmpty() || !m_connected) {
        return 0;
    }
    QStringList normalized;
    for (const QString& command : commands) {
        const QString text = command.trimmed();
        if (text.isEmpty() || text.contains(QLatin1Char('\r')) || text.contains(QLatin1Char('\n'))) {
            emit commandFailed(command, QStringLiteral("Invalid AMCP batch command"));
            return 0;
        }
        normalized.append(text);
    }
    const quint64 batch = ++m_nextBatch;
    m_batchRemaining.insert(batch, normalized.size());
    for (const QString& text : normalized) {
        m_queue.enqueue({text, batch});
    }
    flushQueue();
    return batch;
}

int AmcpClient::queuedCommandCount() const
{
    return m_queue.size() + (m_inFlight ? 1 : 0);
}

void AmcpClient::setReconnectIntervalMs(int ms)
{
    m_reconnectIntervalMs = qMax(0, ms);
}

void AmcpClient::setResponseTimeoutMs(int ms)
{
    m_responseTimeoutMs = qMax(100, ms);
}

void AmcpClient::injectReceivedData(const QByteArray& data)
{
    m_buffer.append(data);
    while (true) {
        const int pos = m_buffer.indexOf("\r\n");
        if (pos < 0) {
            break;
        }
        const QString line = QString::fromUtf8(m_buffer.left(pos));
        m_buffer.remove(0, pos + 2);
        parseLine(line);
    }
}

void AmcpClient::onSocketConnected()
{
    m_connected = true;
    resetParser();
    m_buffer.clear();
    emit connected();
    flushQueue();
}

void AmcpClient::onSocketDisconnected()
{
    const bool wasConnected = m_connected;
    m_connected = false;
    failInFlight(QStringLiteral("Connection closed"));
    clearQueue(QStringLiteral("Connection closed"));
    if (wasConnected) {
        emit disconnected();
    }
    scheduleReconnect();
}

void AmcpClient::onSocketError()
{
    if (m_socket->error() == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    emit connectionFailed(m_socket->errorString());
    if (!m_connected) {
        scheduleReconnect();
    }
}

void AmcpClient::onSocketReadyRead()
{
    injectReceivedData(m_socket->readAll());
}

void AmcpClient::onReconnectTimeout()
{
    if (!m_wantConnected || m_connected) {
        return;
    }
    m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void AmcpClient::onResponseTimeout()
{
    // AMCP replies carry no request ID. A late reply cannot be distinguished
    // from the reply to the next command, so retire the entire TCP session.
    const bool wasConnected = m_connected;
    m_connected = false;
    failInFlight(QStringLiteral("AMCP response timeout; execution state is unknown"));
    clearQueue(QStringLiteral("Discarded after AMCP response timeout"));
    resetParser();
    m_buffer.clear();
    m_socket->abort();
    if (wasConnected) {
        emit disconnected();
    }
    scheduleReconnect();
}

void AmcpClient::resetParser()
{
    m_xmlReader.clear();
    m_parserState = ParserState::ExpectingHeader;
    m_responseCode = 0;
    m_responseStatus.clear();
    m_responseLines.clear();
}

void AmcpClient::parseLine(const QString& line)
{
    switch (m_parserState) {
    case ParserState::ExpectingHeader: {
        if (line.isEmpty()) {
            return;
        }
        const QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.size() < 2) {
            qWarning() << "AmcpClient: Malformed header:" << line;
            resetParser();
            return;
        }
        bool ok = false;
        m_responseCode = tokens.first().toInt(&ok);
        if (!ok) {
            qWarning() << "AmcpClient: Invalid code in header:" << line;
            resetParser();
            return;
        }
        m_responseStatus = tokens.mid(1).join(QLatin1Char(' '));
        m_responseLines.append(line);

        if (m_responseCode == 200) {
            m_parserState = ParserState::ExpectingMultiline;
        } else if (m_responseCode == 201 || m_responseCode == 400) {
            m_parserState = ParserState::ExpectingTwoline;
        } else {
            processResponse();
        }
        break;
    }
    case ParserState::ExpectingTwoline:
        if (m_responseCode == 201 && line.trimmed().startsWith(QLatin1Char('<'))) {
            // INFO uses 201 with an XML document. Accept native LF inside the
            // payload as well as CRLF-normalised XML without releasing the next
            // queued command before the complete document has arrived.
            m_parserState = ParserState::ExpectingXml;
            parseLine(line);
        } else {
            m_responseLines.append(line);
            processResponse();
        }
        break;
    case ParserState::ExpectingXml:
        m_responseLines.append(line);
        m_xmlReader.addData(line + QLatin1Char('\n'));
        while (!m_xmlReader.atEnd()) {
            if (m_xmlReader.readNext() == QXmlStreamReader::EndDocument) {
                processResponse();
                break;
            }
        }
        // Incomplete XML resumes on the next line. Malformed XML remains in
        // flight until the response timeout retires this ambiguous session.
        break;
    case ParserState::ExpectingMultiline:
        if (line.isEmpty()) {
            processResponse();
        } else {
            m_responseLines.append(line);
        }
        break;
    }
}

void AmcpClient::processResponse()
{
    m_responseTimer->stop();
    const bool wasDispatching = m_dispatchingResponse;
    QScopedValueRollback<bool> dispatchGuard(m_dispatchingResponse, true);
    const QString command = m_inFlightCommand;
    const quint64 batch = m_inFlightBatch;
    m_inFlightCommand.clear();
    m_inFlightBatch = 0;
    m_inFlight = false;

    const int code = m_responseCode;
    const QString status = m_responseStatus;
    const QStringList lines = m_responseLines;
    resetParser();
    emit responseReceived(code, status, lines, command);
    if (code >= 400 && batch != 0) {
        cancelQueuedBatch(batch, QStringLiteral("Cancelled after batch command failure"));
    }
    completeBatchCommand(batch, command, code < 400);
    m_dispatchingResponse = wasDispatching;
    flushQueue();
}

void AmcpClient::flushQueue()
{
    if (!m_connected || m_dispatchingResponse || m_inFlight || m_queue.isEmpty()) {
        return;
    }
    const QueuedCommand next = m_queue.dequeue();
    m_inFlightCommand = next.text;
    m_inFlightBatch = next.batch;
    m_inFlight = true;
    writeCurrentCommand();
}

void AmcpClient::writeCurrentCommand()
{
    const QByteArray data = (m_inFlightCommand + QStringLiteral("\r\n")).toUtf8();
    m_socket->write(data);
    m_socket->flush();
    if (m_responseTimeoutMs > 0) {
        m_responseTimer->start(m_responseTimeoutMs);
    }
    qCDebug(kavtorLog) << "AmcpClient: Sent command:" << m_inFlightCommand;
}

void AmcpClient::failInFlight(const QString& error)
{
    m_responseTimer->stop();
    if (!m_inFlight) {
        return;
    }
    const QString command = m_inFlightCommand;
    const quint64 batch = m_inFlightBatch;
    m_inFlight = false;
    m_inFlightBatch = 0;
    m_inFlightCommand.clear();
    resetParser();
    emit commandFailed(command, error);
    completeBatchCommand(batch, command, false);
}

void AmcpClient::clearQueue(const QString& error)
{
    while (!m_queue.isEmpty()) {
        const QueuedCommand command = m_queue.dequeue();
        emit commandFailed(command.text, error);
        completeBatchCommand(command.batch, command.text, false);
    }
}

void AmcpClient::cancelQueuedBatch(quint64 batch, const QString& error)
{
    QQueue<QueuedCommand> retained;
    QList<QueuedCommand> cancelled;
    while (!m_queue.isEmpty()) {
        const QueuedCommand command = m_queue.dequeue();
        if (command.batch == batch) {
            cancelled.append(command);
        } else {
            retained.enqueue(command);
        }
    }
    m_queue = retained;
    for (const QueuedCommand& command : cancelled) {
        emit commandFailed(command.text, error);
        completeBatchCommand(batch, command.text, false);
    }
}

void AmcpClient::completeBatchCommand(quint64 batch, const QString& command, bool success)
{
    if (batch == 0 || !m_batchRemaining.contains(batch)) {
        return;
    }
    if (!success) {
        m_failedBatches.insert(batch);
    }
    emit batchCommandFinished(batch, command, success);
    // Callbacks may close the connection and finish other commands in this batch.
    auto remaining = m_batchRemaining.find(batch);
    if (remaining == m_batchRemaining.end()) {
        return;
    }
    if (--remaining.value() == 0) {
        m_batchRemaining.erase(remaining);
        const bool succeeded = !m_failedBatches.remove(batch);
        emit batchFinished(batch, succeeded);
    }
}

void AmcpClient::scheduleReconnect()
{
    if (!m_wantConnected || m_reconnectIntervalMs <= 0 || m_reconnectTimer->isActive()) {
        return;
    }
    m_reconnectTimer->start(m_reconnectIntervalMs);
}
