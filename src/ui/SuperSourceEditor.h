#pragma once
#include "core/SuperSource.h"
#include <QtCore/QSet>
#include <QtWidgets/QWidget>
class Configuration;
class QComboBox;
class QListWidget;
class QGraphicsView;
class QGraphicsScene;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QAction;
class SuperSourceEditor : public QWidget {
    Q_OBJECT
  public:
    explicit SuperSourceEditor(Configuration *, QWidget *parent = nullptr);
    ~SuperSourceEditor() override;
    QList<SuperSourceLayout> layouts() const { return data; }
    void setLayouts(const QList<SuperSourceLayout> &);
    void selectBoxes(const QList<int> &);
    void duplicateSelection();
    void copySelection();
    void pasteBoxes();
    void alignSelection(const QString &mode, bool toFrame = false);
    void distributeSelection(bool horizontal);
    void arrangeGrid(int columns, double gap = .02);
    void undo();
    void redo();
  signals:
    void changed();

  private:
    void rebuildLayouts();
    void rebuildCanvas();
    void selectBox();
    void editBox();
    void addLayout();
    void addBox(bool image);
    void removeSelection();
    void moveSelection(int delta);
    void commitCanvas();
    void recordHistory();
    void restoreHistory(int cursor);
    QList<int> selectedRows() const;
    QList<SuperSourceBox> copiedBoxes() const;
    void insertBoxes(QList<SuperSourceBox>);
    QSet<int> selectedSet;
    QList<QJsonArray> history;
    int historyCursor = -1;
    bool restoringHistory = false;
    QAction *undoAction = nullptr, *redoAction = nullptr;
    QCheckBox *aspectLock, *snapEnabled;
    QComboBox *keyButton;
    double snapStep = .025;
    QList<SuperSourceLayout> data;
    Configuration *config;
    QComboBox *layoutSelect, *input, *fit;
    QListWidget *layers;
    QGraphicsScene *scene;
    QGraphicsView *view;
    QLineEdit *name, *imagePath;
    QDoubleSpinBox *imageAspect;
    QCheckBox *audio;
    QDoubleSpinBox *geometry[4], *crop[4], *zoom, *centerX, *centerY,*corners[8];
    QCheckBox* perspectiveEditing=nullptr;
    bool canvasRefreshPending = false;
    bool updating = false;
    int selected = -1;
};
