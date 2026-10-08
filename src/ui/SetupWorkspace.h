#pragma once

#include <QtWidgets/QWidget>
#include "core/KeyProcessing.h"
#include <array>

class Configuration;
class SuperSourceEditor;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QComboBox;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;

class SetupWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit SetupWorkspace(Configuration* configuration, QWidget* parent = nullptr);

    void reload();
    void setInputState(int id, const QString& text);
    void appendDiagnostic(const QString& message);
    bool isDirty() const { return m_dirty; }

signals:
    void saveRequested();
    void dirtyChanged(bool dirty);

public:
    QString validationError() const;
    bool applyTo(Configuration* configuration) const;
    bool fillDraft(Configuration* configuration) const;

private slots:
    void onTypeChanged();
    void onBrowseArgument();

private:
    void populate();
    void addSourceRow(int row);
    void updateArgumentPrompt(int row);

    void markDirty();
    QPlainTextEdit* m_diagnostics = nullptr;
    bool m_dirty = false;
    bool m_loading = false;
    QTableWidget* m_stingerTable = nullptr;
    QList<QComboBox*> m_keySources;
    std::array<KeyProcessing,18> m_processingDraft{};
    void editKeyProcessing(int index);
    QComboBox* m_dsk2SourceCombo = nullptr;
    QCheckBox* m_metersCheck = nullptr;
    Configuration* m_config = nullptr;
    SuperSourceEditor* m_superSourceEditor = nullptr;
    QLineEdit* m_hostEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QSpinBox* m_previewChannelSpin = nullptr;
    QSpinBox* m_programChannelSpin = nullptr;
    QSpinBox* m_multiviewChannelSpin = nullptr;
    QSpinBox* m_programOutputSpin = nullptr;
    QCheckBox* m_ndiProgramCheck = nullptr;
    QLineEdit* m_ndiProgramNameEdit = nullptr;
    QCheckBox* m_ndiCleanCheck = nullptr;
    QLineEdit* m_ndiCleanNameEdit = nullptr;
    QComboBox* m_borderSide=nullptr;QSpinBox* m_innerSoft=nullptr;QSpinBox* m_outerSoft=nullptr;
    QSpinBox* m_dustRatio=nullptr;QSpinBox* m_dustSize=nullptr;QSpinBox* m_dustFlash=nullptr;
    QSpinBox* m_superGainA=nullptr;QSpinBox* m_superGainB=nullptr;
    QSpinBox* m_autoFramesSpin = nullptr;
    QComboBox* m_wipePatternCombo = nullptr;
    QComboBox* m_wipeDirectionCombo = nullptr;
    QComboBox* m_wipeEdgeCombo = nullptr;
    QSpinBox* m_wipeEdgeAmountSpin = nullptr;
    QSpinBox* m_wipeBorderSpin = nullptr;
    QSpinBox* m_wipeShadowSpin = nullptr;
    QSpinBox* m_wipeAspectCombo = nullptr;
    QComboBox* m_wipeMultiCombo = nullptr;
    std::array<QComboBox*,6> m_dmeBackgroundCombos{};
    std::array<QLineEdit*,6> m_dmeBackgroundImages{};
    QPushButton* m_wipeColorButton = nullptr;
    QPushButton* m_dipColorButton = nullptr;
    QSpinBox* m_panelPortSpin = nullptr;
    QSpinBox* m_oscPortSpin = nullptr;
    QComboBox* m_programSideCombo = nullptr;
    QComboBox* m_nameEdgeCombo = nullptr;
    QComboBox* m_nameAlignCombo = nullptr;
    QComboBox* m_clockEdgeCombo = nullptr;
    QComboBox* m_clockAlignCombo = nullptr;
    QComboBox* m_safePreviewAspect = nullptr;
    QComboBox* m_safeProgramAspect = nullptr;
    QComboBox* m_safePreset = nullptr;
    QCheckBox* m_safePreviewCheck = nullptr;
    QCheckBox* m_safeProgramCheck = nullptr;
    QComboBox* m_dskSourceCombo = nullptr;
    void addDestinationRow(int row);
    QTableWidget* m_destinationTable = nullptr;
    QTableWidget* m_sourceTable = nullptr;
};
