#pragma once

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QMap>

class VlcPlayer;
class Configuration;

class StreamManager : public QObject
{
    Q_OBJECT

public:
    explicit StreamManager(Configuration* config, QObject* parent = nullptr);
    ~StreamManager() override;

    void startStreams();
    void stopStreams();

    VlcPlayer* playerForSource(int sourceIndex) const;
    VlcPlayer* programPlayer() const { return m_programPlayer; }
    VlcPlayer* previewPlayer() const { return m_previewPlayer; }

    int streamPort(int sourceIndex) const;

signals:
    void streamStarted(int sourceIndex, int port);
    void streamStopped(int sourceIndex);
    void error(const QString& message);

private:
    Configuration* m_config = nullptr;
    QList<VlcPlayer*> m_sourcePlayers;
    VlcPlayer* m_programPlayer = nullptr;
    VlcPlayer* m_previewPlayer = nullptr;
    QMap<int, int> m_sourcePortMap; // sourceIndex -> UDP port
};
