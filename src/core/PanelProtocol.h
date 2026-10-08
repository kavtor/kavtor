#pragma once

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QJsonObject>

class QTcpServer;
class QTcpSocket;
class SwitcherEngine;

class PanelProtocol : public QObject
{
    Q_OBJECT

public:
    explicit PanelProtocol(SwitcherEngine* engine, QObject* parent = nullptr);
    ~PanelProtocol() override;

    bool start(quint16 port);
    void stop();
    bool isListening() const;
    quint16 port() const;

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onClientReadyRead();
    void onPreviewChanged(int sourceId);
    void onProgramChanged(int sourceId);
    void onConnectionChanged(bool connected);
    void onTransitioningChanged(bool transitioning);
    void onDskChanged();
    void onEngineError(const QString& message);

private:
    void handleLine(QTcpSocket* client, const QString& line);
    void broadcast(const QJsonObject& object);
    void sendTo(QTcpSocket* client, const QJsonObject& object);
    QJsonObject stateObject() const;
    QJsonObject tallyObject() const;

    SwitcherEngine* m_engine = nullptr;
    QTcpServer* m_server = nullptr;
    QList<QTcpSocket*> m_clients;
    QTcpSocket* m_manualOwner = nullptr;
};
