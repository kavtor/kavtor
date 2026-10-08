#include "SuperSourceEditor.h"
#include "SetupWorkspace.h"
#include "config/Configuration.h"
#include "core/Transition.h"

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QMessageBox>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QComboBox>
#include <QStandardItemModel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QColorDialog>
#include <QtGui/QColor>
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QPlainTextEdit>
#include <QDateTime>
#include <QScopedValueRollback>
#include <QMenu>

namespace {

const QStringList kTypeLabels = {
    QStringLiteral("SDI"),
    QStringLiteral("NDI"),
    QStringLiteral("Clip"),
    QStringLiteral("HTTP"),
    QStringLiteral("Stream"),
    QStringLiteral("Still"),
    QStringLiteral("Bars"), QStringLiteral("M/E Program"), QStringLiteral("Matte"), QStringLiteral("V4L2"), QStringLiteral("SuperSource")
};

const QStringList kDmeBackgroundEffects = {
    QStringLiteral("move"), QStringLiteral("cube"), QStringLiteral("zoom"),
    QStringLiteral("page_curl"), QStringLiteral("page_roll"), QStringLiteral("global")
};

int typeIndex(SourceType type)
{
    switch (type) {
    case SourceType::Decklink:
        return 0;
    case SourceType::Ndi:
        return 1;
    case SourceType::File:
        return 2;
    case SourceType::Html:
        return 3;
    case SourceType::Ffmpeg:
        return 4;
    case SourceType::Still:
        return 5;
    case SourceType::SuperSource: return 10;
    case SourceType::V4l2: return 9;
    case SourceType::Matte: return 8;
    case SourceType::MeProgram: return 7;
    case SourceType::ColorBars:
        return 6;
    }
    return 2;
}

SourceType typeFromIndex(int index)
{
    switch (index) {
    case 0:
        return SourceType::Decklink;
    case 1:
        return SourceType::Ndi;
    case 3:
        return SourceType::Html;
    case 4:
        return SourceType::Ffmpeg;
    case 5:
        return SourceType::Still;
    case 10: return SourceType::SuperSource;
    case 9: return SourceType::V4l2;
    case 8: return SourceType::Matte;
    case 7: return SourceType::MeProgram;
    case 6:
        return SourceType::ColorBars;
    default:
        return SourceType::File;
    }
}

QString sourceArgument(QLineEdit* edit,SourceType type){return type==SourceType::SuperSource?edit->property("layoutId").toString():edit->text().trimmed();}

QString argumentPlaceholder(SourceType type)
{
    switch (type) {
    case SourceType::Decklink:
        return QStringLiteral("DeckLink device (1, 2, …)");
    case SourceType::Ndi:
        return QStringLiteral("HOSTNAME (source)  or  ndi://host/source");
    case SourceType::File:
        return QStringLiteral("Clip name in media/ or file path");
    case SourceType::Html:
        return QStringLiteral("https://… or file://…");
    case SourceType::Ffmpeg:
        return QStringLiteral("rtsp://  udp://  srt://  http://…");
    case SourceType::Still:
        return QStringLiteral("PNG/WebP with alpha, or still in media/");
    case SourceType::SuperSource: return QStringLiteral("Choose a SuperSource layout →");
    case SourceType::V4l2: return QStringLiteral("/dev/video0 or /dev/v4l/by-id/… on CasparCG host");
    case SourceType::Matte: return QStringLiteral("Opaque colour: #RRGGBB");
    case SourceType::MeProgram: return QStringLiteral("M/E number (1–4)");
    case SourceType::ColorBars:
        return QStringLiteral("Select a standard pattern →");
    }
    return {};
}

} // namespace

SetupWorkspace::SetupWorkspace(Configuration* configuration, QWidget* parent)
    : QWidget(parent)
    , m_config(configuration)
{
    setObjectName(QStringLiteral("configDialog"));


    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* heading = new QLabel(tr("CROSS-POINT  ·  CHANNELS  ·  SOURCES"), this);
    heading->setText(tr("ENGINE CONFIGURATION"));
    heading->setObjectName(QStringLiteral("configHeading"));
    root->addWidget(heading);

    auto* body = new QHBoxLayout;
    auto* navigation = new QListWidget(this);
    navigation->setObjectName(QStringLiteral("setupNavigation"));
    navigation->setFixedWidth(190);
    auto* pages = new QStackedWidget(this);
    body->addWidget(navigation);
    body->addWidget(pages, 1);
    root->addLayout(body, 1);
    QList<QPair<QString, QWidget*>> sections;
    const auto addPage = [&](const QString& title, QWidget* content) {
        sections.append({title, content});
    };
    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

    auto* amcpBox = new QGroupBox(tr("AMCP CORE"), this);
    amcpBox->setObjectName(QStringLiteral("rackGroup"));
    auto* amcpForm = new QFormLayout(amcpBox);
    amcpForm->setAlignment(Qt::AlignTop);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setObjectName(QStringLiteral("casparHost"));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setObjectName(QStringLiteral("amcpPort"));
    m_portSpin->setRange(1, 65535);
    m_panelPortSpin = new QSpinBox(this);
    m_panelPortSpin->setObjectName(QStringLiteral("panelPort"));
    m_panelPortSpin->setRange(1, 65535);
    m_oscPortSpin = new QSpinBox(this);
    m_oscPortSpin->setObjectName(QStringLiteral("oscPort"));
    m_oscPortSpin->setRange(1, 65535);
    amcpForm->addRow(tr("Host"), m_hostEdit);
    amcpForm->addRow(tr("AMCP"), m_portSpin);
    amcpForm->addRow(tr("TCP panel"), m_panelPortSpin);
    amcpForm->addRow(tr("OSC in"), m_oscPortSpin);
    addPage(tr("Connections"), amcpBox);

    auto* routeBox = new QGroupBox(tr("OUTPUT ROUTING"), this);
    routeBox->setObjectName(QStringLiteral("rackGroup"));
    auto* routeForm = new QFormLayout(routeBox);
    routeForm->setAlignment(Qt::AlignTop);
    m_programChannelSpin = new QSpinBox(this);
    m_programChannelSpin->setRange(1, 128);
    m_previewChannelSpin = new QSpinBox(this);
    m_previewChannelSpin->setRange(1, 128);
    m_multiviewChannelSpin = new QSpinBox(this);
    m_multiviewChannelSpin->setRange(1, 128);
    m_programOutputSpin = new QSpinBox(this);
    m_programOutputSpin->setRange(1, 128);
    m_ndiProgramCheck = new QCheckBox(tr("On"), this);
    m_ndiProgramNameEdit = new QLineEdit(this);
    m_ndiProgramNameEdit->setObjectName(QStringLiteral("ndiProgramName"));
    m_ndiProgramNameEdit->setPlaceholderText(QStringLiteral("KAVTOR_PGM"));
    auto* ndiPgmRow = new QWidget(this);
    auto* ndiPgmLayout = new QHBoxLayout(ndiPgmRow);
    ndiPgmLayout->setContentsMargins(0, 0, 0, 0);
    ndiPgmLayout->addWidget(m_ndiProgramCheck);
    ndiPgmLayout->addWidget(m_ndiProgramNameEdit, 1);
    m_ndiCleanCheck = new QCheckBox(tr("On"), this);
    m_ndiCleanNameEdit = new QLineEdit(this);
    m_ndiCleanNameEdit->setObjectName(QStringLiteral("ndiCleanName"));
    m_ndiCleanNameEdit->setPlaceholderText(QStringLiteral("KAVTOR_CLEAN"));
    auto* ndiCleanRow = new QWidget(this);
    auto* ndiCleanLayout = new QHBoxLayout(ndiCleanRow);
    ndiCleanLayout->setContentsMargins(0, 0, 0, 0);
    ndiCleanLayout->addWidget(m_ndiCleanCheck);
    ndiCleanLayout->addWidget(m_ndiCleanNameEdit, 1);
    m_dskSourceCombo = new QComboBox(this);
    routeForm->addRow(tr("PGM channel"), m_programChannelSpin);
    routeForm->addRow(tr("PVW channel"), m_previewChannelSpin);
    routeForm->addRow(tr("Multiview"), m_multiviewChannelSpin);
    routeForm->addRow(tr("PGM out (DSK)"), m_programOutputSpin);
    routeForm->addRow(tr("NDI program"), ndiPgmRow);
    routeForm->addRow(tr("NDI clean"), ndiCleanRow);
    routeForm->addRow(tr("DSK source"), m_dskSourceCombo);
    auto* outputHint = new QLabel(tr(
        "PGM channel is the M/E mix including upstream keyers. PGM out is that mix plus DSK. "
        "NDI clean publishes the M/E; NDI program publishes PGM out. The out channel must exist in casparcg.config."), this);
    outputHint->setObjectName(QStringLiteral("mutedLabel"));
    outputHint->setWordWrap(true);
    routeForm->addRow(outputHint);
    m_destinationTable = new QTableWidget(0,7,this);
    connect(m_destinationTable,&QTableWidget::itemChanged,this,[this](){markDirty();});
    m_destinationTable->setObjectName(QStringLiteral("destinationTable"));
    m_destinationTable->setHorizontalHeaderLabels({tr("On"),tr("Name"),tr("Destination"),tr("Source"),tr("AUX role"),tr("Display"),tr("Fullscreen")});
    m_destinationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_destinationTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    m_destinationTable->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
    auto* destinationBox = new QGroupBox(tr("Additional destinations"),this);
    auto* destinationLayout = new QVBoxLayout(destinationBox);
    destinationLayout->addWidget(new QLabel(tr("Choose a source and destination independently. AUX roles reserve channels 35–38. Display numbers follow CasparCG; 0 is the default display."),this));
    destinationLayout->addWidget(m_destinationTable);
    auto* destinationButtons = new QHBoxLayout;
    auto* addDestination = new QPushButton(tr("Add output"),this);
    auto* removeDestination = new QPushButton(tr("Remove selected"),this);
    destinationButtons->addWidget(addDestination);destinationButtons->addWidget(removeDestination);destinationButtons->addStretch();
    destinationLayout->addLayout(destinationButtons);
    connect(addDestination,&QPushButton::clicked,this,[this](){if(m_destinationTable->rowCount()>=128)return;const int row=m_destinationTable->rowCount();m_destinationTable->insertRow(row);addDestinationRow(row);markDirty();});
    connect(removeDestination,&QPushButton::clicked,this,[this](){const int row=m_destinationTable->currentRow();if(row>=0){m_destinationTable->removeRow(row);markDirty();}});
    auto* outputsPage = new QWidget(this);auto* outputsLayout = new QVBoxLayout(outputsPage);
    outputsLayout->addWidget(routeBox);outputsLayout->addWidget(destinationBox,1);
    addPage(tr("Outputs"), outputsPage);

    auto* fxBox = new QGroupBox(tr("DEFAULT TRANSITION"), this);
    fxBox->setObjectName(QStringLiteral("rackGroup"));
    auto* fxRow = new QFormLayout(fxBox);
    fxRow->setAlignment(Qt::AlignTop);
    m_autoFramesSpin = new QSpinBox(this);
    m_autoFramesSpin->setRange(1, 1000);
    m_autoFramesSpin->setSuffix(tr(" f"));
    m_wipePatternCombo = new QComboBox(this);
    m_wipePatternCombo->setMaxVisibleItems(24);
    for (const WipePattern& pattern : builtinWipePatterns()) {
        m_wipePatternCombo->addItem(pattern.label, pattern.id);
        if(!pattern.implemented)if(auto* model=qobject_cast<QStandardItemModel*>(m_wipePatternCombo->model()))model->item(m_wipePatternCombo->count()-1)->setEnabled(false);
    }
    m_wipeDirectionCombo = new QComboBox(this);
    m_wipeDirectionCombo->addItem(tr("Forward"), static_cast<int>(WipeDirectionMode::Forward));
    m_wipeDirectionCombo->addItem(tr("Reverse"), static_cast<int>(WipeDirectionMode::Reverse));
    m_wipeDirectionCombo->addItem(tr("Ping-pong"), static_cast<int>(WipeDirectionMode::PingPong));
    m_wipeEdgeCombo = new QComboBox(this);
    m_wipeEdgeCombo->addItem(tr("Hard"), static_cast<int>(WipeEdgeMode::Hard));
    m_wipeEdgeCombo->addItem(tr("Soft"), static_cast<int>(WipeEdgeMode::Soft));
    m_wipeEdgeCombo->addItem(tr("Color border"), static_cast<int>(WipeEdgeMode::Border));
    m_wipeEdgeAmountSpin = new QSpinBox(this);
    m_wipeEdgeAmountSpin->setRange(0, 40);
    m_wipeEdgeAmountSpin->setSuffix(tr(" units"));
    m_wipeEdgeAmountSpin->setToolTip(tr("Resolution-independent width: one unit is four pixels at 1080 lines."));
    m_wipeBorderSpin = new QSpinBox(this);
    m_wipeBorderSpin->setRange(0, 40);
    m_wipeShadowSpin = new QSpinBox(this);
    m_wipeShadowSpin->setRange(0, 40);
    m_wipeAspectCombo = new QSpinBox(this);
    m_wipeAspectCombo->setRange(1, 1000);
    m_wipeAspectCombo->setSuffix(tr(" %"));
    m_wipeAspectCombo->setToolTip(tr("Shape width relative to height: 100% is the original shape."));
    m_wipeMultiCombo = new QComboBox(this);
    for (int count : {1, 2, 4, 9, 16}) m_wipeMultiCombo->addItem(QString::number(count), count);
    m_wipeColorButton = new QPushButton(tr("Tint"), this);
    connect(m_wipeColorButton, &QPushButton::clicked, this, [this]() {
        const QColor current(m_wipeColorButton->property("borderColor").toString());
        const QColor chosen = QColorDialog::getColor(current.isValid() ? current : Qt::white, this, tr("Wipe border color"));
        if (chosen.isValid()) {
            m_wipeColorButton->setProperty("borderColor", chosen.name());
            m_wipeColorButton->setStyleSheet(QStringLiteral("background:%1").arg(chosen.name()));
        }
    });
    m_dipColorButton = new QPushButton(tr("Choose colour"), this);
    m_dipColorButton->setObjectName(QStringLiteral("dipColor"));
    connect(m_dipColorButton, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(QColor(m_dipColorButton->property("dipColor").toString()),
            this, tr("DIP colour"));
        if (!chosen.isValid()) return;
        m_dipColorButton->setProperty("dipColor", chosen.name(QColor::HexRgb).toUpper());
        m_dipColorButton->setText(chosen.name(QColor::HexRgb).toUpper());
        markDirty();
    });
    fxRow->addRow(tr("DIP colour"), m_dipColorButton);
    m_superGainA=new QSpinBox(this);m_superGainB=new QSpinBox(this);m_superGainA->setObjectName("superMixGainA");m_superGainB->setObjectName("superMixGainB");for(auto* spin:{m_superGainA,m_superGainB}){spin->setRange(0,100);spin->setSuffix(" %");}
    m_dustRatio=new QSpinBox(this);m_dustSize=new QSpinBox(this);m_dustFlash=new QSpinBox(this);for(auto* spin:{m_dustRatio,m_dustSize,m_dustFlash})spin->setRange(0,100);m_dustSize->setMinimum(1);
    fxRow->addRow(tr("Dust Mix ratio (%)"),m_dustRatio);fxRow->addRow(tr("Dust particle size (% height)"),m_dustSize);fxRow->addRow(tr("Dust flash steps"),m_dustFlash);
    fxRow->addRow(tr("SUPER MIX midpoint A"),m_superGainA);fxRow->addRow(tr("SUPER MIX midpoint B"),m_superGainB);
    fxRow->addRow(tr("Duration (frames)"), m_autoFramesSpin);
    fxRow->addRow(tr("Wipe pattern"), m_wipePatternCombo);
    fxRow->addRow(tr("Direction"), m_wipeDirectionCombo);
    fxRow->addRow(tr("Edge"), m_wipeEdgeCombo);
    fxRow->addRow(tr("Edge width"), m_wipeEdgeAmountSpin);
    fxRow->addRow(tr("Border width"), m_wipeBorderSpin);
    fxRow->addRow(tr("Border colour"), m_wipeColorButton);
    fxRow->addRow(tr("Shadow"), m_wipeShadowSpin);
    fxRow->addRow(tr("Shape aspect"), m_wipeAspectCombo);
    fxRow->addRow(tr("Repeat count"), m_wipeMultiCombo);
    addPage(tr("Transition defaults"), fxBox);

    auto* dmeBox = new QGroupBox(tr("DME BACKGROUNDS"), this);
    dmeBox->setObjectName(QStringLiteral("rackGroup"));
    auto* dmeForm = new QFormLayout(dmeBox);
    dmeForm->setAlignment(Qt::AlignTop);
    const QStringList effectLabels = {tr("Move"), tr("Cube"), tr("Zoom"),
        tr("Page turn"), tr("Page roll"), tr("GLOBAL — common background")};
    for (int index = 0; index < 6; ++index) {
        auto* selector = new QComboBox(this);
        selector->setObjectName(QStringLiteral("dmeBackground_%1").arg(kDmeBackgroundEffects[index]));
        selector->setMaxVisibleItems(16);
        selector->setToolTip(tr("Silent fill behind this effect. Input generators supply solid colours. Requires casparMIX 0.6.0."));
        m_dmeBackgroundCombos[index] = selector;
        auto* controls = new QWidget(this);auto* row = new QHBoxLayout(controls);row->setContentsMargins(0,0,0,0);
        auto* image = new QLineEdit(this);image->setObjectName("dmeImage_"+kDmeBackgroundEffects[index]);
        image->setPlaceholderText(tr("Image file accessible to CasparCG"));
        image->setToolTip(tr("Static asset, not a switcher input. The CasparCG host must be able to read this file."));
        auto* browse = new QPushButton(tr("Choose image…"),this);browse->setObjectName("dmeImageBrowse_"+kDmeBackgroundEffects[index]);
        m_dmeBackgroundImages[index]=image;
        row->addWidget(selector,1);row->addWidget(image,2);row->addWidget(browse);
        connect(selector,&QComboBox::currentIndexChanged,this,[selector,image,browse]{const bool selected=selector->currentData().toInt()==-3;image->setVisible(selected);browse->setVisible(selected);});
        connect(browse,&QPushButton::clicked,this,[this,image]{auto path=QFileDialog::getOpenFileName(this,tr("Static DME background"),image->text(),tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"));if(!path.isEmpty())image->setText(path);});
        if(index==5)dmeForm->insertRow(0,effectLabels[index],controls);else dmeForm->addRow(effectLabels[index], controls);
    }
    auto* dmeHint = new QLabel(tr("Effects use GLOBAL by default. Select an explicit fill for a CUSTOM override. Backgrounds have no audio. "
        "Choose a static image without using an input, a Matte input for solid colour, any prepared input for video, or an M/E program. "
        "The running mixer rejects sources that would feed back into the selected M/E. "
        "Paper backside appearance is separate from the background."), this);
    dmeHint->setWordWrap(true);
    dmeHint->setObjectName(QStringLiteral("mutedLabel"));
    dmeForm->addRow(dmeHint);
    addPage(tr("DME backgrounds"), dmeBox);

    auto* sourcesBox = new QGroupBox(tr("INPUT SOURCES"), this);
    sourcesBox->setObjectName(QStringLiteral("rackGroup"));
    auto* sourcesLayout = new QVBoxLayout(sourcesBox);
    m_sourceTable = new QTableWidget(this);
    m_sourceTable->setObjectName(QStringLiteral("sourceMatrix"));
    m_sourceTable->setColumnCount(9);
    m_sourceTable->setHorizontalHeaderLabels({
        tr("On"), tr("Input"), tr("Name"), tr("Type"), tr("Ch"), tr("URI / descriptor"), tr(""), tr("Running state"), tr("Loop")
    });
    m_sourceTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_sourceTable->verticalHeader()->setVisible(false);
    m_sourceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sourceTable->setAlternatingRowColors(true);
    sourcesLayout->addWidget(m_sourceTable);
    auto* hint = new QLabel(tr(
        "Clip: media/ or local file.  NDI: HOSTNAME (source) or ndi://host/source.  Stream: RTSP/UDP/SRT/HTTP-TS.  "
        "HTTP: CEF page.  Still: PNG/WebP alpha.  Bars: empty / hd / pal.  Matte: #RRGGBB or colour picker.  V4L2: Linux video device on CasparCG host.  SDI: DeckLink index."), this);
    hint->setObjectName(QStringLiteral("mutedLabel"));
    hint->setWordWrap(true);
    sourcesLayout->addWidget(hint);
    addPage(tr("Inputs"), sourcesBox);
    m_superSourceEditor=new SuperSourceEditor(m_config,this);
    connect(m_superSourceEditor,&SuperSourceEditor::changed,this,&SetupWorkspace::markDirty);
    addPage(tr("SuperSources"),m_superSourceEditor);

    auto* mvBox = new QGroupBox(tr("MULTIVIEW"), this);
    mvBox->setObjectName(QStringLiteral("rackGroup"));
    auto* mvForm = new QFormLayout(mvBox);
    mvForm->setAlignment(Qt::AlignTop);
    m_programSideCombo = new QComboBox(this);
    m_programSideCombo->addItem(tr("Program on the left"), 1);
    m_programSideCombo->addItem(tr("Program on the right"), 0);
    m_nameEdgeCombo = new QComboBox(this);
    m_nameEdgeCombo->addItem(tr("Bottom"), QStringLiteral("bottom"));
    m_nameEdgeCombo->addItem(tr("Top"), QStringLiteral("top"));
    m_nameAlignCombo = new QComboBox(this);
    m_nameAlignCombo->addItem(tr("Center"), QStringLiteral("center"));
    m_nameAlignCombo->addItem(tr("Left"), QStringLiteral("left"));
    m_nameAlignCombo->addItem(tr("Right"), QStringLiteral("right"));
    m_clockEdgeCombo = new QComboBox(this);
    m_clockEdgeCombo->addItem(tr("Top"), QStringLiteral("top"));
    m_clockEdgeCombo->addItem(tr("Bottom"), QStringLiteral("bottom"));
    m_clockAlignCombo = new QComboBox(this);
    m_clockAlignCombo->addItem(tr("Center"), QStringLiteral("center"));
    m_clockAlignCombo->addItem(tr("Left"), QStringLiteral("left"));
    m_clockAlignCombo->addItem(tr("Right"), QStringLiteral("right"));
    m_safePreviewAspect = new QComboBox(this);
    m_safeProgramAspect = new QComboBox(this);
    for (const QString& ratio : {QStringLiteral("16:9"), QStringLiteral("4:3"), QStringLiteral("9:16"), QStringLiteral("14:9"), QStringLiteral("1:1"), QStringLiteral("4:5")}) {
        m_safePreviewAspect->addItem(ratio, ratio); m_safeProgramAspect->addItem(ratio, ratio);
    }
    m_safePreset = new QComboBox(this);
    m_safePreset->addItem(tr("EBU R95: action 3.5%, graphics 5%"), QStringLiteral("ebu-r95"));
    m_safePreset->addItem(tr("Classic: action 5%, titles 10%"), QStringLiteral("legacy"));
    mvForm->addRow(tr("Preview framing guide"), m_safePreviewAspect);
    mvForm->addRow(tr("Program framing guide"), m_safeProgramAspect);
    mvForm->addRow(tr("Safe margins per edge"), m_safePreset);
    m_safePreviewCheck = new QCheckBox(tr("Safe area on preview"), this);
    m_safeProgramCheck = new QCheckBox(tr("Safe area on program"), this);
    mvForm->addRow(tr("Bus position"), m_programSideCombo);
    mvForm->addRow(tr("Names"), m_nameEdgeCombo);
    mvForm->addRow(tr("Name alignment"), m_nameAlignCombo);
    mvForm->addRow(tr("Clip timers"), m_clockEdgeCombo);
    mvForm->addRow(tr("Timer alignment"), m_clockAlignCombo);
    mvForm->addRow(QString(), m_safePreviewCheck);
    mvForm->addRow(QString(), m_safeProgramCheck);
    auto* safeAll = new QPushButton(tr("Use preview guide on both buses"), this);
    mvForm->addRow(safeAll);
    connect(safeAll, &QPushButton::clicked, this, [this] {
        m_safeProgramAspect->setCurrentIndex(m_safePreviewAspect->currentIndex());
        m_safePreviewCheck->setChecked(true); m_safeProgramCheck->setChecked(true);
    });
    m_metersCheck = new QCheckBox(tr("Show audio meters"), this);
    mvForm->addRow(QString(), m_metersCheck);
    addPage(tr("Multiview"), mvBox);

    auto* meBox = new QGroupBox(tr("M/E TOPOLOGY & KEY SOURCES"), this);
    auto* meForm = new QFormLayout(meBox);
    meForm->setAlignment(Qt::AlignTop);
    auto* topology = new QLabel(tr("M/E 1 uses the configured preview and program channels. "
        "M/E 2: PVW 14 / PGM 13. M/E 3: PVW 16 / PGM 15. "
        "M/E 4: PVW 18 / PGM 17. Input 12 is the next M/E re-entry; unavailable on M/E 4."), this);
    topology->setWordWrap(true);
    meForm->addRow(topology);
    auto* keyMode = new QLabel(tr("Four upstream keys per M/E and two downstream keys on M/E 1 air output. "
        "LINEAR uses embedded alpha; CHR adds native chroma extraction. Each key has independent chroma settings and a rectangular MAIN MASK. Luma, separate fill/key and DVE key processing are not available yet."), this);
    keyMode->setWordWrap(true);
    meForm->addRow(keyMode);
    for (int me = 0; me < 4; ++me) {
        for (int key = 0; key < 4; ++key) {
            auto* selector = new QComboBox(this);
            m_keySources.append(selector);
            auto* row=new QWidget(this);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
            layout->addWidget(selector,1);auto* edit=new QPushButton(tr("Processing…"),row);
            edit->setObjectName(QStringLiteral("key-processing-%1").arg(me*4+key));
            layout->addWidget(edit);connect(edit,&QPushButton::clicked,this,[this,me,key]{editKeyProcessing(me*4+key);});
            meForm->addRow(tr("M/E %1 · Key %2 source").arg(me + 1).arg(key + 1), row);
        }
    }
    m_dsk2SourceCombo = new QComboBox(this);
    meForm->addRow(tr("DSK 2 source"), m_dsk2SourceCombo);
    for(int slot=0;slot<2;++slot) {
        auto* edit=new QPushButton(tr("DSK %1 processing…").arg(slot+1),meBox);
        edit->setObjectName(QStringLiteral("key-processing-%1").arg(16+slot));
        connect(edit,&QPushButton::clicked,this,[this,slot]{editKeyProcessing(16+slot);});
        meForm->addRow(edit);
    }
    addPage(tr("M/Es & keyers"), meBox);

    auto* mediaBox = new QGroupBox(tr("STINGER LIBRARY"), this);
    auto* mediaLayout = new QVBoxLayout(mediaBox);
    m_stingerTable = new QTableWidget(10, 4, this);
    m_stingerTable->setObjectName(QStringLiteral("stingerLibrary"));
    m_stingerTable->setHorizontalHeaderLabels({tr("Media"), tr("Reverse media"), tr("Cut frame"), tr("Length (frames)")});
    m_stingerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stingerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mediaLayout->addWidget(m_stingerTable);
    auto* mediaHint = new QLabel(tr("Ten slots. Leave media empty to disable a slot. Media paths refer to the CasparCG server."), this);
    mediaHint->setWordWrap(true);
    mediaLayout->addWidget(mediaHint);
    addPage(tr("Media & stingers"), mediaBox);

    auto* auxBox = new QGroupBox(tr("AUXILIARY OUTPUTS"), this);
    auto* auxLayout = new QVBoxLayout(auxBox);
    auto* auxHint = new QLabel(tr("Assign AUX1–AUX4 to enabled destinations under Outputs. "
        "Each role has an independent route. On the panel, select AUX1–3 or EDIT PVW (AUX4), then select its input on the AUX bus. The last crosspoint selects the next M/E program. Other M/E programs, previews and multiview can be selected under Outputs."), this);
    auxHint->setWordWrap(true);
    auxLayout->addWidget(auxHint);
    auxLayout->addStretch();
    addPage(tr("Auxiliaries"), auxBox);
    auto* diagnosticsBox = new QGroupBox(tr("SESSION DIAGNOSTICS"), this);
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsBox);
    m_diagnostics = new QPlainTextEdit(this);
    m_diagnostics->setObjectName(QStringLiteral("sessionDiagnostics"));
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setMaximumBlockCount(200);
    diagnosticsLayout->addWidget(m_diagnostics);
    auto* clearDiagnostics = new QPushButton(tr("Clear session log"), this);
    diagnosticsLayout->addWidget(clearDiagnostics);
    connect(clearDiagnostics, &QPushButton::clicked, m_diagnostics, &QPlainTextEdit::clear);
    addPage(tr("Diagnostics"), diagnosticsBox);
    const QStringList order = {tr("Connections"), tr("Inputs"), tr("SuperSources"), tr("Outputs"), tr("M/Es & keyers"),
        tr("Multiview"), tr("Media & stingers"), tr("Transition defaults"), tr("DME backgrounds"), tr("Auxiliaries"), tr("Diagnostics")};
    for (const QString& title : order) {
        for (const auto& section : sections) {
            if (section.first != title) continue;
            navigation->addItem(title);
            auto* scroll = new QScrollArea(this);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(section.second);
            pages->addWidget(scroll);
        }
    }
    navigation->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(this);
    auto* discard = buttons->addButton(tr("Discard"), QDialogButtonBox::RejectRole);
    auto* save = buttons->addButton(tr("Save configuration"), QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("saveApplyButton"));
    discard->setObjectName(QStringLiteral("discardButton"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SetupWorkspace::saveRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &SetupWorkspace::reload);
    root->addWidget(buttons);

    populate();
    for (auto* edit : findChildren<QLineEdit*>())
        connect(edit, &QLineEdit::textChanged, this, &SetupWorkspace::markDirty);
    for (auto* box : findChildren<QSpinBox*>())
        connect(box, &QSpinBox::valueChanged, this, &SetupWorkspace::markDirty);
    for (auto* box : findChildren<QComboBox*>())
        connect(box, &QComboBox::currentIndexChanged, this, &SetupWorkspace::markDirty);
    for (auto* box : findChildren<QCheckBox*>())
        connect(box, &QCheckBox::toggled, this, &SetupWorkspace::markDirty);
    connect(m_sourceTable, &QTableWidget::itemChanged, this, &SetupWorkspace::markDirty);
    connect(m_sourceTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_loading || !item || item->column() != 2) return;
        QList<QComboBox*> selectors = m_keySources;
        for (auto* background : m_dmeBackgroundCombos) selectors.append(background);
        selectors.append(m_dskSourceCombo);
        selectors.append(m_dsk2SourceCombo);
        for (auto* selector : selectors) {
            const int index = selector->findData(item->row());
            if (index >= 0) selector->setItemText(index, tr("Input %1 — %2").arg(item->row() + 1).arg(item->text()));
        }
    });
    connect(m_stingerTable, &QTableWidget::itemChanged, this, &SetupWorkspace::markDirty);
    connect(m_wipeColorButton, &QPushButton::clicked, this, &SetupWorkspace::markDirty);
}

void SetupWorkspace::setInputState(int id, const QString& text)
{
    if (id < 0 || id >= m_sourceTable->rowCount()) return;
    const QSignalBlocker blocker(m_sourceTable);
    m_sourceTable->item(id, 7)->setText(text);
}

void SetupWorkspace::appendDiagnostic(const QString& message)
{
    m_diagnostics->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        + QStringLiteral("  ") + message);
}

void SetupWorkspace::markDirty()
{
    if (m_loading || m_dirty) return;
    m_dirty = true;
    emit dirtyChanged(true);
}

void SetupWorkspace::reload()
{
    populate();
    m_dirty = false;
    emit dirtyChanged(false);
}

void SetupWorkspace::populate()
{
    if(m_superSourceEditor)m_superSourceEditor->setLayouts(m_config->superSources());
    QScopedValueRollback<bool> loading(m_loading, true);
    m_hostEdit->setText(m_config->casparHost());
    m_portSpin->setValue(m_config->casparPort());
    m_previewChannelSpin->setValue(m_config->previewChannel());
    m_programChannelSpin->setValue(m_config->programChannel());
    m_multiviewChannelSpin->setValue(m_config->multiviewChannel());
    m_programOutputSpin->setValue(m_config->programOutputChannel());
    m_ndiProgramCheck->setChecked(m_config->ndiProgramEnabled());
    m_ndiProgramNameEdit->setText(m_config->ndiProgramName());
    m_ndiCleanCheck->setChecked(m_config->ndiCleanEnabled());
    m_ndiCleanNameEdit->setText(m_config->ndiCleanName());
    m_dipColorButton->setProperty("dipColor", m_config->dipColor());
    m_dipColorButton->setText(m_config->dipColor());
    m_dustRatio->setValue(m_config->dustRatio());m_dustSize->setValue(m_config->dustSize());m_dustFlash->setValue(m_config->dustFlash());
    m_superGainA->setValue(m_config->superMixGainA());m_superGainB->setValue(m_config->superMixGainB());
    m_autoFramesSpin->setValue(m_config->autoDurationFrames());
    const int wipeIndex = m_wipePatternCombo->findData(m_config->wipePatternId());
    m_wipePatternCombo->setCurrentIndex(wipeIndex >= 0 ? wipeIndex : 0);
    const int dirIndex = m_wipeDirectionCombo->findData(static_cast<int>(m_config->wipeDirectionMode()));
    m_wipeDirectionCombo->setCurrentIndex(dirIndex >= 0 ? dirIndex : 0);
    const int edgeIndex = m_wipeEdgeCombo->findData(static_cast<int>(m_config->wipeEdgeMode()));
    m_wipeEdgeCombo->setCurrentIndex(edgeIndex >= 0 ? edgeIndex : 0);
    m_wipeEdgeAmountSpin->setValue(m_config->wipeEdgeAmount());
    m_wipeBorderSpin->setValue(m_config->wipeBorderAmount());
    m_wipeShadowSpin->setValue(m_config->wipeShadowAmount());
    m_wipeAspectCombo->setValue(qRound(100.0 * m_config->wipeAspectW() / m_config->wipeAspectH()));
    m_wipeMultiCombo->setCurrentIndex(m_wipeMultiCombo->findData(m_config->wipeMulti()));
    m_wipeColorButton->setProperty("borderColor", m_config->wipeBorderColor());
    m_wipeColorButton->setStyleSheet(QStringLiteral("background:%1").arg(m_config->wipeBorderColor()));
    m_panelPortSpin->setValue(m_config->panelPort());
    m_oscPortSpin->setValue(m_config->oscPort());
    const int sideIndex = m_programSideCombo->findData(m_config->programLeft() ? 1 : 0);
    m_programSideCombo->setCurrentIndex(sideIndex >= 0 ? sideIndex : 0);
    const auto selectText = [](QComboBox* box, const QString& text) {
        const int index = box->findData(text);
        box->setCurrentIndex(index >= 0 ? index : 0);
    };
    selectText(m_nameEdgeCombo, m_config->nameEdge());
    selectText(m_nameAlignCombo, m_config->nameAlign());
    selectText(m_clockEdgeCombo, m_config->clockEdge());
    selectText(m_clockAlignCombo, m_config->clockAlign());
    m_safePreviewAspect->setCurrentIndex(m_safePreviewAspect->findData(m_config->safePreviewAspect()));
    m_safeProgramAspect->setCurrentIndex(m_safeProgramAspect->findData(m_config->safeProgramAspect()));
    m_safePreset->setCurrentIndex(m_safePreset->findData(m_config->safePreset()));
    m_safePreviewCheck->setChecked(m_config->safePreview());
    m_safeProgramCheck->setChecked(m_config->safeProgram());

    m_destinationTable->setRowCount(0);
    for(const auto& out : m_config->destinations()) {
        const int row=m_destinationTable->rowCount();m_destinationTable->insertRow(row);addDestinationRow(row);
        qobject_cast<QCheckBox*>(m_destinationTable->cellWidget(row,0))->setChecked(out.enabled);
        m_destinationTable->item(row,1)->setText(out.name);
        auto* type=qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,2));type->setCurrentIndex(type->findData(out.type));
        auto* source=qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,3));source->setCurrentIndex(source->findData(out.source));
        qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,4))->setCurrentIndex(out.aux);
        qobject_cast<QSpinBox*>(m_destinationTable->cellWidget(row,5))->setValue(out.device);
        qobject_cast<QCheckBox*>(m_destinationTable->cellWidget(row,6))->setChecked(out.fullscreen);
    }
    const QList<Source> sources = m_config->sources();
    m_sourceTable->setRowCount(m_config->maxSources());
    for (int row = 0; row < m_config->maxSources(); ++row) {
        if (!m_sourceTable->cellWidget(row, 0)) addSourceRow(row);
        auto* enabled = qobject_cast<QCheckBox*>(m_sourceTable->cellWidget(row, 0));
        auto* type = qobject_cast<QComboBox*>(m_sourceTable->cellWidget(row, 3));
        auto* channel = qobject_cast<QSpinBox*>(m_sourceTable->cellWidget(row, 4));
        auto* argument = qobject_cast<QLineEdit*>(m_sourceTable->cellWidget(row, 5));
        const Source* src = (row < sources.size()) ? &sources.at(row) : nullptr;
        if (src) {
            enabled->setChecked(src->enabled);
            qobject_cast<QCheckBox*>(m_sourceTable->cellWidget(row, 8))->setChecked(src->loop);
            m_sourceTable->item(row, 1)->setText(QString::number(src->id + 1));
            m_sourceTable->item(row, 2)->setText(src->name);
            type->setCurrentIndex(typeIndex(src->type));
            channel->setValue(src->casparChannel);
            argument->setProperty("layoutId",src->type==SourceType::SuperSource?src->argument:QString());
            argument->setText(src->argument);
        } else {
            enabled->setChecked(false);
            m_sourceTable->item(row, 1)->setText(QString::number(row + 1));
            m_sourceTable->item(row, 2)->setText(QStringLiteral("Source %1").arg(row + 1));
            type->setCurrentIndex(typeIndex(SourceType::File));
            channel->setValue(row + 1);
        }
        const bool reserved = row == 11 || row == 23;
        if (reserved) { enabled->setChecked(false); m_sourceTable->item(row, 2)->setText(QStringLiteral("Next M/E (reserved)")); }
        for (int col : {0, 3, 4, 5, 6}) if (auto* w = m_sourceTable->cellWidget(row, col)) w->setEnabled(!reserved);
        updateArgumentPrompt(row);
    }

    for (int index = 0; index < 6; ++index) {
        auto* selector = m_dmeBackgroundCombos[index];
        selector->clear();
        if(index<5)selector->addItem(tr("GLOBAL — use common background"),-2);
        selector->addItem(tr("Black"), -1);
        selector->addItem(tr("Static image"), -3);
        m_dmeBackgroundImages[index]->setText(m_config->dmeBackgroundImage(kDmeBackgroundEffects[index]));
        for (const Source& input : sources) {
            if (input.id == 11 || input.id == 23) continue;
            selector->addItem(tr("Input %1 — %2").arg(input.id + 1).arg(input.name), input.id);
        }
        for (int me = 0; me < 4; ++me)
            selector->addItem(tr("M/E %1 program").arg(me + 1), 1000 + me);
        const int selected=index<5&&!m_config->dmeBackgroundCustom(kDmeBackgroundEffects[index])?-2:m_config->dmeBackground(kDmeBackgroundEffects[index]);
        selector->setCurrentIndex(qMax(0, selector->findData(selected)));
        m_dmeBackgroundImages[index]->setVisible(selected==-3);
        findChild<QPushButton*>("dmeImageBrowse_"+kDmeBackgroundEffects[index])->setVisible(selected==-3);
    }

    m_dskSourceCombo->clear();
    m_dskSourceCombo->addItem(tr("None"), -1);
    for (int i = 0; i < m_config->maxSources(); ++i) {
        const auto* input=m_config->sourceById(i);
        if(i==11||i==23||(input&&input->type==SourceType::MeProgram))continue;
        const QString name = m_sourceTable->item(i, 2)->text();
        m_dskSourceCombo->addItem(tr("SRC%1 — %2").arg(i + 1).arg(name), i);
    }
    const int dskIndex = m_dskSourceCombo->findData(m_config->dskSourceId());
    m_dskSourceCombo->setCurrentIndex(qMax(0, dskIndex));

    m_metersCheck->setChecked(m_config->meters());
    for(int index=0;index<18;++index)m_processingDraft[index]=m_config->keyProcessing(index<16?index/4:0,index<16?index%4:index-16,index>=16);
    QList<QComboBox*> assignments = m_keySources;
    assignments.append(m_dsk2SourceCombo);
    for (int index = 0; index < assignments.size(); ++index) {
        auto* selector = assignments.at(index);
        selector->clear();
        selector->addItem(tr("None"), -1);
        for (const Source& source : sources) if(source.id!=11&&source.id!=23&&source.type!=SourceType::MeProgram)
            selector->addItem(tr("Input %1 — %2").arg(source.id + 1).arg(source.name), source.id);
        const int id = index < 16 ? m_config->meKeySource(index / 4, index % 4) : m_config->dskSource(1);
        selector->setCurrentIndex(qMax(0, selector->findData(id)));
    }
    for (int row = 0; row < 10; ++row) {
        const StingerSlot* slot = m_config->stinger(row);
        m_stingerTable->setItem(row, 0, new QTableWidgetItem(slot ? slot->media : QString()));
        m_stingerTable->setItem(row, 1, new QTableWidgetItem(slot ? slot->reverse : QString()));
        for (int column = 2; column < 4; ++column) {
            auto* value = qobject_cast<QSpinBox*>(m_stingerTable->cellWidget(row, column));
            if (!value) {
                value = new QSpinBox(this);
                value->setRange(column == 2 ? 0 : 1, column == 2 ? 2999 : 3000);
                m_stingerTable->setCellWidget(row, column, value);
            }
            value->setValue(slot ? (column == 2 ? slot->cutFrames : slot->lengthFrames) : (column == 2 ? 12 : 50));
        }
    }
    m_sourceTable->setColumnWidth(0, 46);
    m_sourceTable->setColumnWidth(1, 64);
    m_sourceTable->setColumnWidth(2, 180);
    m_sourceTable->setColumnWidth(3, 100);
    m_sourceTable->setColumnWidth(4, 64);
    m_sourceTable->setColumnWidth(6, 182);
    m_sourceTable->setColumnWidth(7, 120);
    m_sourceTable->setColumnWidth(8, 52);
    m_sourceTable->verticalHeader()->setDefaultSectionSize(40);


}

void SetupWorkspace::addSourceRow(int row)
{
    auto* enabled = new QCheckBox(this);
    m_sourceTable->setCellWidget(row, 0, enabled);

    auto* idItem = new QTableWidgetItem;
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    idItem->setTextAlignment(Qt::AlignCenter);
    m_sourceTable->setItem(row, 1, idItem);
    m_sourceTable->setItem(row, 2, new QTableWidgetItem);
    auto* status = new QTableWidgetItem(tr("Not prepared"));
    status->setFlags(status->flags() & ~Qt::ItemIsEditable);
    m_sourceTable->setItem(row, 7, status);

    auto* loop = new QCheckBox(this);
    loop->setChecked(true);
    loop->setToolTip(tr("Repeat this clip at its end. Changing this setting reloads the clip when applied."));
    m_sourceTable->setCellWidget(row, 8, loop);

    auto* type = new QComboBox(this);
    type->addItems(kTypeLabels);
    type->setProperty("row", row);
    connect(type, &QComboBox::currentIndexChanged, this, &SetupWorkspace::onTypeChanged);
    m_sourceTable->setCellWidget(row, 3, type);

    auto* channel = new QSpinBox(this);
    channel->setRange(1, 128);
    m_sourceTable->setCellWidget(row, 4, channel);

    auto* argument = new QLineEdit(this);
    m_sourceTable->setCellWidget(row, 5, argument);

    auto* browse = new QPushButton(tr("…"), this);
    browse->setProperty("row", row);
    browse->setFixedWidth(32);
    connect(browse, &QPushButton::clicked, this, &SetupWorkspace::onBrowseArgument);
    m_sourceTable->setCellWidget(row, 6, browse);
}

void SetupWorkspace::updateArgumentPrompt(int row)
{
    auto* type = qobject_cast<QComboBox*>(m_sourceTable->cellWidget(row, 3));
    auto* argument = qobject_cast<QLineEdit*>(m_sourceTable->cellWidget(row, 5));
    auto* browse = qobject_cast<QPushButton*>(m_sourceTable->cellWidget(row, 6));
    if (!type || !argument || !browse) {
        return;
    }
    const SourceType sourceType = typeFromIndex(type->currentIndex());
    if (auto* loop = m_sourceTable->cellWidget(row, 8))
        loop->setEnabled(sourceType == SourceType::File && row != 11 && row != 23);
    argument->setPlaceholderText(argumentPlaceholder(sourceType));
    const bool matte = sourceType == SourceType::Matte;
    const bool bars = sourceType == SourceType::ColorBars;
    argument->setReadOnly(bars||sourceType==SourceType::SuperSource);
    if(sourceType==SourceType::SuperSource){
        auto id=argument->property("layoutId").toString();if(id.isEmpty()){id=argument->text();argument->setProperty("layoutId",id);}
        QString title=tr("Choose layout");for(auto& layout:m_superSourceEditor->layouts())if(layout.id==id)title=layout.name;
        argument->setText(title);browse->setFixedWidth(70);browse->setText(tr("Choose"));browse->setEnabled(true);return;
    }
    if (auto* old = browse->menu()) { browse->setMenu(nullptr); old->deleteLater(); }
    if (bars) {
        auto* menu = new QMenu(browse);
        const QList<QPair<QString, QString>> patterns = {
            {QStringLiteral("EBU75"), tr("EBU 75%")}, {QStringLiteral("EBU100"), tr("EBU 100%")},
            {QStringLiteral("SMPTESD"), tr("SMPTE SD (EG 1)")},
            {QStringLiteral("SMPTEHD"), tr("SMPTE HD (RP 219)")}};
        QString selected = tr("Choose pattern");
        for (const auto& pattern : patterns) {
            auto* action = menu->addAction(pattern.second);
            action->setCheckable(true);
            const bool active = pattern.first == colorBarsPattern(argument->text());
            action->setChecked(active);
            if (active) selected = pattern.second;
            connect(action, &QAction::triggered, this, [this, argument, row, key = pattern.first] {
                argument->setText(key); updateArgumentPrompt(row); markDirty();
            });
        }
        browse->setFixedWidth(170);
        browse->setText(selected);
        browse->setToolTip(tr("Choose the standard color-bar pattern generated by the engine"));
        browse->setMenu(menu);
        browse->setEnabled(true);
        return;
    }
    browse->setFixedWidth(32);
    browse->setText(matte ? tr("RGB") : tr("…"));
    browse->setToolTip(matte ? tr("Choose matte colour") : tr("Choose source file"));
    browse->setEnabled(sourceType == SourceType::File || sourceType == SourceType::Html
        || sourceType == SourceType::Still || matte);
}

void SetupWorkspace::onTypeChanged()
{
    auto* type = qobject_cast<QComboBox*>(sender());
    if (!type) {
        return;
    }
    updateArgumentPrompt(type->property("row").toInt());
}


void SetupWorkspace::onBrowseArgument()
{
    auto* browse = qobject_cast<QPushButton*>(sender());
    if (!browse) {
        return;
    }
    const int row = browse->property("row").toInt();
    auto* type = qobject_cast<QComboBox*>(m_sourceTable->cellWidget(row, 3));
    auto* argument = qobject_cast<QLineEdit*>(m_sourceTable->cellWidget(row, 5));
    if (!type || !argument) {
        return;
    }
    const SourceType sourceType = typeFromIndex(type->currentIndex());
    if(sourceType==SourceType::SuperSource){QMenu menu(this);for(auto& layout:m_superSourceEditor->layouts()){auto action=menu.addAction(layout.name);connect(action,&QAction::triggered,this,[this,argument,layout](){argument->setProperty("layoutId",layout.id);argument->setText(layout.name);markDirty();});}menu.exec(browse->mapToGlobal(QPoint(0,browse->height())));return;}
    if (sourceType == SourceType::ColorBars) return;
    if (sourceType == SourceType::Matte) {
        const QColor current(argument->text().trimmed());
        const QColor chosen = QColorDialog::getColor(current.isValid() ? current : Qt::black,
            this, tr("Matte colour"));
        if (chosen.isValid()) argument->setText(chosen.name(QColor::HexRgb).toUpper());
        return;
    }
    QString filter = tr("All files (*)");
    if (sourceType == SourceType::File) {
        filter = tr("Video (*.mp4 *.mov *.mxf *.mkv *.avi *.webm);;All files (*)");
    } else if (sourceType == SourceType::Html) {
        filter = tr("HTML (*.html *.htm);;All files (*)");
    } else if (sourceType == SourceType::Still) {
        filter = tr("Images (*.png *.webp *.tif *.tiff *.tga);;All files (*)");
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("Select source"), argument->text(), filter);
    if (!path.isEmpty()) {
        argument->setText(path);
    }
}

QString SetupWorkspace::validationError() const
{
    for(int index=0;index<6;++index)if(m_dmeBackgroundCombos[index]->currentData().toInt()==-3&&staticDmeImageProducer(m_dmeBackgroundImages[index]->text()).isEmpty())return tr("%1 needs a valid static image filename.").arg(kDmeBackgroundEffects[index]);
    if (m_hostEdit->text().trimmed().isEmpty()) return tr("A CasparCG host is required.");
    if (m_ndiProgramCheck->isChecked() && m_ndiProgramNameEdit->text().trimmed().isEmpty())
        return tr("The enabled NDI program feed needs a name.");
    if (m_ndiCleanCheck->isChecked() && m_ndiCleanNameEdit->text().trimmed().isEmpty())
        return tr("The enabled NDI clean feed needs a name.");
    if (m_ndiProgramCheck->isChecked() && m_ndiCleanCheck->isChecked()
        && m_ndiProgramNameEdit->text().trimmed() == m_ndiCleanNameEdit->text().trimmed())
        return tr("NDI program and clean feeds need distinct names.");
    const auto invalidCommandText = [](const QString& value) {
        return value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n')) || value.contains(QChar(0));
    };
    for (auto* field : {m_hostEdit, m_ndiProgramNameEdit, m_ndiCleanNameEdit})
        if (invalidCommandText(field->text())) return tr("Connection and output fields cannot contain line breaks or NUL characters.");
    QSet<int> channels;
    for (auto* box : {m_previewChannelSpin, m_programChannelSpin, m_multiviewChannelSpin, m_programOutputSpin}) {
        const int channel = box->value();
        if ((channel >= 13 && channel <= 18) || (channel >=35 && channel<=38) || channels.contains(channel))
            return tr("Output channels must be distinct and cannot use M/E channels 13–18.");
        channels.insert(channel);
    }
    for (int row = 0; row < m_sourceTable->rowCount(); ++row) {
        if (!qobject_cast<QCheckBox*>(m_sourceTable->cellWidget(row, 0))->isChecked()) continue;
        const QString argument = qobject_cast<QLineEdit*>(m_sourceTable->cellWidget(row, 5))->text();
        if (invalidCommandText(argument)) return tr("Input %1 has an invalid descriptor.").arg(row + 1);
        auto* type = qobject_cast<QComboBox*>(m_sourceTable->cellWidget(row, 3));
        if (typeFromIndex(type->currentIndex()) == SourceType::Decklink) {
            bool ok = false;
            const int device = argument.trimmed().toInt(&ok);
            if (!ok || device < 1) return tr("Input %1 needs a positive DeckLink device number.").arg(row + 1);
        }
        if (typeFromIndex(type->currentIndex()) == SourceType::V4l2) {
            Source device; device.type = SourceType::V4l2; device.argument = argument;
            if (device.producerCommand().isEmpty())
                return tr("Input %1 needs /dev/videoN or a /dev/v4l/by-id/ or by-path/ device on the CasparCG host.").arg(row + 1);
        }
        if (typeFromIndex(type->currentIndex()) == SourceType::Matte) {
            Source matte; matte.type = SourceType::Matte; matte.argument = argument;
            if (matte.producerCommand().isEmpty())
                return tr("Input %1 needs an opaque matte colour in #RRGGBB format.").arg(row + 1);
        }
        if(typeFromIndex(type->currentIndex()) == SourceType::MeProgram) {
            bool ok=false;const int me=argument.trimmed().toInt(&ok);
            if(!ok||me<1||me>4)return tr("Input %1 needs an M/E number between 1 and 4.").arg(row+1);
            continue;
        }
        const int channel = qobject_cast<QSpinBox*>(m_sourceTable->cellWidget(row, 4))->value();
        if ((channel >= 13 && channel <= 18) || (channel >=35 && channel<=38) || channels.contains(channel))
            return tr("Input %1 has a channel collision. Enabled inputs, outputs and M/Es need separate channels.").arg(row + 1);
        channels.insert(channel);
    }
    const auto message=validateSuperSources(m_superSourceEditor->layouts());if(!message.isEmpty())return message;
    Configuration graph;graph.fromJson(m_config->toJson());graph.setProgramChannel(m_programChannelSpin->value());graph.setPreviewChannel(m_previewChannelSpin->value());graph.setMultiviewChannel(m_multiviewChannelSpin->value());graph.setProgramOutputChannel(m_programOutputSpin->value());QList<Source> graphSources;
    for(int row=0;row<m_sourceTable->rowCount();++row){Source src;src.id=row;src.casparChannel=qobject_cast<QSpinBox*>(m_sourceTable->cellWidget(row,4))->value();src.type=typeFromIndex(qobject_cast<QComboBox*>(m_sourceTable->cellWidget(row,3))->currentIndex());src.argument=sourceArgument(qobject_cast<QLineEdit*>(m_sourceTable->cellWidget(row,5)),src.type);src.enabled=qobject_cast<QCheckBox*>(m_sourceTable->cellWidget(row,0))->isChecked();graphSources.append(src);}
    graph.setSources(graphSources);graph.setSuperSources(m_superSourceEditor->layouts());if(!graph.superSourceGraphError().isEmpty())return graph.superSourceGraphError();
    QSet<int> auxRoles; QSet<QString> ndiNames;
    if(m_ndiProgramCheck->isChecked()) ndiNames.insert(m_ndiProgramNameEdit->text().trimmed());
    if(m_ndiCleanCheck->isChecked()) ndiNames.insert(m_ndiCleanNameEdit->text().trimmed());
    for(int row=0;row<m_destinationTable->rowCount();++row) {
        if(!qobject_cast<QCheckBox*>(m_destinationTable->cellWidget(row,0))->isChecked())continue;
        const QString name=m_destinationTable->item(row,1)->text().trimmed();
        if(name.isEmpty()||invalidCommandText(name))return tr("Output %1 needs a valid name.").arg(row+1);
        const int role=qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,4))->currentIndex();
        if(role&&auxRoles.contains(role))return tr("Each AUX role can be assigned to only one enabled output.");
        if(role)auxRoles.insert(role);
        if(qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,2))->currentData().toString()==QLatin1String("ndi")) {
            if(ndiNames.contains(name))return tr("NDI output names must be unique.");ndiNames.insert(name);
        }
    }
    for (int row = 0; row < m_stingerTable->rowCount(); ++row) {
        const int cut = qobject_cast<QSpinBox*>(m_stingerTable->cellWidget(row, 2))->value();
        const int length = qobject_cast<QSpinBox*>(m_stingerTable->cellWidget(row, 3))->value();
        if (cut >= length) return tr("Stinger %1: the cut frame must precede the end frame.").arg(row + 1);
    }
    return {};
}

bool SetupWorkspace::applyTo(Configuration* configuration) const
{
    Configuration candidate;
    candidate.fromJson(configuration->toJson());
    candidate.setConfigFilePath(configuration->configFilePath());
    if (!fillDraft(&candidate) || !candidate.save()) return false;
    return configuration->fromJson(candidate.toJson());
}

bool SetupWorkspace::fillDraft(Configuration* configuration) const
{
    if (!validationError().isEmpty()) return false;
    configuration->setCasparHost(m_hostEdit->text().trimmed());
    configuration->setCasparPort(m_portSpin->value());
    configuration->setPreviewChannel(m_previewChannelSpin->value());
    configuration->setProgramChannel(m_programChannelSpin->value());
    configuration->setMultiviewChannel(m_multiviewChannelSpin->value());
    configuration->setProgramOutputChannel(m_programOutputSpin->value());
    configuration->setNdiProgramEnabled(m_ndiProgramCheck->isChecked());
    configuration->setNdiProgramName(m_ndiProgramNameEdit->text());
    configuration->setNdiCleanEnabled(m_ndiCleanCheck->isChecked());
    configuration->setNdiCleanName(m_ndiCleanNameEdit->text());
    configuration->setDustMix(m_dustRatio->value(),m_dustSize->value(),m_dustFlash->value());
    configuration->setSuperMixGains(m_superGainA->value(),m_superGainB->value());
    configuration->setAutoDurationFrames(m_autoFramesSpin->value());
    configuration->setWipePatternId(m_wipePatternCombo->currentData().toString());
    configuration->setWipeDirectionMode(static_cast<WipeDirectionMode>(m_wipeDirectionCombo->currentData().toInt()));
    configuration->setWipeEdgeMode(static_cast<WipeEdgeMode>(m_wipeEdgeCombo->currentData().toInt()));
    configuration->setWipeEdgeAmount(m_wipeEdgeAmountSpin->value());
    configuration->setWipeBorderAmount(m_wipeBorderSpin->value());
    configuration->setWipeShadowAmount(m_wipeShadowSpin->value());
    configuration->setWipeAspect(m_wipeAspectCombo->value(), 100);
    configuration->setWipeMulti(m_wipeMultiCombo->currentData().toInt());
    for (int index = 0; index < 6; ++index) {
        const int source=m_dmeBackgroundCombos[index]->currentData().toInt();
        if(source==-2){configuration->setDmeBackgroundScope(kDmeBackgroundEffects[index],false);continue;}
        if(source==-3){if(!configuration->setDmeBackgroundImage(kDmeBackgroundEffects[index],m_dmeBackgroundImages[index]->text()))return false;}
        else configuration->setDmeBackground(kDmeBackgroundEffects[index],source);
    }
    configuration->setDipColor(m_dipColorButton->property("dipColor").toString());
    configuration->setWipeBorderColor(m_wipeColorButton->property("borderColor").toString());
    configuration->setPanelPort(m_panelPortSpin->value());
    configuration->setOscPort(m_oscPortSpin->value());
    configuration->setProgramLeft(m_programSideCombo->currentData().toInt() != 0);
    configuration->setNameEdge(m_nameEdgeCombo->currentData().toString());
    configuration->setNameAlign(m_nameAlignCombo->currentData().toString());
    configuration->setClockEdge(m_clockEdgeCombo->currentData().toString());
    configuration->setClockAlign(m_clockAlignCombo->currentData().toString());
    configuration->setSafePreviewAspect(m_safePreviewAspect->currentData().toString());
    configuration->setSafeProgramAspect(m_safeProgramAspect->currentData().toString());
    configuration->setSafePreset(m_safePreset->currentData().toString());
    configuration->setSafePreview(m_safePreviewCheck->isChecked());
    configuration->setSafeProgram(m_safeProgramCheck->isChecked());
    configuration->setDskSourceId(m_dskSourceCombo->currentData().toInt());

    QList<Source> sources;
    for (int row = 0; row < m_sourceTable->rowCount(); ++row) {
        Source src;
        src.id = row;
        src.name = m_sourceTable->item(row, 2)->text().trimmed();
        src.type = typeFromIndex(qobject_cast<QComboBox*>(m_sourceTable->cellWidget(row, 3))->currentIndex());
        src.casparChannel = qobject_cast<QSpinBox*>(m_sourceTable->cellWidget(row, 4))->value();
        src.argument = sourceArgument(qobject_cast<QLineEdit*>(m_sourceTable->cellWidget(row, 5)),src.type);
        src.enabled = qobject_cast<QCheckBox*>(m_sourceTable->cellWidget(row, 0))->isChecked();
        src.loop = qobject_cast<QCheckBox*>(m_sourceTable->cellWidget(row, 8))->isChecked();
        sources.append(src);
    }
    configuration->setSources(sources);
    if(!configuration->setSuperSources(m_superSourceEditor->layouts())||!configuration->superSourceGraphError().isEmpty())return false;
    QList<OutputDestination> destinations;
    for(int row=0;row<m_destinationTable->rowCount();++row) {
        OutputDestination out;out.id=row;out.name=m_destinationTable->item(row,1)->text().trimmed();
        out.enabled=qobject_cast<QCheckBox*>(m_destinationTable->cellWidget(row,0))->isChecked();
        out.type=qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,2))->currentData().toString();
        out.source=qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,3))->currentData().toInt();
        out.aux=qobject_cast<QComboBox*>(m_destinationTable->cellWidget(row,4))->currentIndex();
        out.device=qobject_cast<QSpinBox*>(m_destinationTable->cellWidget(row,5))->value();
        out.fullscreen=qobject_cast<QCheckBox*>(m_destinationTable->cellWidget(row,6))->isChecked();
        destinations.append(out);
    }
    if(!configuration->setDestinations(destinations))return false;
    configuration->setMeters(m_metersCheck->isChecked());
    for(int index=0;index<18;++index)configuration->setKeyProcessing(index<16?index/4:0,index<16?index%4:index-16,index>=16,m_processingDraft[index]);
    for (int index = 0; index < m_keySources.size(); ++index)
        configuration->setMeKeySource(index / 4, index % 4, m_keySources.at(index)->currentData().toInt());
    configuration->setDskSource(1, m_dsk2SourceCombo->currentData().toInt());
    QList<StingerSlot> entries;
    for (int row = 0; row < 10; ++row) {
        StingerSlot entry;
        entry.media = m_stingerTable->item(row, 0)->text().trimmed();
        entry.reverse = m_stingerTable->item(row, 1)->text().trimmed();
        entry.cutFrames = qobject_cast<QSpinBox*>(m_stingerTable->cellWidget(row, 2))->value();
        entry.lengthFrames = qobject_cast<QSpinBox*>(m_stingerTable->cellWidget(row, 3))->value();
        if (!entry.media.isEmpty() && entry.cutFrames >= entry.lengthFrames) return false;
        entries.append(entry);
    }
    if (!configuration->replaceStingers(entries)) return false;
    return true;
}

void SetupWorkspace::addDestinationRow(int row)
{
    auto* enabled=new QCheckBox(this); m_destinationTable->setCellWidget(row,0,enabled);
    m_destinationTable->setItem(row,1,new QTableWidgetItem(tr("Output %1").arg(row+1)));
    auto* type=new QComboBox(this); type->addItem(tr("NDI"),QStringLiteral("ndi"));type->addItem(tr("Screen"),QStringLiteral("screen"));
    m_destinationTable->setCellWidget(row,2,type);
    auto* source=new QComboBox(this);
    source->addItem(tr("Final program (DSK / FTB)"),1201);source->addItem(tr("Multiview"),1200);
    for(int me=0;me<4;++me) {source->addItem(tr("M/E %1 program (clean)").arg(me+1),1000+me);source->addItem(tr("M/E %1 preview").arg(me+1),1100+me);}
    for(const auto& input:m_config->sources())if(input.id!=11&&input.id!=23)source->addItem(tr("Input %1 — %2").arg(input.id+1).arg(input.name),input.id);
    m_destinationTable->setCellWidget(row,3,source);
    auto* role=new QComboBox(this);role->addItem(tr("Fixed"));for(int aux=1;aux<=4;++aux)role->addItem(tr("AUX %1").arg(aux));m_destinationTable->setCellWidget(row,4,role);
    auto* device=new QSpinBox(this);device->setRange(0,32);m_destinationTable->setCellWidget(row,5,device);
    auto* fullscreen=new QCheckBox(this);fullscreen->setChecked(true);m_destinationTable->setCellWidget(row,6,fullscreen);
    connect(enabled,&QCheckBox::toggled,this,&SetupWorkspace::markDirty);
    connect(type,&QComboBox::currentIndexChanged,this,[this,type,device,fullscreen](){const bool screen=type->currentData()==QVariant(QStringLiteral("screen"));device->setEnabled(screen);fullscreen->setEnabled(screen);markDirty();});
    device->setEnabled(false);fullscreen->setEnabled(false);
    connect(source,&QComboBox::currentIndexChanged,this,&SetupWorkspace::markDirty);
    connect(role,&QComboBox::currentIndexChanged,this,&SetupWorkspace::markDirty);
    connect(device,&QSpinBox::valueChanged,this,&SetupWorkspace::markDirty);
    connect(fullscreen,&QCheckBox::toggled,this,&SetupWorkspace::markDirty);
}

void SetupWorkspace::editKeyProcessing(int index)
{
    if(index<0||index>=18)return;
    QDialog dialog(this);dialog.setWindowTitle(index<16?tr("M/E %1 · Key %2 processing").arg(index/4+1).arg(index%4+1):tr("DSK %1 processing").arg(index-15));
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    const auto original=m_processingDraft[index];
    auto* mode=new QComboBox(&dialog);mode->addItem(tr("LINEAR — embedded alpha"),"linear");mode->addItem(tr("CHR — chroma key"),"chroma");mode->addItem(tr("LUMA — luminance key"),"luma");mode->setObjectName("key-mode");mode->setCurrentIndex(mode->findData(original.mode));form->addRow(tr("Key type"),mode);
    auto* mask=new QCheckBox(tr("Enable rectangular MAIN MASK"),&dialog);mask->setObjectName("key-mask");mask->setChecked(original.mask);form->addRow(mask);
    auto* invert=new QCheckBox(tr("Invert key alpha (KEY INV)"),&dialog);invert->setObjectName("key-invert");invert->setChecked(original.invert);form->addRow(invert);
    auto* maskInvert=new QCheckBox(tr("Invert MAIN MASK"),&dialog);maskInvert->setObjectName("mask-invert");maskInvert->setChecked(original.maskInvert);form->addRow(maskInvert);
    QMap<QString,QDoubleSpinBox*> fields;
    const auto values=original.toJson();
    for(const QString& name:{QStringLiteral("lumaLow"),QStringLiteral("lumaHigh"),QStringLiteral("left"),QStringLiteral("top"),QStringLiteral("right"),QStringLiteral("bottom"),QStringLiteral("hue"),QStringLiteral("width"),QStringLiteral("saturation"),QStringLiteral("brightness"),QStringLiteral("softness"),QStringLiteral("spill"),QStringLiteral("spillSaturation")}) {
        auto* spin=new QDoubleSpinBox(&dialog);spin->setDecimals(4);spin->setRange(0,name=="hue"?360:name=="spill"?180:1);spin->setSingleStep(name=="hue"||name=="spill"?1:0.01);spin->setValue(values.value(name).toDouble());fields[name]=spin;
        const QMap<QString,QString> labels{{"lumaLow",tr("Luma black threshold (0–1)")},{"lumaHigh",tr("Luma white threshold (0–1)")},{"left",tr("Mask left (0–1)")},{"top",tr("Mask top (0–1)")},{"right",tr("Mask right (0–1)")},{"bottom",tr("Mask bottom (0–1)")},{"hue",tr("Target hue (degrees)")},{"width",tr("Hue width (0–1)")},{"saturation",tr("Minimum saturation (0–1)")},{"brightness",tr("Minimum brightness (0–1)")},{"softness",tr("Chroma softness (0–1)")},{"spill",tr("Spill suppression (degrees)")},{"spillSaturation",tr("Spill saturation (0–1)")}};
        form->addRow(labels.value(name),spin);
    }
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    auto enabled=[&] {
        for(auto it=fields.begin();it!=fields.end();++it) {
            const bool geometry=it.key()=="left"||it.key()=="right"||it.key()=="top"||it.key()=="bottom";
            it.value()->setEnabled(geometry?mask->isChecked():it.key().startsWith("luma")?mode->currentData()==QVariant("luma"):mode->currentData()==QVariant("chroma"));
        }
        maskInvert->setEnabled(mask->isChecked());
    };connect(mode,&QComboBox::currentIndexChanged,&dialog,enabled);connect(mask,&QCheckBox::toggled,&dialog,enabled);enabled();
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QJsonObject patch{{"mode",mode->currentData().toString()},{"mask",mask->isChecked()},{"invert",invert->isChecked()},{"maskInvert",maskInvert->isChecked()}};
        for(auto it=fields.begin();it!=fields.end();++it)patch.insert(it.key(),it.value()->value());
        auto updated=original;QString error;
        if(!updated.update(patch,&error)){QMessageBox::warning(&dialog,tr("Invalid key settings"),tr("Mask left/top must precede right/bottom; luma black threshold must be below white threshold."));return;}
        if(updated.toJson()!=original.toJson()){m_processingDraft[index]=updated;markDirty();}
        dialog.accept();
    });dialog.exec();
}
