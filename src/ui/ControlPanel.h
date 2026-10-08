#pragma once

#include "config/Configuration.h"
#include "core/Transition.h"

#include <QtWidgets/QWidget>
#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QVector>

class QPushButton;
class QLabel;
class QComboBox;
class QSpinBox;
class QButtonGroup;
class QTimer;

class ControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);
    ~ControlPanel() override = default;

    void setSources(const QList<Source>& sources);
    void setConnectionStatus(bool connected);
    void setServerInfo(const QString& host, int amcpPort, int panelPort, int oscPort);
    void setOutputFeeds(bool ndiProgramOn, const QString& ndiProgramName,
                        bool ndiCleanOn, const QString& ndiCleanName);
    void setDurationFrames(int frames);
    void setPreviewSource(int sourceId, const QString& name = QString());
    void setProgramSource(int sourceId, const QString& name = QString());
    void setDeckSource(int sourceId);
    void setCuedSource(int sourceId);
    void setTransitioning(bool transitioning);
    void setActiveTake(const QString& take);
    void setDskOn(bool on);
    void setDskPreview(bool on);
    void setWipePattern(const QString& patternId);
    void setWipeDirectionMode(WipeDirectionMode mode);
    void setWipeSenseForward(bool forward);
    void setWipeEdge(WipeEdgeMode mode, int amount, const QString& color);

signals:
    void previewRequested(int sourceId);
    void programRequested(int sourceId);
    void cutRequested();
    void autoRequested();
    void wipeRequested();
    void wipePatternChanged(const QString& patternId);
    void wipeDirectionChanged(WipeDirectionMode mode);
    void wipeEdgeChanged(WipeEdgeMode mode, int amount, const QString& color);
    void dskToggled(bool on);
    void dskPreviewToggled(bool on);
    void connectRequested();
    void disconnectRequested();
    void sourcesEditRequested();
    void durationChanged(int frames);

private slots:
    void onCutClicked();
    void onAutoClicked();
    void onWipeClicked();
    void onWipePatternIndexChanged();
    void onWipeDirectionClicked();
    void onWipeEdgeChanged();
    void onWipeColorClicked();
    void onConnectClicked();
    void onPreviewClicked();
    void onProgramClicked();

private:
    void updateTally();
    void updateActionEnabled();
    void applyTally(QPushButton* button, const QString& tally);
    QString busText(const QString& bus, int sourceId, const QString& name) const;

    QPushButton* m_cutButton = nullptr;
    QPushButton* m_autoButton = nullptr;
    QPushButton* m_wipeButton = nullptr;
    QComboBox* m_wipePatternCombo = nullptr;
    QPushButton* m_fwdButton = nullptr;
    QPushButton* m_pingButton = nullptr;
    QPushButton* m_revButton = nullptr;
    QButtonGroup* m_wipeDirGroup = nullptr;
    QComboBox* m_wipeEdgeCombo = nullptr;
    QSpinBox* m_wipeEdgeAmountSpin = nullptr;
    QPushButton* m_wipeColorButton = nullptr;
    QPushButton* m_dskButton = nullptr;
    QPushButton* m_dskPreviewButton = nullptr;
    QPushButton* m_connectButton = nullptr;
    QPushButton* m_sourcesButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_serverLabel = nullptr;
    QLabel* m_tcpLabel = nullptr;
    QLabel* m_oscLabel = nullptr;
    QLabel* m_ndiPgmLabel = nullptr;
    QLabel* m_ndiCleanLabel = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_programLabel = nullptr;
    QLabel* m_routePrgLabel = nullptr;
    QLabel* m_routePrvLabel = nullptr;
    QLabel* m_rateReadout = nullptr;
    QSpinBox* m_durationSpin = nullptr;
    QVector<QPushButton*> m_programButtons;
    QVector<QPushButton*> m_previewButtons;
    QVector<bool> m_sourceOn;
    bool m_connected = false;
    bool m_transitioning = false;
    QString m_activeTake;
    int m_previewId = -1;
    int m_programId = -1;
    int m_deckId = -1;
    int m_cuedId = -1;
    bool m_cuePulse = true;
    QTimer* m_cueBlinkTimer = nullptr;
};
