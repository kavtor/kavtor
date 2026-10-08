#include "StreamManager.h"
#include "config/Configuration.h"
#include "video/VlcPlayer.h"

#include <QtCore/QDebug>

StreamManager::StreamManager(Configuration* config, QObject* parent)
    : QObject(parent)
    , m_config(config)
{
    // Create players for 8 source thumbnails
    for (int i = 0; i < 8; ++i) {
        VlcPlayer* player = new VlcPlayer;
        m_sourcePlayers.append(player);
        m_sourcePortMap[i] = 5001 + i;
    }

    // Create players for program and preview windows
    m_programPlayer = new VlcPlayer;
    m_previewPlayer = new VlcPlayer;
}

StreamManager::~StreamManager()
{
    stopStreams();
    qDeleteAll(m_sourcePlayers);
    delete m_programPlayer;
    delete m_previewPlayer;
}

void StreamManager::startStreams()
{
    // Start all source thumbnail streams
    for (int i = 0; i < m_sourcePlayers.size(); ++i) {
        VlcPlayer* player = m_sourcePlayers.at(i);
        int port = m_sourcePortMap.value(i);
        if (player->playUdpStream(port)) {
            emit streamStarted(i, port);
        } else {
            emit error(tr("Failed to start stream for source %1 on port %2").arg(i).arg(port));
        }
    }
}

void StreamManager::stopStreams()
{
    for (int i = 0; i < m_sourcePlayers.size(); ++i) {
        m_sourcePlayers.at(i)->stop();
        emit streamStopped(i);
    }
    m_programPlayer->stop();
    m_previewPlayer->stop();
}

VlcPlayer* StreamManager::playerForSource(int sourceIndex) const
{
    if (sourceIndex >= 0 && sourceIndex < m_sourcePlayers.size()) {
        return m_sourcePlayers.at(sourceIndex);
    }
    return nullptr;
}

int StreamManager::streamPort(int sourceIndex) const
{
    return m_sourcePortMap.value(sourceIndex, -1);
}
