#pragma once
#include <QMainWindow>
#include <QJsonObject>
class SwitcherEngine;
class PanelProtocol;
class Configuration;
class SetupWorkspace;
class SpeedEditorHid;
class QLabel;
class QPushButton;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(SwitcherEngine* engine, PanelProtocol* panel, QWidget* parent = nullptr);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void refreshStatus();
    void captureEditorBaseline();
    void reportNotice(const QString& message);
    void saveConfiguration();
    void applyConfiguration();
    void onSpeedEditorKey(quint16 key);
    void onSpeedEditorJog(int value, quint8 mode);
    SwitcherEngine* m_engine;
    PanelProtocol* m_panel;
    Configuration* m_prepared;
    QJsonObject m_liveBaseline;
    QJsonObject m_editorBaseline;
    SetupWorkspace* m_workspace;
    SpeedEditorHid* m_speedEditor;
    QLabel* m_configurationState;
    QLabel* m_status;
    QLabel* m_notice;
    QPushButton* m_connect;
    QPushButton* m_apply;
};
