#include "MainWindow.h"
#include "SetupWorkspace.h"
#include "config/Configuration.h"
#include "config/ConfigurationMerge.h"
#include "core/SwitcherEngine.h"
#include "core/PanelProtocol.h"
#include "core/SpeedEditorHid.h"
#include "Version.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QCloseEvent>

MainWindow::MainWindow(SwitcherEngine* engine, PanelProtocol* panel, QWidget* parent)
    : QMainWindow(parent), m_engine(engine), m_panel(panel)
{
    m_prepared = new Configuration(this);
    m_liveBaseline = engine->configuration()->toJson();
    m_prepared->fromJson(m_liveBaseline);
    m_prepared->setConfigFilePath(engine->configuration()->configFilePath());
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("managementWorkspace"));
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("KAVTOR  /  ENGINE MANAGER"), this);
    title->setObjectName(QStringLiteral("workspaceTitle"));
    header->addWidget(title);
    header->addStretch();
    header->addWidget(new QLabel(QStringLiteral("v") + QStringLiteral(KAVTOR_VERSION), this));
    m_connect = new QPushButton(this);
    m_connect->setObjectName(QStringLiteral("engineConnection"));
    header->addWidget(m_connect);
    m_apply = new QPushButton(tr("Apply saved configuration"), this);
    m_apply->setObjectName(QStringLiteral("applyPrepared"));
    header->addWidget(m_apply);
    layout->addLayout(header);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("engineSummary"));
    layout->addWidget(m_status);
    m_notice = new QLabel(tr("Prepare configuration here. Live operation belongs to the hardware panel."), this);
    m_notice->setWordWrap(true);
    m_notice->setObjectName(QStringLiteral("workspaceNotice"));
    layout->addWidget(m_notice);
    m_configurationState = new QLabel(this);
    m_configurationState->setObjectName(QStringLiteral("configurationState"));
    layout->addWidget(m_configurationState);
    m_workspace = new SetupWorkspace(m_prepared, this);
    layout->addWidget(m_workspace, 1);
    captureEditorBaseline();
    connect(m_workspace, &SetupWorkspace::saveRequested, this, &MainWindow::saveConfiguration);
    connect(m_workspace, &SetupWorkspace::dirtyChanged, this, [this](bool dirty) {
        m_notice->setText(dirty ? tr("Unsaved changes. Save or discard before applying.") : tr("Configuration editor is clean. Saved settings are applied separately."));
        refreshStatus();
    });
    connect(m_apply, &QPushButton::clicked, this, &MainWindow::applyConfiguration);
    connect(m_connect, &QPushButton::clicked, this, [this] {
        if (m_engine->isConnected()) m_engine->disconnectFromCaspar();
        else m_engine->connectToCaspar();
    });
    connect(engine, &SwitcherEngine::connectionStatusChanged, this, &MainWindow::refreshStatus);
    connect(engine, &SwitcherEngine::renderReadinessChanged, this, &MainWindow::refreshStatus);
    connect(engine, &SwitcherEngine::transitioningChanged, this, &MainWindow::refreshStatus);
    connect(engine, &SwitcherEngine::error, this, &MainWindow::reportNotice);
    connect(engine->configuration(), &Configuration::configurationChanged, this, &MainWindow::refreshStatus);
    m_speedEditor = new SpeedEditorHid(this);
    connect(m_speedEditor, &SpeedEditorHid::keyPressed, this, &MainWindow::onSpeedEditorKey);
    connect(m_speedEditor, &SpeedEditorHid::jogMoved, this, &MainWindow::onSpeedEditorJog);
    connect(engine, &SwitcherEngine::deckSourceChanged, m_speedEditor, &SpeedEditorHid::setArmedSource);
    connect(engine, &SwitcherEngine::deckCueChanged, this, [this](int id) { m_speedEditor->setTransLed(id >= 0); });
    setWindowTitle(tr("kavtor — Engine Manager"));
    setMinimumSize(1000, 650);
    resize(qMax(1180, engine->configuration()->windowWidth()), qMax(800, engine->configuration()->windowHeight()));
    refreshStatus();
}

void MainWindow::captureEditorBaseline()
{
    Configuration normalized;
    normalized.fromJson(m_prepared->toJson());
    if (m_workspace->fillDraft(&normalized)) m_editorBaseline = normalized.toJson();
    else m_editorBaseline = m_prepared->toJson();
}

void MainWindow::refreshStatus()
{
    Configuration* config = m_engine->configuration();
    const bool different = config->toJson() != m_prepared->toJson();
    m_configurationState->setText(different ? tr("Saved preparation differs from running settings.") : tr("Saved preparation matches running settings."));
    int ready = 0;
    for (int id = 0; id < config->maxSources(); ++id) {
        const Source* live = config->sourceById(id);
        const Source* saved = m_prepared->sourceById(id);
        QString state;
        if (!live) state = tr("Unavailable");
        else if (saved && (saved->name != live->name || saved->enabled != live->enabled || saved->type != live->type
            || saved->casparChannel != live->casparChannel || saved->argument != live->argument || saved->loop != live->loop)) state = tr("Pending apply");
        else if (!live->enabled) state = tr("Disabled");
        else if (!live->isAssigned()) state = tr("Unassigned");
        else if (!m_engine->isConnected()) state = tr("Offline");
        else state = m_engine->sourceReady(id) ? tr("Prepared") : tr("Not prepared");
        m_workspace->setInputState(id, state);
    }
    for (const Source& source : config->sources()) if (m_engine->sourceReady(source.id)) ++ready;
    m_status->setText(tr("%1  ·  %2:%3  ·  Inputs prepared %4/%5  ·  Outputs %6  ·  Panel TCP %7  ·  4 M/Es  ·  1080p50")
        .arg(m_engine->isConnected() ? tr("Connected") : tr("Disconnected"))
        .arg(config->casparHost()).arg(config->casparPort()).arg(ready).arg(config->maxSources())
        .arg(m_engine->outputsReady() ? tr("prepared") : tr("not prepared"))
        .arg(m_panel && m_panel->isListening() ? QString::number(m_panel->port()) : tr("offline")));
    m_connect->setText(m_engine->isConnected() ? tr("Disconnect engine") : tr("Connect engine"));
    bool needsPreparation = m_engine->isConnected() && !m_engine->outputsReady();
    if (m_engine->isConnected()) {
        for (const Source& source : config->sources())
            if (source.enabled && source.isAssigned() && !m_engine->sourceReady(source.id)) needsPreparation = true;
    }
    m_apply->setText(different ? tr("Apply saved configuration")
        : needsPreparation ? tr("Retry preparation") : tr("No pending changes"));
    if (m_engine->isPreparing()) m_apply->setText(tr("Preparing engine…"));
    m_apply->setEnabled(!m_workspace->isDirty() && !m_engine->isBusy()
        && !m_engine->isPreparing() && (different || needsPreparation));
}

void MainWindow::reportNotice(const QString& message)
{
    m_notice->setText(message);
    m_workspace->appendDiagnostic(message);
}

void MainWindow::saveConfiguration()
{
    const QString error = m_workspace->validationError();
    if (!error.isEmpty()) { reportNotice(error); return; }
    Configuration candidate;
    candidate.fromJson(m_prepared->toJson());
    candidate.setConfigFilePath(m_prepared->configFilePath());
    QStringList conflicts;
    if (!m_workspace->fillDraft(&candidate)) return;
    // Widget serialization normalizes legacy/default representations. Only
    // actual user edits should override the prepared model or live panel state.
    const auto edited = mergeConfiguration(m_editorBaseline, candidate.toJson(),
        m_prepared->toJson(), conflicts);
    const auto merged = mergeConfiguration(m_liveBaseline, edited,
        m_engine->configuration()->toJson(), conflicts).toObject();
    if (!conflicts.isEmpty()) {
        reportNotice(tr("Settings changed on both the panel and editor: %1. Save was cancelled.").arg(conflicts.join(", ")));
        return;
    }
    candidate.fromJson(merged);
    if (!candidate.save()) {
        reportNotice(tr("Configuration was not saved. Check stinger timings, media names and file permissions."));
        return;
    }
    m_prepared->fromJson(candidate.toJson());
    m_liveBaseline = m_engine->configuration()->toJson();
    m_workspace->reload();
    captureEditorBaseline();
    reportNotice(tr("Saved to disk. Running engine settings are unchanged until Apply."));
}

void MainWindow::applyConfiguration()
{
    if (m_workspace->isDirty() || m_engine->isBusy() || m_engine->isPreparing()) return;
    if (m_engine->isConnected() && QMessageBox::question(this, tr("Apply engine configuration"),
        tr("Applying can reload changed inputs, multiview and DSK routes, affecting the output. Apply now?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    // The confirmation dialog runs an event loop: hardware may have started a
    // take while it was open. Recheck before replacing any running settings.
    if (m_engine->isBusy() || m_engine->isPreparing()) { reportNotice(tr("An engine operation is in progress. Apply after it completes.")); return; }
    QStringList conflicts;
    const auto merged = mergeConfiguration(m_liveBaseline, m_prepared->toJson(),
        m_engine->configuration()->toJson(), conflicts).toObject();
    if (!conflicts.isEmpty()) {
        reportNotice(tr("Settings changed on both the panel and saved preparation: %1. Apply was cancelled.").arg(conflicts.join(", ")));
        return;
    }
    const auto panelPort = merged.value("panel").toObject().value("port").toInt();
    if (m_panel && !m_panel->start(static_cast<quint16>(panelPort))) {
        reportNotice(tr("Panel port %1 is unavailable. Running settings and the existing listener were preserved.").arg(panelPort));
        return;
    }
    m_engine->configuration()->fromJson(merged);
    m_prepared->fromJson(merged);
    m_liveBaseline = merged;
    m_workspace->reload();
    captureEditorBaseline();
    m_engine->applyPreparedConfiguration();
    reportNotice(tr("Configuration submitted. Check preparation status; endpoint changes take effect on the next connection."));
    refreshStatus();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_workspace->isDirty() && QMessageBox::question(this, tr("Unsaved configuration"),
        tr("Discard unsaved edits and close?"), QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel) != QMessageBox::Discard) {
        event->ignore();
        return;
    }
    // Do not overwrite a saved preparation with the older running settings.
    if (m_engine->isConnected()) m_engine->disconnectFromCaspar();
    event->accept();
}

void MainWindow::onSpeedEditorKey(quint16 key)
{
    const int cam = SpeedEditorHid::sourceIdForCamKey(key);
    if (cam >= 0) {
        m_engine->selectDeckSource(cam);
        return;
    }
    switch (key) {
    case SpeedEditorHid::StopPlay:
        m_engine->toggleDeckPause();
        break;
    case SpeedEditorHid::Trans:
        m_engine->toggleDeckCue();
        break;
    case SpeedEditorHid::Jog:
        m_speedEditor->setJogMode(SpeedEditorHid::Relative);
        m_engine->setDeckJogMode(SwitcherEngine::DeckJogMode::Jog);
        break;
    case SpeedEditorHid::Shtl:
        m_speedEditor->setJogMode(SpeedEditorHid::Shuttle);
        m_engine->setDeckJogMode(SwitcherEngine::DeckJogMode::Shuttle);
        break;
    case SpeedEditorHid::Scrl:
        m_speedEditor->setJogMode(SpeedEditorHid::Scroll);
        m_engine->setDeckJogMode(SwitcherEngine::DeckJogMode::Scroll);
        break;
    default:
        break;
    }
}

void MainWindow::onSpeedEditorJog(int value, quint8 mode)
{
    if (mode == SpeedEditorHid::Shuttle || mode == 3) {
        m_engine->setDeckJogMode(SwitcherEngine::DeckJogMode::Shuttle);
    } else if (mode == SpeedEditorHid::Scroll) {
        m_engine->setDeckJogMode(SwitcherEngine::DeckJogMode::Scroll);
    } else {
        m_engine->setDeckJogMode(SwitcherEngine::DeckJogMode::Jog);
    }
    m_engine->jogDeck(value);
}
