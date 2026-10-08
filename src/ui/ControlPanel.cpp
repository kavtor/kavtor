#include "ControlPanel.h"

#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QColorDialog>

#include "core/Transition.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QStyle>
#include <QtGui/QIcon>
#include <QtGui/QColor>
#include <QtCore/QFileInfo>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>

namespace {

QPushButton* makeBusButton(QWidget* parent, int id, const QString& objectName)
{
    auto* button = new QPushButton(parent);
    button->setObjectName(objectName);
    button->setMinimumHeight(76);
    button->setProperty("sourceId", id);
    return button;
}

QString sourceKindLabel(const Source& source)
{
    switch (source.type) {
    case SourceType::Decklink:
        return QStringLiteral("SDI %1").arg(source.argument.isEmpty()
            ? QString::number(source.id + 1) : source.argument);
    case SourceType::Ndi:
        return source.argument.isEmpty() ? QStringLiteral("NDI") : source.argument;
    case SourceType::File:
        return source.argument.isEmpty()
            ? QStringLiteral("NO SOURCE")
            : QFileInfo(source.argument).fileName();
    case SourceType::Html:
        return QStringLiteral("HTTP");
    case SourceType::Ffmpeg:
        return QStringLiteral("STREAM");
    case SourceType::Still:
        return source.argument.isEmpty() ? QStringLiteral("NO SOURCE") : QStringLiteral("STILL");
    case SourceType::ColorBars:
        return QStringLiteral("BARS");
    }
    return {};
}

QLabel* makeSectionLabel(QWidget* parent, const QString& text, const QString& objectName)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    return label;
}

} // namespace

ControlPanel::ControlPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 8, 10, 8);
    mainLayout->setSpacing(8);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("rackHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 6, 10, 6);
    auto* title = new QLabel(tr("KAVTOR  M/E"), header);
    title->setObjectName(QStringLiteral("rackTitle"));
    m_statusLabel = new QLabel(tr("DISCONNECTED"), header);
    m_statusLabel->setObjectName(QStringLiteral("connBadge"));
    m_serverLabel = new QLabel(QStringLiteral("127.0.0.1:5250"), header);
    m_serverLabel->setObjectName(QStringLiteral("serverLabel"));
    m_tcpLabel = new QLabel(QStringLiteral("TCP 9100"), header);
    m_tcpLabel->setObjectName(QStringLiteral("chipLabel"));
    m_oscLabel = new QLabel(QStringLiteral("OSC 6250"), header);
    m_oscLabel->setObjectName(QStringLiteral("chipLabel"));
    m_ndiPgmLabel = new QLabel(header);
    m_ndiPgmLabel->setObjectName(QStringLiteral("chipLabel"));
    m_ndiCleanLabel = new QLabel(header);
    m_ndiCleanLabel->setObjectName(QStringLiteral("chipLabel"));
    m_connectButton = new QPushButton(tr("CONNECT"), header);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    m_sourcesButton = new QPushButton(tr("I/O & SOURCES"), header);
    m_sourcesButton->setObjectName(QStringLiteral("sourcesButton"));
    headerLayout->addWidget(title);
    headerLayout->addSpacing(12);
    headerLayout->addWidget(m_statusLabel);
    headerLayout->addWidget(m_serverLabel);
    headerLayout->addWidget(m_tcpLabel);
    headerLayout->addWidget(m_oscLabel);
    headerLayout->addWidget(m_ndiPgmLabel);
    headerLayout->addWidget(m_ndiCleanLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_sourcesButton);
    headerLayout->addWidget(m_connectButton);
    mainLayout->addWidget(header);

    auto* deck = new QHBoxLayout;
    deck->setSpacing(8);

    auto* mePanel = new QFrame(this);
    mePanel->setObjectName(QStringLiteral("mePanel"));
    auto* meLayout = new QVBoxLayout(mePanel);
    meLayout->setSpacing(8);

    auto* meHeader = new QHBoxLayout;
    meHeader->addWidget(makeSectionLabel(mePanel, tr("M/E 1  CROSS-POINT"), QStringLiteral("sectionTitle")));
    meHeader->addStretch(1);
    auto* tallyHint = new QLabel(tr("RED PRG   GREEN PVW"), mePanel);
    tallyHint->setObjectName(QStringLiteral("mutedLabel"));
    meHeader->addWidget(tallyHint);
    meLayout->addLayout(meHeader);

    auto* prgBox = new QFrame(mePanel);
    prgBox->setObjectName(QStringLiteral("busBox"));
    auto* prgBoxLayout = new QVBoxLayout(prgBox);
    prgBoxLayout->setContentsMargins(8, 6, 8, 8);
    auto* prgHead = new QHBoxLayout;
    auto* prgTitle = new QLabel(tr("PROGRAM BUS  //  ON-AIR"), prgBox);
    prgTitle->setObjectName(QStringLiteral("prgBusTitle"));
    prgHead->addWidget(prgTitle);
    prgHead->addStretch(1);
    prgHead->addWidget(makeSectionLabel(prgBox, tr("SHIFT+1…8"), QStringLiteral("mutedLabel")));
    prgBoxLayout->addLayout(prgHead);

    m_sourceOn.fill(false, 8);
    auto* programGrid = new QGridLayout;
    programGrid->setSpacing(6);
    for (int i = 0; i < 8; ++i) {
        QPushButton* button = makeBusButton(prgBox, i, QStringLiteral("programButton"));
        connect(button, &QPushButton::clicked, this, &ControlPanel::onProgramClicked);
        m_programButtons.append(button);
        programGrid->addWidget(button, 0, i);
    }
    prgBoxLayout->addLayout(programGrid);
    meLayout->addWidget(prgBox);

    auto* prvBox = new QFrame(mePanel);
    prvBox->setObjectName(QStringLiteral("busBox"));
    auto* prvBoxLayout = new QVBoxLayout(prvBox);
    prvBoxLayout->setContentsMargins(8, 6, 8, 8);
    auto* prvHead = new QHBoxLayout;
    auto* prvTitle = new QLabel(tr("PREVIEW BUS  //  ARMED NEXT"), prvBox);
    prvTitle->setObjectName(QStringLiteral("prvBusTitle"));
    prvHead->addWidget(prvTitle);
    prvHead->addStretch(1);
    prvHead->addWidget(makeSectionLabel(prvBox, tr("1…8"), QStringLiteral("mutedLabel")));
    prvBoxLayout->addLayout(prvHead);

    auto* previewGrid = new QGridLayout;
    previewGrid->setSpacing(6);
    for (int i = 0; i < 8; ++i) {
        QPushButton* button = makeBusButton(prvBox, i, QStringLiteral("sourceButton"));
        connect(button, &QPushButton::clicked, this, &ControlPanel::onPreviewClicked);
        m_previewButtons.append(button);
        previewGrid->addWidget(button, 0, i);
    }
    prvBoxLayout->addLayout(previewGrid);
    meLayout->addWidget(prvBox);

    auto* routeRow = new QHBoxLayout;
    m_programLabel = new QLabel(mePanel);
    m_programLabel->setObjectName(QStringLiteral("programBus"));
    m_previewLabel = new QLabel(mePanel);
    m_previewLabel->setObjectName(QStringLiteral("previewBus"));
    m_routePrgLabel = m_programLabel;
    m_routePrvLabel = m_previewLabel;
    routeRow->addWidget(m_programLabel, 1);
    routeRow->addWidget(m_previewLabel, 1);
    meLayout->addLayout(routeRow);
    deck->addWidget(mePanel, 3);

    auto* transPanel = new QFrame(this);
    transPanel->setObjectName(QStringLiteral("transPanel"));
    auto* transLayout = new QVBoxLayout(transPanel);
    auto* transHead = new QHBoxLayout;
    transHead->addWidget(makeSectionLabel(transPanel, tr("TRANSITION"), QStringLiteral("sectionTitle")));
    m_rateReadout = new QLabel(tr("25 frames"), transPanel);
    m_rateReadout->setObjectName(QStringLiteral("rateReadout"));
    transHead->addStretch(1);
    transHead->addWidget(m_rateReadout);
    transLayout->addLayout(transHead);

    auto* rateRow = new QHBoxLayout;
    m_durationSpin = new QSpinBox(transPanel);
    m_durationSpin->setObjectName(QStringLiteral("durationSpin"));
    m_durationSpin->setRange(1, 1000);
    m_durationSpin->setValue(25);
    m_durationSpin->setSuffix(tr(" f"));
    rateRow->addWidget(m_durationSpin, 1);
    const int presets[] = {12, 25, 50, 75};
    for (int frames : presets) {
        auto* preset = new QPushButton(tr("%1 f").arg(frames), transPanel);
        preset->setObjectName(QStringLiteral("rateButton"));
        preset->setProperty("frames", frames);
        connect(preset, &QPushButton::clicked, this, [this, frames]() {
            m_durationSpin->setValue(frames);
        });
        rateRow->addWidget(preset);
    }
    transLayout->addLayout(rateRow);

    m_cutButton = new QPushButton(tr("CUT"), transPanel);
    m_cutButton->setObjectName(QStringLiteral("cutButton"));
    m_cutButton->setMinimumHeight(52);
    m_autoButton = new QPushButton(tr("AUTO"), transPanel);
    m_autoButton->setObjectName(QStringLiteral("autoButton"));
    m_autoButton->setMinimumHeight(52);
    m_autoButton->setCheckable(true);
    m_wipeButton = new QPushButton(tr("WIPE"), transPanel);
    m_wipeButton->setObjectName(QStringLiteral("wipeButton"));
    m_wipeButton->setCheckable(true);
    m_wipeButton->setMinimumHeight(44);
    auto* takeGrid = new QGridLayout;
    takeGrid->addWidget(m_cutButton, 0, 0);
    takeGrid->addWidget(m_autoButton, 0, 1);
    takeGrid->addWidget(m_wipeButton, 1, 0, 1, 2);
    transLayout->addLayout(takeGrid);

    m_dskButton = new QPushButton(tr("DSK 1  CUT"), transPanel);
    m_dskButton->setObjectName(QStringLiteral("dskButton"));
    m_dskButton->setCheckable(true);
    m_dskButton->setMinimumHeight(40);
    m_dskPreviewButton = new QPushButton(tr("DSK 1  PVW"), transPanel);
    m_dskPreviewButton->setObjectName(QStringLiteral("dskPreviewButton"));
    m_dskPreviewButton->setCheckable(true);
    m_dskPreviewButton->setMinimumHeight(40);
    auto* dskRow = new QHBoxLayout;
    dskRow->addWidget(m_dskPreviewButton, 1);
    dskRow->addWidget(m_dskButton, 1);
    transLayout->addLayout(dskRow);
    transLayout->addStretch(1);
    deck->addWidget(transPanel, 1);
    mainLayout->addLayout(deck, 1);

    auto* fxPanel = new QFrame(this);
    fxPanel->setObjectName(QStringLiteral("fxPanel"));
    auto* fxLayout = new QHBoxLayout(fxPanel);
    m_wipePatternCombo = new QComboBox(fxPanel);
    m_wipePatternCombo->setObjectName(QStringLiteral("wipePatternCombo"));
    m_wipePatternCombo->setMinimumWidth(280);
    m_wipePatternCombo->setMaxVisibleItems(24);
    for (const WipePattern& pattern : builtinWipePatterns()) {
        m_wipePatternCombo->addItem(pattern.label, pattern.id);
    }
    m_fwdButton = new QPushButton(tr("FWD"), fxPanel);
    m_fwdButton->setObjectName(QStringLiteral("wipeFwdButton"));
    m_fwdButton->setCheckable(true);
    m_fwdButton->setChecked(true);
    m_fwdButton->setMinimumHeight(36);
    m_pingButton = new QPushButton(tr("P-P"), fxPanel);
    m_pingButton->setObjectName(QStringLiteral("wipePingButton"));
    m_pingButton->setCheckable(true);
    m_pingButton->setMinimumHeight(36);
    m_revButton = new QPushButton(tr("REV"), fxPanel);
    m_revButton->setObjectName(QStringLiteral("wipeRevButton"));
    m_revButton->setCheckable(true);
    m_revButton->setMinimumHeight(36);
    m_wipeDirGroup = new QButtonGroup(this);
    m_wipeDirGroup->setExclusive(true);
    m_wipeDirGroup->addButton(m_fwdButton, static_cast<int>(WipeDirectionMode::Forward));
    m_wipeDirGroup->addButton(m_pingButton, static_cast<int>(WipeDirectionMode::PingPong));
    m_wipeDirGroup->addButton(m_revButton, static_cast<int>(WipeDirectionMode::Reverse));
    m_wipeEdgeCombo = new QComboBox(fxPanel);
    m_wipeEdgeCombo->setObjectName(QStringLiteral("wipeEdgeCombo"));
    m_wipeEdgeCombo->addItem(tr("Hard"), static_cast<int>(WipeEdgeMode::Hard));
    m_wipeEdgeCombo->addItem(tr("Soft"), static_cast<int>(WipeEdgeMode::Soft));
    m_wipeEdgeCombo->addItem(tr("Color border"), static_cast<int>(WipeEdgeMode::Border));
    m_wipeEdgeAmountSpin = new QSpinBox(fxPanel);
    m_wipeEdgeAmountSpin->setObjectName(QStringLiteral("wipeEdgeAmountSpin"));
    m_wipeEdgeAmountSpin->setRange(0, 40);
    m_wipeEdgeAmountSpin->setValue(8);
    m_wipeEdgeAmountSpin->setSuffix(tr(" px"));
    m_wipeColorButton = new QPushButton(tr("TINT"), fxPanel);
    m_wipeColorButton->setObjectName(QStringLiteral("wipeColorButton"));
    m_wipeColorButton->setMinimumWidth(64);
    m_wipeColorButton->setProperty("borderColor", QStringLiteral("#ffffff"));
    fxLayout->addWidget(makeSectionLabel(fxPanel, tr("SMPTE WIPE"), QStringLiteral("sectionTitle")));
    fxLayout->addWidget(m_wipePatternCombo, 2);
    fxLayout->addWidget(m_fwdButton);
    fxLayout->addWidget(m_pingButton);
    fxLayout->addWidget(m_revButton);
    fxLayout->addWidget(m_wipeEdgeCombo, 1);
    fxLayout->addWidget(m_wipeEdgeAmountSpin);
    fxLayout->addWidget(m_wipeColorButton);
    mainLayout->addWidget(fxPanel);

    auto* footer = new QLabel(
        tr("SPACE CUT    SHIFT+SPACE AUTO    CTRL+SPACE WIPE    1–8 PVW    SHIFT+1–8 PRG"),
        this);
    footer->setObjectName(QStringLiteral("footerLabel"));
    footer->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(footer);

    connect(m_durationSpin, &QSpinBox::valueChanged, this, [this](int frames) {
        m_rateReadout->setText(tr("%1 frames").arg(frames));
        emit durationChanged(frames);
    });

    connect(m_cutButton, &QPushButton::clicked, this, &ControlPanel::onCutClicked);
    connect(m_autoButton, &QPushButton::clicked, this, &ControlPanel::onAutoClicked);
    connect(m_wipeButton, &QPushButton::clicked, this, &ControlPanel::onWipeClicked);
    connect(m_wipePatternCombo, &QComboBox::currentIndexChanged, this, &ControlPanel::onWipePatternIndexChanged);
    connect(m_wipeDirGroup, &QButtonGroup::idClicked, this, &ControlPanel::onWipeDirectionClicked);
    connect(m_wipeEdgeCombo, &QComboBox::currentIndexChanged, this, &ControlPanel::onWipeEdgeChanged);
    connect(m_wipeEdgeAmountSpin, &QSpinBox::valueChanged, this, &ControlPanel::onWipeEdgeChanged);
    connect(m_wipeColorButton, &QPushButton::clicked, this, &ControlPanel::onWipeColorClicked);
    connect(m_connectButton, &QPushButton::clicked, this, &ControlPanel::onConnectClicked);
    connect(m_sourcesButton, &QPushButton::clicked, this, &ControlPanel::sourcesEditRequested);
    connect(m_dskButton, &QPushButton::toggled, this, &ControlPanel::dskToggled);
    connect(m_dskPreviewButton, &QPushButton::toggled, this, &ControlPanel::dskPreviewToggled);

    setObjectName(QStringLiteral("controlPanel"));
    m_cueBlinkTimer = new QTimer(this);
    m_cueBlinkTimer->setInterval(500);
    connect(m_cueBlinkTimer, &QTimer::timeout, this, [this]() {
        m_cuePulse = !m_cuePulse;
        updateTally();
    });
    setPreviewSource(-1);
    setProgramSource(-1);
    updateActionEnabled();
}

void ControlPanel::setSources(const QList<Source>& sources)
{
    m_sourceOn.fill(false, 8);
    for (int i = 0; i < 8; ++i) {
        const Source* src = nullptr;
        for (const Source& candidate : sources) {
            if (candidate.id == i) {
                src = &candidate;
                break;
            }
        }
        const QString name = src && !src->name.isEmpty() ? src->name : tr("SRC %1").arg(i + 1);
        const QString kind = src ? sourceKindLabel(*src) : QStringLiteral("NO SOURCE");
        m_sourceOn[i] = src && src->isAssigned();
        const QString caption = QStringLiteral("SRC %1\n%2\n%3").arg(i + 1).arg(name, kind);
        m_programButtons.at(i)->setText(caption);
        m_previewButtons.at(i)->setText(caption);
        m_programButtons.at(i)->setToolTip(tr("Hot punch %1 to program").arg(name));
        m_previewButtons.at(i)->setToolTip(tr("Send %1 to preview").arg(name));
    }
    updateTally();
    updateActionEnabled();
}

void ControlPanel::setConnectionStatus(bool connected)
{
    m_connected = connected;
    m_statusLabel->setText(connected ? tr("CONNECTED") : tr("DISCONNECTED"));
    m_statusLabel->setProperty("online", connected);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_connectButton->setText(connected ? tr("DISCONNECT") : tr("CONNECT"));
    updateActionEnabled();
}

void ControlPanel::setServerInfo(const QString& host, int amcpPort, int panelPort, int oscPort)
{
    m_serverLabel->setText(QStringLiteral("%1:%2").arg(host).arg(amcpPort));
    m_tcpLabel->setText(tr("TCP %1").arg(panelPort));
    m_oscLabel->setText(tr("OSC %1").arg(oscPort));
}

void ControlPanel::setOutputFeeds(bool ndiProgramOn, const QString& ndiProgramName,
                                 bool ndiCleanOn, const QString& ndiCleanName)
{
    m_ndiPgmLabel->setVisible(ndiProgramOn);
    m_ndiPgmLabel->setText(tr("NDI %1").arg(ndiProgramName));
    m_ndiCleanLabel->setVisible(ndiCleanOn);
    m_ndiCleanLabel->setText(tr("CLEAN %1").arg(ndiCleanName));
}

void ControlPanel::setDurationFrames(int frames)
{
    const QSignalBlocker blocker(m_durationSpin);
    m_durationSpin->setValue(frames);
    m_rateReadout->setText(tr("%1 frames").arg(m_durationSpin->value()));
}

void ControlPanel::setPreviewSource(int sourceId, const QString& name)
{
    m_previewId = sourceId;
    m_previewLabel->setText(busText(tr("PREVIEW"), sourceId, name));
    updateTally();
}

void ControlPanel::setProgramSource(int sourceId, const QString& name)
{
    m_programId = sourceId;
    m_programLabel->setText(busText(tr("PROGRAM"), sourceId, name));
    updateTally();
}

void ControlPanel::setDeckSource(int sourceId)
{
    m_deckId = sourceId;
    updateTally();
}

void ControlPanel::setCuedSource(int sourceId)
{
    if (m_cuedId == sourceId) {
        return;
    }
    m_cuedId = sourceId;
    m_cuePulse = true;
    if (m_cuedId >= 0) {
        m_cueBlinkTimer->start();
    } else {
        m_cueBlinkTimer->stop();
    }
    updateTally();
}

void ControlPanel::setTransitioning(bool transitioning)
{
    m_transitioning = transitioning;
    updateActionEnabled();
}

void ControlPanel::setActiveTake(const QString& take)
{
    m_activeTake = take;
    const QSignalBlocker mixBlocker(m_autoButton);
    const QSignalBlocker wipeBlocker(m_wipeButton);
    m_autoButton->setChecked(take == QLatin1String("mix"));
    m_wipeButton->setChecked(take == QLatin1String("wipe"));
    updateActionEnabled();
}

void ControlPanel::setWipePattern(const QString& patternId)
{
    const int index = m_wipePatternCombo->findData(wipePatternById(patternId).id);
    const QSignalBlocker blocker(m_wipePatternCombo);
    m_wipePatternCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void ControlPanel::setWipeDirectionMode(WipeDirectionMode mode)
{
    QAbstractButton* button = m_wipeDirGroup->button(static_cast<int>(mode));
    if (!button) {
        return;
    }
    const QSignalBlocker blocker(m_wipeDirGroup);
    button->setChecked(true);
}

void ControlPanel::setWipeSenseForward(bool forward)
{
    m_pingButton->setText(forward ? tr("PING → FWD") : tr("PING → REV"));
}

void ControlPanel::setWipeEdge(WipeEdgeMode mode, int amount, const QString& color)
{
    const QSignalBlocker edgeBlocker(m_wipeEdgeCombo);
    const QSignalBlocker amountBlocker(m_wipeEdgeAmountSpin);
    const int index = m_wipeEdgeCombo->findData(static_cast<int>(mode));
    m_wipeEdgeCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_wipeEdgeAmountSpin->setValue(amount);
    m_wipeColorButton->setProperty("borderColor", color);
    m_wipeColorButton->setStyleSheet(QStringLiteral("background:%1; color:%2;")
        .arg(color, QColor(color).lightness() > 140 ? QStringLiteral("#111") : QStringLiteral("#fff")));
    const bool hasEdge = mode != WipeEdgeMode::Hard;
    m_wipeEdgeAmountSpin->setEnabled(hasEdge);
    m_wipeColorButton->setEnabled(mode == WipeEdgeMode::Border);
}

void ControlPanel::setDskOn(bool on)
{
    const QSignalBlocker blocker(m_dskButton);
    m_dskButton->setChecked(on);
}

void ControlPanel::setDskPreview(bool on)
{
    const QSignalBlocker blocker(m_dskPreviewButton);
    m_dskPreviewButton->setChecked(on);
}

void ControlPanel::onCutClicked()
{
    emit cutRequested();
}

void ControlPanel::onAutoClicked()
{
    if (m_transitioning) {
        const QSignalBlocker blocker(m_autoButton);
        m_autoButton->setChecked(m_activeTake == QLatin1String("mix"));
        return;
    }
    emit autoRequested();
    if (!m_transitioning) {
        const QSignalBlocker blocker(m_autoButton);
        m_autoButton->setChecked(false);
    }
}

void ControlPanel::onWipeClicked()
{
    if (m_transitioning) {
        const QSignalBlocker blocker(m_wipeButton);
        m_wipeButton->setChecked(m_activeTake == QLatin1String("wipe"));
        return;
    }
    emit wipeRequested();
    if (!m_transitioning) {
        const QSignalBlocker blocker(m_wipeButton);
        m_wipeButton->setChecked(false);
    }
}

void ControlPanel::onWipePatternIndexChanged()
{
    emit wipePatternChanged(m_wipePatternCombo->currentData().toString());
}

void ControlPanel::onWipeDirectionClicked()
{
    emit wipeDirectionChanged(static_cast<WipeDirectionMode>(m_wipeDirGroup->checkedId()));
}

void ControlPanel::onWipeEdgeChanged()
{
    const auto mode = static_cast<WipeEdgeMode>(m_wipeEdgeCombo->currentData().toInt());
    const bool hasEdge = mode != WipeEdgeMode::Hard;
    m_wipeEdgeAmountSpin->setEnabled(hasEdge);
    m_wipeColorButton->setEnabled(mode == WipeEdgeMode::Border);
    emit wipeEdgeChanged(mode,
                         m_wipeEdgeAmountSpin->value(),
                         m_wipeColorButton->property("borderColor").toString());
}

void ControlPanel::onWipeColorClicked()
{
    const QColor current(m_wipeColorButton->property("borderColor").toString());
    const QColor chosen = QColorDialog::getColor(current.isValid() ? current : Qt::white, this, tr("Wipe border color"));
    if (!chosen.isValid()) {
        return;
    }
    m_wipeColorButton->setProperty("borderColor", chosen.name());
    setWipeEdge(static_cast<WipeEdgeMode>(m_wipeEdgeCombo->currentData().toInt()),
                m_wipeEdgeAmountSpin->value(),
                chosen.name());
    emit wipeEdgeChanged(static_cast<WipeEdgeMode>(m_wipeEdgeCombo->currentData().toInt()),
                         m_wipeEdgeAmountSpin->value(),
                         chosen.name());
}

void ControlPanel::onConnectClicked()
{
    if (m_connected) {
        emit disconnectRequested();
    } else {
        emit connectRequested();
    }
}

void ControlPanel::onPreviewClicked()
{
    auto* button = qobject_cast<QPushButton*>(sender());
    if (button) {
        emit previewRequested(button->property("sourceId").toInt());
    }
}

void ControlPanel::onProgramClicked()
{
    auto* button = qobject_cast<QPushButton*>(sender());
    if (button) {
        emit programRequested(button->property("sourceId").toInt());
    }
}

void ControlPanel::updateTally()
{
    for (int i = 0; i < 8; ++i) {
        applyTally(m_programButtons.at(i), i == m_programId ? QStringLiteral("program") : QStringLiteral("off"));
        applyTally(m_previewButtons.at(i), i == m_previewId ? QStringLiteral("preview") : QStringLiteral("off"));
        const bool deck = (i == m_deckId);
        const bool cued = (i == m_cuedId);
        m_programButtons.at(i)->setProperty("deck", deck);
        m_previewButtons.at(i)->setProperty("deck", deck);
        m_programButtons.at(i)->setProperty("cued", cued);
        m_previewButtons.at(i)->setProperty("cued", cued);
        m_programButtons.at(i)->setProperty("cuedPulse", cued && m_cuePulse);
        m_previewButtons.at(i)->setProperty("cuedPulse", cued && m_cuePulse);
        m_programButtons.at(i)->style()->unpolish(m_programButtons.at(i));
        m_programButtons.at(i)->style()->polish(m_programButtons.at(i));
        m_previewButtons.at(i)->style()->unpolish(m_previewButtons.at(i));
        m_previewButtons.at(i)->style()->polish(m_previewButtons.at(i));
    }
}

void ControlPanel::updateActionEnabled()
{
    const bool live = m_connected && !m_transitioning;
    m_cutButton->setEnabled(live);
    m_autoButton->setEnabled(m_connected && (live || m_activeTake == QLatin1String("mix")));
    m_wipeButton->setEnabled(m_connected && (live || m_activeTake == QLatin1String("wipe")));
    m_wipePatternCombo->setEnabled(m_connected);
    m_fwdButton->setEnabled(m_connected);
    m_pingButton->setEnabled(m_connected);
    m_revButton->setEnabled(m_connected);
    m_wipeEdgeCombo->setEnabled(m_connected);
    m_durationSpin->setEnabled(m_connected);
    m_dskButton->setEnabled(m_connected);
    m_dskPreviewButton->setEnabled(m_connected);
    for (int i = 0; i < 8; ++i) {
        const bool on = m_sourceOn.value(i, false);
        m_programButtons.at(i)->setEnabled(live && on);
        m_previewButtons.at(i)->setEnabled(m_connected && on);
    }
}

void ControlPanel::applyTally(QPushButton* button, const QString& tally)
{
    button->setProperty("tally", tally);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

QString ControlPanel::busText(const QString& bus, int sourceId, const QString& name) const
{
    if (sourceId < 0) {
        return tr("%1\n—").arg(bus);
    }
    const QString label = name.isEmpty() ? tr("SRC%1").arg(sourceId + 1) : name;
    return tr("%1: %2").arg(bus, label);
}
