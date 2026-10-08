#include "SuperSourceEditor.h"
#include "config/Configuration.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QClipboard>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>
#include <QtGui/QShortcut>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGraphicsRectItem>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsSceneMouseEvent>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
class CanvasView : public QGraphicsView {
  public:
    CanvasView(QGraphicsScene *s, QWidget *parent) : QGraphicsView(s, parent) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    }
    void showEvent(QShowEvent *event) override {
        QGraphicsView::showEvent(event);
        fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    }
};
class BoxItem : public QGraphicsRectItem {
  public:
    QString label;
    QPixmap image;
    SuperSourceBox box;
    std::function<void(QRectF)> changed;
    bool resizing = false, noSnap = false,warpEditing=false;int cornerDrag=-1;
    QPolygonF quad()const{QPolygonF p;for(auto point:box.corners)p<<QPointF(point.x()*rect().width(),point.y()*rect().height());return p;}
    QRectF boundingRect()const override{return QGraphicsRectItem::boundingRect().united(quad().boundingRect()).adjusted(-8,-8,8,8);}
    QMap<BoxItem *, QPointF> dragStarts;
    QPointF dragCursor;
    QRectF dragBounds;
    double resizeAspect = 1;
    std::function<QPointF(QPointF)> snap;
    explicit BoxItem(QRectF r) : QGraphicsRectItem(0, 0, r.width(), r.height()) {
        setPos(r.topLeft());
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setBrush(Qt::NoBrush);
        setPen(QPen(QColor("#50bedf"), 2));
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *widget) override {
        QGraphicsRectItem::paint(p, opt, widget);
        p->save();QTransform mapping;QPolygonF original;original<<rect().topLeft()<<rect().topRight()<<rect().bottomRight()<<rect().bottomLeft();if(QTransform::quadToQuad(original,quad(),mapping))p->setTransform(mapping,true);
        p->setClipRect(rect());
        auto frame = scene()->sceneRect();
        auto model = box;
        model.rect = {pos().x() / frame.width(), pos().y() / frame.height(), rect().width() / frame.width(),
                      rect().height() / frame.height()};
        auto fill = superSourceFill(model, frame.width() / frame.height());
        QRectF display(fill.x() * frame.width() - pos().x(), fill.y() * frame.height() - pos().y(),
                       fill.width() * frame.width(), fill.height() * frame.height());
        p->save();
        p->setClipRect(QRectF{display.x() + display.width() * box.crop.x(),
                              display.y() + display.height() * box.crop.y(),
                              display.width() * box.crop.width(), display.height() * box.crop.height()},
                       Qt::IntersectClip);
        if (!image.isNull())
            p->drawPixmap(display.toRect(), image);
        else
            p->fillRect(display, QColor(30, 105, 150, 180));
        p->restore();
        p->setPen(Qt::white);
        p->drawText(rect().adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::TextWordWrap, label);
        if (isSelected()) {
            p->setPen(QPen(QColor("#ffd56a"), 3));
            p->drawRect(rect());
            p->fillRect(QRectF(rect().right() - 12, rect().bottom() - 12, 12, 12), QColor("#ffd56a"));
        }
        p->restore();
        if(warpEditing&&isSelected()){p->setPen(QPen(QColor("#e76bbd"),2));p->setBrush(Qt::NoBrush);p->drawPolygon(quad());p->setBrush(QColor("#e76bbd"));for(auto point:quad())p->drawEllipse(point,5,5);}
    }
    QVariant itemChange(GraphicsItemChange type, const QVariant &value) override {
        if (type == ItemPositionChange && scene()) {
            auto pos = value.toPointF();
            if (snap && !noSnap)
                pos = snap(pos);
            auto frame = scene()->sceneRect();
            pos.setX(qBound(0., pos.x(), qMax(0., frame.width() - rect().width())));
            pos.setY(qBound(0., pos.y(), qMax(0., frame.height() - rect().height())));
            return pos;
        }
        return QGraphicsRectItem::itemChange(type, value);
    }
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if(warpEditing){auto points=quad();for(int i=0;i<4;++i)if(QLineF(e->pos(),points[i]).length()<12){cornerDrag=i;setSelected(true);e->accept();return;}}
        resizing = QRectF(rect().right() - 16, rect().bottom() - 16, 16, 16).contains(e->pos());
        resizeAspect = rect().width() / rect().height();
        if (resizing) {
            setSelected(true);
            e->accept();
        } else {
            QGraphicsRectItem::mousePressEvent(e);
            dragCursor = e->scenePos();
            dragStarts.clear();
            dragBounds = {};
            for (auto *item : scene()->selectedItems())
                if (auto *b = dynamic_cast<BoxItem *>(item)) {
                    dragStarts[b] = b->pos();
                    auto r = QRectF(b->pos(), b->rect().size());
                    dragBounds = dragBounds.isNull() ? r : dragBounds.united(r);
                }
        }
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override {
        if(cornerDrag>=0){auto next=box.corners;next[cornerDrag]={qBound(-2.,e->pos().x()/rect().width(),3.),qBound(-2.,e->pos().y()/rect().height(),3.)};if(validSuperSourceCorners(next)){prepareGeometryChange();box.corners=next;update();}e->accept();return;}
        if (resizing) {
            auto frame = scene()->sceneRect();
            double w = qMax(1., e->pos().x()), h = qMax(1., e->pos().y());
            if (box.keepAspect || (e->modifiers() & Qt::ShiftModifier)) {
                h = w / resizeAspect;
                double factor =
                    qMin(1., qMin((frame.width() - pos().x()) / w, (frame.height() - pos().y()) / h));
                w *= factor;
                h *= factor;
            } else {
                w = qMin(w, frame.width() - pos().x());
                h = qMin(h, frame.height() - pos().y());
            }
            setRect(0, 0, w, h);
            e->accept();
        } else if (!dragStarts.isEmpty()) {
            auto delta = e->scenePos() - dragCursor;
            if (snap) {
                auto start = dragStarts.value(this, pos());
                delta = snap(start + delta) - start;
            }
            auto frame = scene()->sceneRect();
            delta.setX(qBound(-dragBounds.left(), delta.x(), frame.width() - dragBounds.right()));
            delta.setY(qBound(-dragBounds.top(), delta.y(), frame.height() - dragBounds.bottom()));
            for (auto i = dragStarts.begin(); i != dragStarts.end(); ++i) {
                i.key()->noSnap = true;
                i.key()->setPos(i.value() + delta);
                i.key()->noSnap = false;
            }
            e->accept();
        } else
            QGraphicsRectItem::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override {
        if(cornerDrag>=0){cornerDrag=-1;if(changed)changed(rect());e->accept();return;}
        QGraphicsRectItem::mouseReleaseEvent(e);
        resizing = false;
        dragStarts.clear();
        if (changed)
            changed(QRectF(pos(), rect().size()));
    }
};
} // namespace
SuperSourceEditor::SuperSourceEditor(Configuration *c, QWidget *parent) : QWidget(parent), config(c) {
    setObjectName("superSourceEditor");
    auto root = new QVBoxLayout(this);
    auto tools = new QHBoxLayout;
    layoutSelect = new QComboBox(this);
    layoutSelect->setObjectName("superSourceLayout");
    tools->addWidget(layoutSelect, 1);
    auto button = [&](QString title, auto action) {
        auto b = new QPushButton(title, this);
        tools->addWidget(b);
        connect(b, &QPushButton::clicked, this, action);
    };
    button(tr("New layout"), [this] { addLayout(); });
    button(tr("Rename"), [this] {
        int i = layoutSelect->currentIndex();
        if (i < 0)
            return;
        bool ok = false;
        auto text =
            QInputDialog::getText(this, tr("Layout name"), tr("Name"), QLineEdit::Normal, data[i].name, &ok);
        if (ok && !text.trimmed().isEmpty()) {
            data[i].name = text.trimmed();
            rebuildLayouts();
            emit changed();
        }
    });
    button(tr("Duplicate"), [this] {
        int i = layoutSelect->currentIndex();
        if (i < 0 || data.size() >= 24)
            return;
        auto copy = data[i];
        copy.id = uuid();
        copy.name += tr(" copy");
        data.append(copy);
        rebuildLayouts();
        layoutSelect->setCurrentIndex(data.size() - 1);
        emit changed();
    });
    button(tr("Delete layout"), [this] {
        int i = layoutSelect->currentIndex();
        if (i < 0)
            return;
        data.removeAt(i);
        rebuildLayouts();
        emit changed();
    });
    root->addLayout(tools);
    auto editing = new QHBoxLayout;
    auto editButton = [&](QString text, auto slot) {
        auto b = new QPushButton(text, this);
        editing->addWidget(b);
        connect(b, &QPushButton::clicked, this, slot);
    };
    editButton(tr("Duplicate boxes"), [this] { duplicateSelection(); });
    auto clipboard = new QToolButton(this);
    clipboard->setText(tr("Clipboard"));
    clipboard->setPopupMode(QToolButton::InstantPopup);
    auto clipMenu = new QMenu(clipboard);
    clipMenu->addAction(tr("Copy"), this, &SuperSourceEditor::copySelection);
    clipMenu->addAction(tr("Cut"), this, [this] {
        copySelection();
        removeSelection();
    });
    clipMenu->addAction(tr("Paste"), this, &SuperSourceEditor::pasteBoxes);
    clipboard->setMenu(clipMenu);
    editing->addWidget(clipboard);
    auto align = new QToolButton(this);
    align->setText(tr("Align"));
    align->setPopupMode(QToolButton::InstantPopup);
    auto alignMenu = new QMenu(align);
    auto frameAction = alignMenu->addAction(tr("Align to frame"));
    frameAction->setCheckable(true);
    alignMenu->addSeparator();
    for (auto entry : QList<QPair<QString, QString>>{{"Left", "left"},
                                                     {"Horizontal centre", "hcenter"},
                                                     {"Right", "right"},
                                                     {"Top", "top"},
                                                     {"Vertical centre", "vcenter"},
                                                     {"Bottom", "bottom"},
                                                     {"Match width", "width"},
                                                     {"Match height", "height"}})
        alignMenu->addAction(entry.first, this, [this, entry, frameAction] {
            alignSelection(entry.second, frameAction->isChecked());
        });
    align->setMenu(alignMenu);
    editing->addWidget(align);
    auto arrange = new QToolButton(this);
    arrange->setText(tr("Arrange"));
    arrange->setPopupMode(QToolButton::InstantPopup);
    auto arrangeMenu = new QMenu(arrange);
    arrangeMenu->addAction(tr("Distribute horizontally"), this, [this] { distributeSelection(true); });
    arrangeMenu->addAction(tr("Distribute vertically"), this, [this] { distributeSelection(false); });
    arrangeMenu->addAction(tr("Automatic grid"), this, [this] {
        auto rows = selectedRows();
        if (rows.isEmpty())
            return;
        bool ok = false;
        int cols =
            QInputDialog::getInt(this, tr("Automatic grid"), tr("Columns"),
                                 qMax(1, int(std::ceil(std::sqrt(rows.size())))), 1, rows.size(), 1, &ok);
        if (ok)
            arrangeGrid(cols);
    });
    arrange->setMenu(arrangeMenu);
    editing->addWidget(arrange);
    snapEnabled = new QCheckBox(tr("Snap"), this);
    snapEnabled->setChecked(true);
    editing->addWidget(snapEnabled);
    auto step = new QDoubleSpinBox(this);
    step->setRange(.1, 10);
    step->setValue(2.5);
    step->setSuffix(" %");
    step->setToolTip(tr("Drag snap grid"));
    editing->addWidget(step);
    connect(step, &QDoubleSpinBox::valueChanged, this, [this](double value) { snapStep = value / 100; });
    auto undoButton = new QToolButton(this);
    undoAction = new QAction(tr("Undo"), this);
    undoButton->setDefaultAction(undoAction);
    connect(undoAction, &QAction::triggered, this, &SuperSourceEditor::undo);
    editing->addStretch();editing->addWidget(undoButton);
    auto redoButton = new QToolButton(this);
    redoAction = new QAction(tr("Redo"), this);
    redoButton->setDefaultAction(redoAction);
    connect(redoAction, &QAction::triggered, this, &SuperSourceEditor::redo);
    editing->addWidget(redoButton);
    root->addLayout(editing);

    auto content = new QHBoxLayout;
    scene = new QGraphicsScene(this);
    scene->setSceneRect(0, 0, 960, 960. * c->videoHeight() / c->videoWidth());
    scene->setBackgroundBrush(Qt::black);
    view = new CanvasView(scene, this);
    view->setObjectName("superSourceCanvas");
    view->setMinimumSize(480, 310);
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    content->addWidget(view, 1);
    auto properties = new QWidget(this);
    auto form = new QFormLayout(properties);
    layers = new QListWidget(this);
    layers->setObjectName("superSourceLayers");
    layers->setMaximumHeight(130);
    layers->setSelectionMode(QAbstractItemView::ExtendedSelection);
    form->addRow(tr("Layers (back to front)"), layers);
    auto layerTools = new QHBoxLayout;
    auto layerButton = [&](QString title, auto action) {
        auto b = new QPushButton(title, this);
        layerTools->addWidget(b);
        connect(b, &QPushButton::clicked, this, action);
    };
    layerButton(tr("Input"), [this] { addBox(false); });
    layerButton(tr("Image"), [this] { addBox(true); });
    layerButton(tr("Remove"), [this] { removeSelection(); });
    form->addRow(layerTools);
    auto order = new QHBoxLayout;
    auto reorder = [&](QString title, int delta) {
        auto b = new QPushButton(title, this);
        order->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, delta] { moveSelection(delta); });
    };
    reorder(tr("Move back"), -1);
    reorder(tr("Move front"), 1);
    form->addRow(order);
    name = new QLineEdit(this);
    form->addRow(tr("Box name"), name);
    connect(name, &QLineEdit::editingFinished, this, &SuperSourceEditor::editBox);
    input = new QComboBox(this);
    input->addItem(tr("Unbound"), -1);
    for (auto &source : c->sources())
        if (source.id != 11 && source.id != 23)
            input->addItem(QString("%1 - %2").arg(source.id + 1).arg(source.name), source.id);
    form->addRow(tr("Default input"), input);
    connect(input, &QComboBox::currentIndexChanged, this, &SuperSourceEditor::editBox);
    auto number = [&](QString title, double min, double max, double value) {
        auto s = new QDoubleSpinBox(this);
        s->setDecimals(1);
        s->setRange(min, max);
        s->setValue(value);
        s->setSuffix(" %");
        form->addRow(title, s);
        connect(s, &QDoubleSpinBox::valueChanged, this, &SuperSourceEditor::editBox);
        return s;
    };
    QStringList geo{tr("Left"), tr("Top"), tr("Width"), tr("Height")};
    for (int i = 0; i < 4; ++i) {
        geometry[i] = number(geo[i], i < 2 ? 0 : .1, i < 2 ? 99.9 : 100, i < 2 ? 0 : 100);
        geometry[i]->setObjectName(QString("superSourceGeometry%1").arg(i));
    }
    perspectiveEditing=new QCheckBox(tr("Edit perspective corners on canvas"),this);perspectiveEditing->setObjectName("superSourcePerspectiveEditing");form->addRow(perspectiveEditing);
    connect(perspectiveEditing,&QCheckBox::toggled,this,[this]{rebuildCanvas();});
    const QStringList cornerNames={tr("Top left"),tr("Top right"),tr("Bottom right"),tr("Bottom left")};
    for(int i=0;i<8;++i){corners[i]=number(cornerNames[i/2]+(i%2?tr(" Y"):tr(" X")),-200,300,(i==2||i==4||i==5||i==7)?100:0);corners[i]->setObjectName(QString("superSourceCorner%1").arg(i));}
    auto* resetPerspective=new QPushButton(tr("Reset perspective"),this);resetPerspective->setObjectName("superSourcePerspectiveReset");form->addRow(resetPerspective);
    connect(resetPerspective,&QPushButton::clicked,this,[this]{int l=layoutSelect->currentIndex();if(l<0||selected<0)return;data[l].boxes[selected].corners={{{0,0},{1,0},{1,1},{0,1}}};rebuildCanvas();emit changed();});
    aspectLock = new QCheckBox(tr("Keep box proportions"), this);
    form->addRow(aspectLock);
    connect(aspectLock, &QCheckBox::toggled, this, &SuperSourceEditor::editBox);
    keyButton = new QComboBox(this);
    keyButton->addItem(tr("No KEY bus button"), -1);
    for (int i = 0; i < 24; ++i)
        keyButton->addItem(i < 12 ? tr("KEY %1").arg(i + 1) : tr("SHIFT + KEY %1").arg(i - 11), i);
    form->addRow(tr("Window selection"), keyButton);
    connect(keyButton, &QComboBox::currentIndexChanged, this, &SuperSourceEditor::editBox);
    fit = new QComboBox(this);
    fit->addItems({tr("Cover (crop to box)"), tr("Contain (fit inside box)")});
    form->addRow(tr("Fit"), fit);
    connect(fit, &QComboBox::currentIndexChanged, this, &SuperSourceEditor::editBox);
    zoom = number(tr("Zoom"), 10, 1000, 100);
    centerX = number(tr("Image centre X"), 0, 100, 50);
    centerY = number(tr("Image centre Y"), 0, 100, 50);
    for (int i = 0; i < 4; ++i)
        crop[i] = number(tr("Crop ") + geo[i], i < 2 ? 0 : .1, i < 2 ? 99.9 : 100, i < 2 ? 0 : 100);
    imagePath = new QLineEdit(this);
    form->addRow(tr("Image path"), imagePath);
    connect(imagePath, &QLineEdit::editingFinished, this, &SuperSourceEditor::editBox);
    imageAspect = new QDoubleSpinBox(this);
    imageAspect->setRange(.01, 100);
    imageAspect->setDecimals(3);
    form->addRow(tr("Image width / height"), imageAspect);
    connect(imageAspect, &QDoubleSpinBox::valueChanged, this, &SuperSourceEditor::editBox);
    audio = new QCheckBox(tr("Include this input's audio"), this);
    form->addRow(audio);
    connect(audio, &QCheckBox::toggled, this, &SuperSourceEditor::editBox);
    content->addWidget(properties);
    root->addLayout(content);
    auto bottom = new QHBoxLayout;
    auto background = new QPushButton(tr("Background colour"), this);
    bottom->addWidget(background);
    auto transparent = new QPushButton(tr("Transparent background"), this);
    bottom->addWidget(transparent);
    connect(background, &QPushButton::clicked, this, [this] {
        int l = layoutSelect->currentIndex();
        if (l < 0)
            return;
        auto colour = QColorDialog::getColor(QColor(data[l].background), this, tr("Background"));
        if (colour.isValid()) {
            data[l].background = colour.name();
            rebuildCanvas();
            emit changed();
        }
    });
    connect(transparent, &QPushButton::clicked, this, [this] {
        int l = layoutSelect->currentIndex();
        if (l >= 0) {
            data[l].background = "transparent";
            rebuildCanvas();
            emit changed();
        }
    });
    bottom->addStretch();
    root->addLayout(bottom);
    auto hint = new QLabel(tr("Ctrl-click selects multiple boxes. Drag to move; resize with the lower-right "
                              "handle (Shift preserves proportions). The canvas shows layout "
                              "geometry, not live video. Changes remain a draft until Save and Apply. "
                              "Bindings can also be changed through the panel API."),
                           this);
    hint->setWordWrap(true);
    root->addWidget(hint);
    connect(layoutSelect, &QComboBox::currentIndexChanged, this, [this] {
        if (!updating) {
            selected = -1;
            selectedSet.clear();
            rebuildCanvas();
        }
    });
    connect(layers, &QListWidget::itemSelectionChanged, this, [this] {
        if (updating)
            return;
        selectedSet.clear();
        for (int i = 0; i < layers->count(); ++i)
            if (layers->item(i)->isSelected())
                selectedSet.insert(i);
        selected = layers->currentRow();
        updating = true;
        for (auto *item : scene->items())
            if (item->data(0).isValid())
                item->setSelected(selectedSet.contains(item->data(0).toInt()));
        updating = false;
        selectBox();
    });
    connect(layers, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!updating) {
            selected = row;
            selectBox();
        }
    });
    connect(scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (updating)
            return;
        selectedSet.clear();
        for (auto *item : scene->selectedItems())
            if (item->data(0).isValid())
                selectedSet.insert(item->data(0).toInt());
        if (!selectedSet.contains(selected))
            selected = selectedSet.isEmpty() ? -1 : *selectedSet.begin();
        updating = true;
        for (int i = 0; i < layers->count(); ++i)
            layers->item(i)->setSelected(selectedSet.contains(i));
        layers->setCurrentRow(selected, QItemSelectionModel::NoUpdate);
        updating = false;
        selectBox();
    });
    for (auto *widget : QList<QWidget *>{view, layers}) {
        auto shortcut = [&](QKeySequence key, auto slot) {
            auto s = new QShortcut(key, widget);
            s->setContext(Qt::WidgetWithChildrenShortcut);
            connect(s, &QShortcut::activated, this, slot);
        };
        shortcut(QKeySequence::Copy, [this] { copySelection(); });
        shortcut(QKeySequence::Paste, [this] { pasteBoxes(); });
        shortcut(QKeySequence::Cut, [this] {
            copySelection();
            removeSelection();
        });
        shortcut(QKeySequence::Undo, [this] { undo(); });
        shortcut(QKeySequence::Redo, [this] { redo(); });
        shortcut(QKeySequence(Qt::Key_Delete), [this] { removeSelection(); });
    }
    connect(this, &SuperSourceEditor::changed, this, &SuperSourceEditor::recordHistory);
    setLayouts(c->superSources());
}
SuperSourceEditor::~SuperSourceEditor() {
    updating = true;
    for (auto *child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
}
void SuperSourceEditor::setLayouts(const QList<SuperSourceLayout> &l) {
    updating = true;
    input->clear();
    input->addItem(tr("Unbound"), -1);
    for (auto &source : config->sources())
        if (source.id != 11 && source.id != 23)
            input->addItem(QString("%1 - %2").arg(source.id + 1).arg(source.name), source.id);
    updating = false;
    data = l;
    selected = -1;
    selectedSet.clear();
    rebuildLayouts();
    history = {superSourcesJson(data)};
    historyCursor = 0;
    undoAction->setEnabled(false);
    redoAction->setEnabled(false);
}
void SuperSourceEditor::rebuildLayouts() {
    int index = layoutSelect->currentIndex();
    updating = true;
    layoutSelect->clear();
    for (auto &l : data)
        layoutSelect->addItem(l.name, l.id);
    layoutSelect->setCurrentIndex(data.isEmpty() ? -1 : qBound(0, index, int(data.size()) - 1));
    updating = false;
    rebuildCanvas();
}
void SuperSourceEditor::addLayout() {
    if (data.size() >= 24)
        return;
    bool ok = false;
    auto title = QInputDialog::getText(this, tr("New SuperSource"), tr("Name"), QLineEdit::Normal,
                                       tr("SuperSource %1").arg(data.size() + 1), &ok);
    if (!ok || title.trimmed().isEmpty())
        return;
    SuperSourceLayout layout;
    layout.id = uuid();
    layout.name = title.trimmed();
    data.append(layout);
    rebuildLayouts();
    layoutSelect->setCurrentIndex(data.size() - 1);
    emit changed();
}
void SuperSourceEditor::addBox(bool image) {
    int l = layoutSelect->currentIndex();
    if (l < 0 || data[l].boxes.size() >= 32)
        return;
    SuperSourceBox box;
    box.id = uuid();
    box.name = tr("Box %1").arg(data[l].boxes.size() + 1);
    box.rect = {.1, .1, .4, .4};
    box.audio = data[l].boxes.isEmpty() && !image;
    box.input = 0;
    if (image) {
        auto path = QFileDialog::getOpenFileName(
            this, tr("Static image"), {}, tr("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*)"));
        if (path.isEmpty())
            return;
        box.kind = "image";
        box.image = path;
        box.cover = false;
        auto size = QImageReader(path).size();
        if (size.isValid())
            box.imageAspect = double(size.width()) / size.height();
    }
    if (!image) {
        QSet<int> used;
        for (auto &b : data[l].boxes)
            if (b.keyButton >= 0)
                used.insert(b.keyButton);
        for (int button = 0; button < 24; ++button)
            if (!used.contains(button)) {
                box.keyButton = button;
                break;
            }
    }
    data[l].boxes.append(box);
    selected = data[l].boxes.size() - 1;
    selectedSet = {selected};
    rebuildCanvas();
    emit changed();
}
void SuperSourceEditor::rebuildCanvas() {
    updating = true;
    if (selectedSet.isEmpty() && selected >= 0)
        selectedSet.insert(selected);
    scene->clear();
    layers->clear();
    int l = layoutSelect->currentIndex();
    if (l >= 0) {
        auto &layout = data[l];
        scene->setBackgroundBrush(QColor("#20272f"));
        auto frame = scene->sceneRect();
        auto background = scene->addRect(
            frame, QPen(QColor("#748da3"), 1),
            QBrush(layout.background == "transparent" ? QColor("#304353") : QColor(layout.background)));
        background->setZValue(-1000);
        for (int i = 0; i < layout.boxes.size(); ++i) {
            auto &b = layout.boxes[i];
            layers->addItem(b.name);
            auto item = new BoxItem({b.rect.x() * frame.width(), b.rect.y() * frame.height(),
                                     b.rect.width() * frame.width(), b.rect.height() * frame.height()});
            item->box = b;item->warpEditing=perspectiveEditing->isChecked();
            item->setData(0, i);
            item->setZValue(i);
            item->label =
                b.name + (b.kind == "image" ? tr("\nStatic image") : tr("\nInput %1").arg(b.input + 1));
            if (b.kind == "image")
                item->image = QPixmap(b.image);
            scene->addItem(item);
            item->setSelected(selectedSet.contains(i));
            layers->item(i)->setSelected(selectedSet.contains(i));
            item->changed = [this](QRectF) { commitCanvas(); };
            item->snap = [this](QPointF pos) {
                if (!snapEnabled->isChecked())
                    return pos;
                auto frame = scene->sceneRect();
                double gx = frame.width() * snapStep, gy = frame.height() * snapStep;
                pos.setX(std::round(pos.x() / gx) * gx);
                pos.setY(std::round(pos.y() / gy) * gy);
                return pos;
            };
        }
    }
    layers->setCurrentRow(selected, QItemSelectionModel::NoUpdate);
    view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    updating = false;
    selectBox();
}
void SuperSourceEditor::selectBox() {
    updating = true;
    int l = layoutSelect->currentIndex();
    bool valid = l >= 0 && selected >= 0 && selected < data[l].boxes.size();
    for (auto *field : findChildren<QDoubleSpinBox *>())
        field->setEnabled(valid);
    name->setEnabled(valid);
    input->setEnabled(valid);
    audio->setEnabled(valid);
    fit->setEnabled(valid);
    aspectLock->setEnabled(valid);
    keyButton->setEnabled(valid);
    imagePath->setEnabled(valid);
    imageAspect->setEnabled(valid);
    if (valid) {
        auto &b = data[l].boxes[selected];
        for(int i=0;i<4;++i){corners[2*i]->setValue(b.corners[i].x()*100);corners[2*i+1]->setValue(b.corners[i].y()*100);}
        imagePath->setText(b.image);
        imageAspect->setValue(b.imageAspect);
        imagePath->setEnabled(b.kind == "image");
        imageAspect->setEnabled(b.kind == "image");
        aspectLock->setChecked(b.keepAspect);
        keyButton->setCurrentIndex(keyButton->findData(b.keyButton));
        keyButton->setEnabled(b.kind == "input");
        name->setText(b.name);
        input->setCurrentIndex(input->findData(b.input));
        input->setEnabled(b.kind == "input");
        audio->setEnabled(b.kind == "input");
        fit->setCurrentIndex(b.cover ? 0 : 1);
        double g[]{b.rect.x(), b.rect.y(), b.rect.width(), b.rect.height()},
            c[]{b.crop.x(), b.crop.y(), b.crop.width(), b.crop.height()};
        for (int i = 0; i < 4; ++i) {
            geometry[i]->setValue(g[i] * 100);
            crop[i]->setValue(c[i] * 100);
        }
        zoom->setValue(b.zoom * 100);
        centerX->setValue(b.centerX * 100);
        centerY->setValue(b.centerY * 100);
        audio->setChecked(b.audio);
    }
    updating = false;
}
void SuperSourceEditor::editBox() {
    if (updating)
        return;
    int l = layoutSelect->currentIndex();
    if (l < 0 || selected < 0 || selected >= data[l].boxes.size())
        return;
    auto &b = data[l].boxes[selected];
    std::array<QPointF,4> nextCorners;for(int i=0;i<4;++i)nextCorners[i]={corners[2*i]->value()/100,corners[2*i+1]->value()/100};if(!validSuperSourceCorners(nextCorners)){selectBox();return;}b.corners=nextCorners;
    const auto oldRect = b.rect;
    b.keepAspect = aspectLock->isChecked();
    b.keyButton = b.kind == "input" ? keyButton->currentData().toInt() : -1;
    b.name = name->text();
    b.image = imagePath->text();
    b.imageAspect = imageAspect->value();
    b.input = input->currentData().toInt();
    b.audio = audio->isChecked();
    b.cover = fit->currentIndex() == 0;
    b.zoom = zoom->value() / 100;
    b.centerX = centerX->value() / 100;
    b.centerY = centerY->value() / 100;
    auto rect = [](QDoubleSpinBox **p) {
        double x = p[0]->value() / 100, y = p[1]->value() / 100;
        return QRectF(x, y, qMin(p[2]->value() / 100, 1 - x), qMin(p[3]->value() / 100, 1 - y));
    };
    auto next = rect(geometry);
    if (b.keepAspect && oldRect.width() > 0 && oldRect.height() > 0) {
        const double aspect = oldRect.width() / oldRect.height();
        if (std::abs(next.width() - oldRect.width()) > 1e-8)
            next.setHeight(next.width() / aspect);
        else if (std::abs(next.height() - oldRect.height()) > 1e-8)
            next.setWidth(next.height() * aspect);
        double scale = qMin(1., qMin((1 - next.x()) / next.width(), (1 - next.y()) / next.height()));
        next.setSize(next.size() * scale);
    }
    b.rect = next;
    b.crop = rect(crop);
    if (!canvasRefreshPending) {
        canvasRefreshPending = true;
        QTimer::singleShot(0, this, [this] {
            canvasRefreshPending = false;
            rebuildCanvas();
        });
    }
    emit changed();
}

QList<int> SuperSourceEditor::selectedRows() const {
    QList<int> rows = selectedSet.values();
    std::sort(rows.begin(), rows.end());
    int l = layoutSelect->currentIndex();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&](int i) { return l < 0 || i < 0 || i >= data[l].boxes.size(); }),
               rows.end());
    return rows;
}
void SuperSourceEditor::selectBoxes(const QList<int> &rows) {
    selectedSet.clear();
    int l = layoutSelect->currentIndex();
    if (l < 0)
        return;
    for (int i : rows)
        if (i >= 0 && i < data[l].boxes.size())
            selectedSet.insert(i);
    selected = rows.isEmpty() ? -1 : rows.last();
    rebuildCanvas();
}
void SuperSourceEditor::commitCanvas() {
    int l = layoutSelect->currentIndex();
    if (l < 0)
        return;
    auto frame = scene->sceneRect();
    for (auto *item : scene->items())
        if (auto *box = dynamic_cast<BoxItem *>(item)) {
            int i = box->data(0).toInt();
            auto r = QRectF(box->pos(), box->rect().size());
            data[l].boxes[i].corners=box->box.corners;
            data[l].boxes[i].rect = {r.x() / frame.width(), r.y() / frame.height(), r.width() / frame.width(),
                                     r.height() / frame.height()};
        }
    selectBox();
    emit changed();
}
QList<SuperSourceBox> SuperSourceEditor::copiedBoxes() const {
    QList<SuperSourceBox> boxes;
    int l = layoutSelect->currentIndex();
    if (l >= 0)
        for (int i : selectedRows())
            boxes.append(data[l].boxes[i]);
    return boxes;
}
void SuperSourceEditor::copySelection() {
    auto boxes = copiedBoxes();
    if (boxes.isEmpty())
        return;
    SuperSourceLayout clip;
    clip.id = "clipboard";
    clip.name = "Clipboard";
    clip.boxes = boxes;
    auto mime = new QMimeData;
    mime->setData("application/x-kavtor-supersource-boxes",
                  QJsonDocument(superSourcesJson({clip})).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);
}
void SuperSourceEditor::pasteBoxes() {
    auto mime = QApplication::clipboard()->mimeData();
    if (!mime->hasFormat("application/x-kavtor-supersource-boxes"))
        return;
    QList<SuperSourceLayout> parsed;
    if (!parseSuperSources(
            QJsonDocument::fromJson(mime->data("application/x-kavtor-supersource-boxes")).array(), &parsed) ||
        parsed.size() != 1)
        return;
    insertBoxes(parsed[0].boxes);
}
void SuperSourceEditor::duplicateSelection() { insertBoxes(copiedBoxes()); }
void SuperSourceEditor::insertBoxes(QList<SuperSourceBox> boxes) {
    int l = layoutSelect->currentIndex();
    if (l < 0 || boxes.isEmpty() || data[l].boxes.size() + boxes.size() > 32)
        return;
    QRectF bounds;
    for (auto &box : boxes)
        bounds = bounds.isNull() ? box.rect : bounds.united(box.rect);
    double dx = bounds.right() + .02 <= 1 ? .02
                : bounds.left() >= .02    ? -.02
                                          : 0,
           dy = bounds.bottom() + .02 <= 1 ? .02
                : bounds.top() >= .02      ? -.02
                                           : 0;
    QSet<int> used;
    for (auto &box : data[l].boxes)
        if (box.keyButton >= 0)
            used.insert(box.keyButton);
    selectedSet.clear();
    for (auto &box : boxes) {
        box.id = uuid();
        box.name += tr(" copy");
        box.audio = false;
        box.rect.translate(dx, dy);
        box.keyButton = -1;
        if (box.kind == "input")
            for (int button = 0; button < 24; ++button)
                if (!used.contains(button)) {
                    box.keyButton = button;
                    used.insert(button);
                    break;
                }
        selected = data[l].boxes.size();
        selectedSet.insert(selected);
        data[l].boxes.append(box);
    }
    rebuildCanvas();
    emit changed();
}
void SuperSourceEditor::removeSelection() {
    int l = layoutSelect->currentIndex();
    if (l < 0)
        return;
    auto rows = selectedRows();
    if (rows.isEmpty())
        return;
    for (auto i = rows.rbegin(); i != rows.rend(); ++i)
        data[l].boxes.removeAt(*i);
    selectedSet.clear();
    selected = -1;
    rebuildCanvas();
    emit changed();
}
void SuperSourceEditor::moveSelection(int delta) {
    int l = layoutSelect->currentIndex();
    if (l < 0)
        return;
    auto rows = selectedRows();
    if (rows.isEmpty())
        return;
    QString primary = selected >= 0 ? data[l].boxes[selected].id : QString();
    if (delta > 0)
        std::reverse(rows.begin(), rows.end());
    for (int i : rows) {
        int next = i + delta;
        if (next < 0 || next >= data[l].boxes.size() || selectedSet.contains(next))
            continue;
        data[l].boxes.swapItemsAt(i, next);
        selectedSet.remove(i);
        selectedSet.insert(next);
    }
    for (int i = 0; i < data[l].boxes.size(); ++i)
        if (data[l].boxes[i].id == primary)
            selected = i;
    rebuildCanvas();
    emit changed();
}
void SuperSourceEditor::alignSelection(const QString &mode, bool toFrame) {
    int l = layoutSelect->currentIndex();
    auto rows = selectedRows();
    if (l < 0 || rows.isEmpty())
        return;
    QRectF bounds;
    for (int i : rows)
        bounds = bounds.isNull() ? data[l].boxes[i].rect : bounds.united(data[l].boxes[i].rect);
    if (toFrame || rows.size() == 1)
        bounds = {0, 0, 1, 1};
    auto reference = data[l].boxes[selectedSet.contains(selected) ? selected : rows.first()].rect;
    for (int i : rows) {
        auto &box = data[l].boxes[i];
        auto r = box.rect;
        if (mode == "left")
            r.moveLeft(bounds.left());
        else if (mode == "right")
            r.moveRight(bounds.right());
        else if (mode == "top")
            r.moveTop(bounds.top());
        else if (mode == "bottom")
            r.moveBottom(bounds.bottom());
        else if (mode == "hcenter")
            r.moveCenter({bounds.center().x(), r.center().y()});
        else if (mode == "vcenter")
            r.moveCenter({r.center().x(), bounds.center().y()});
        else if (mode == "width") {
            double ratio = r.height() / r.width();
            r.setWidth(reference.width());
            if (box.keepAspect)
                r.setHeight(r.width() * ratio);
        } else if (mode == "height") {
            double ratio = r.width() / r.height();
            r.setHeight(reference.height());
            if (box.keepAspect)
                r.setWidth(r.height() * ratio);
        } else
            return;
        double scale = qMin(1., qMin(1 / r.width(), 1 / r.height()));
        r.setSize(r.size() * scale);
        r.moveLeft(qBound(0., r.x(), 1 - r.width()));
        r.moveTop(qBound(0., r.y(), 1 - r.height()));
        box.rect = r;
    }
    rebuildCanvas();
    emit changed();
}
void SuperSourceEditor::distributeSelection(bool horizontal) {
    int l = layoutSelect->currentIndex();
    auto rows = selectedRows();
    if (l < 0 || rows.size() < 3)
        return;
    std::sort(rows.begin(), rows.end(), [&](int a, int b) {
        return horizontal ? data[l].boxes[a].rect.left() < data[l].boxes[b].rect.left()
                          : data[l].boxes[a].rect.top() < data[l].boxes[b].rect.top();
    });
    double begin =
               horizontal ? data[l].boxes[rows.first()].rect.left() : data[l].boxes[rows.first()].rect.top(),
           end = begin, total = 0;
    for (int i : rows) {
        auto r = data[l].boxes[i].rect;
        end = qMax(end, horizontal ? r.right() : r.bottom());
        total += horizontal ? r.width() : r.height();
    }
    double gap = (end - begin - total) / (rows.size() - 1), pos = begin;
    for (int i : rows) {
        auto &r = data[l].boxes[i].rect;
        if (horizontal)
            r.moveLeft(pos);
        else
            r.moveTop(pos);
        pos += (horizontal ? r.width() : r.height()) + gap;
    }
    rebuildCanvas();
    emit changed();
}
void SuperSourceEditor::arrangeGrid(int columns, double gap) {
    int l = layoutSelect->currentIndex();
    auto rows = selectedRows();
    if (l < 0 || rows.isEmpty() || columns < 1)
        return;
    columns = qMin(columns, int(rows.size()));
    int height = (rows.size() + columns - 1) / columns;
    gap = qBound(0., gap, qMin(.1, 1. / qMax(columns, height)));
    double w = (1 - gap * (columns - 1)) / columns, h = (1 - gap * (height - 1)) / height;
    for (int n = 0; n < rows.size(); ++n) {
        auto &box = data[l].boxes[rows[n]];
        QRectF cell((n % columns) * (w + gap), (n / columns) * (h + gap), w, h);
        if (box.keepAspect) {
            double scale = qMin(w / box.rect.width(), h / box.rect.height());
            auto size = box.rect.size() * scale;
            cell = {cell.center().x() - size.width() / 2, cell.center().y() - size.height() / 2, size.width(),
                    size.height()};
        }
        box.rect = cell;
    }
    rebuildCanvas();
    emit changed();
}
void SuperSourceEditor::recordHistory() {
    if (restoringHistory)
        return;
    auto snapshot = superSourcesJson(data);
    if (historyCursor >= 0 && history[historyCursor] == snapshot)
        return;
    while (history.size() > historyCursor + 1)
        history.removeLast();
    history.append(snapshot);
    historyCursor = history.size() - 1;
    if (history.size() > 64) {
        history.removeFirst();
        --historyCursor;
    }
    undoAction->setEnabled(historyCursor > 0);
    redoAction->setEnabled(false);
}
void SuperSourceEditor::restoreHistory(int cursor) {
    if (cursor < 0 || cursor >= history.size())
        return;
    QList<SuperSourceLayout> restored;
    if (!parseSuperSources(history[cursor], &restored))
        return;
    restoringHistory = true;
    data = restored;
    historyCursor = cursor;
    selectedSet.clear();
    selected = -1;
    rebuildLayouts();
    emit changed();
    restoringHistory = false;
    undoAction->setEnabled(cursor > 0);
    redoAction->setEnabled(cursor + 1 < history.size());
}
void SuperSourceEditor::undo() { restoreHistory(historyCursor - 1); }
void SuperSourceEditor::redo() { restoreHistory(historyCursor + 1); }
