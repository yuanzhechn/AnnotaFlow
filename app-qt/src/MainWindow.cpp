#include "MainWindow.h"

#include "AnnotationCanvas.h"
#include "ImageLoader.h"

#include <QAction>
#include <QColorDialog>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>
#include <QXmlStreamReader>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    canvas_ = new AnnotationCanvas(this);
    setCentralWidget(canvas_);

    createActions();
    createToolbar();
    createDock();

    connect(canvas_, &AnnotationCanvas::rectangleCreated, this, &MainWindow::addRectangle);
    connect(canvas_, &AnnotationCanvas::selectionChanged, this, &MainWindow::onCanvasSelectionChanged);
    connect(canvas_, &AnnotationCanvas::cursorImagePositionChanged, this, &MainWindow::updateCursorPosition);

    resize(1280, 820);
    refreshWindowState();
    refreshActionState();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveDirtyImages()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::openImageFolder()
{
    if (!maybeSaveDirtyImages()) {
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(this, "打开图片文件夹", imageFolder_);
    if (dir.isEmpty()) {
        return;
    }

    QStringList files;
    const QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif", "*.tif", "*.tiff"};
    QDirIterator it(dir, filters, QDir::Files, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        files.append(QDir::toNativeSeparators(it.next()));
    }
    files.sort(Qt::CaseInsensitive);

    if (files.isEmpty()) {
        QMessageBox::information(this, "AnnotaFlow", "这个文件夹里没有找到支持的图片。");
        return;
    }

    imageFolder_ = dir;
    imagePaths_ = files;
    annotationsByImage_.clear();
    undoByImage_.clear();
    dirtyImages_.clear();
    classNames_.clear();
    classColors_.clear();
    lastLabel_ = "未命名";
    if (!outputFolder_.isEmpty()) {
        loadClassCatalog();
        addKnownLabelsFromOutput();
    }
    loadImageAt(0);
}

void MainWindow::chooseOutputFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "选择标注保存目录", outputFolder_.isEmpty() ? imageFolder_ : outputFolder_);
    if (dir.isEmpty()) {
        return;
    }

    outputFolder_ = dir;
    loadClassCatalog();
    addKnownLabelsFromOutput();
    if (currentIndex_ >= 0) {
        const QString path = currentImagePath();
        if (!annotationsByImage_.contains(path)) {
            QVector<Annotation> loaded;
            QString error;
            if (AnnotationIO::load(currentFormat(), path, outputFolder_, currentImageSize_, &loaded, &error)) {
                annotationsByImage_[path] = loaded;
                canvas_->setAnnotations(loaded);
                refreshLabels();
            } else {
                statusBar()->showMessage(error, 4000);
            }
        }
    }

    refreshClassList();
    refreshWindowState();
    refreshActionState();
}

void MainWindow::previousImage()
{
    if (currentIndex_ > 0) {
        loadImageAt(currentIndex_ - 1);
    }
}

void MainWindow::nextImage()
{
    if (currentIndex_ >= 0 && currentIndex_ < imagePaths_.size() - 1) {
        loadImageAt(currentIndex_ + 1);
    }
}

void MainWindow::saveCurrentAnnotations()
{
    if (currentIndex_ < 0 || !ensureOutputFolder()) {
        return;
    }

    QString error;
    if (!AnnotationIO::save(currentFormat(),
                            currentImagePath(),
                            outputFolder_,
                            currentImageSize_,
                            currentAnnotations(),
                            &error)) {
        QMessageBox::warning(this, "保存失败", error);
        return;
    }

    dirtyImages_.remove(currentImagePath());
    saveClassCatalog();
    statusBar()->showMessage(QString("已保存 %1 标注").arg(AnnotationIO::formatDisplayName(currentFormat())), 3000);
    refreshWindowState();
}

void MainWindow::setDrawMode()
{
    canvas_->setMode(AnnotationCanvas::Mode::DrawBox);
    statusBar()->showMessage("画框模式：在图片上拖拽创建标注框，按 Q 退出。", 3000);
    refreshActionState();
}

void MainWindow::fitImage()
{
    canvas_->fitToWindow();
}

void MainWindow::zoomIn()
{
    canvas_->zoomIn();
}

void MainWindow::zoomOut()
{
    canvas_->zoomOut();
}

void MainWindow::addRectangle(const QRectF& rect)
{
    pushUndoState();

    const QString label = lastLabel_.trimmed().isEmpty() ? QString("未命名") : lastLabel_.trimmed();
    ensureClassExists(label);
    Annotation annotation;
    annotation.rect = rect.normalized();
    annotation.label = label;
    currentAnnotations().append(annotation);
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(currentAnnotations().size() - 1);
    markCurrentDirty();
    refreshClassList();
    refreshLabels();
    canvas_->setMode(AnnotationCanvas::Mode::DrawBox);
    statusBar()->showMessage(QString("已添加标注，标签沿用：%1。不同的话可在右侧列表双击修改。").arg(label), 3500);
    refreshActionState();
}

void MainWindow::deleteSelectedAnnotation()
{
    const int index = canvas_->selectedIndex();
    if (currentIndex_ < 0 || index < 0 || index >= currentAnnotations().size()) {
        return;
    }

    pushUndoState();
    currentAnnotations().remove(index);
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(-1);
    markCurrentDirty();
    refreshLabels();
}

void MainWindow::undoLastChange()
{
    if (currentIndex_ < 0) {
        return;
    }

    const QString path = currentImagePath();
    QVector<QVector<Annotation>>& history = undoByImage_[path];
    if (history.isEmpty()) {
        statusBar()->showMessage("没有可撤销的操作", 2000);
        return;
    }

    annotationsByImage_[path] = history.takeLast();
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(-1);
    markCurrentDirty();
    refreshLabels();
}

void MainWindow::cancelOrUndo()
{
    if (canvas_->mode() == AnnotationCanvas::Mode::DrawBox) {
        canvas_->cancelInteraction();
        refreshActionState();
        return;
    }
    undoLastChange();
}

void MainWindow::onCanvasSelectionChanged(int index)
{
    if (index >= 0 && index < currentAnnotations().size()) {
        lastLabel_ = currentAnnotations()[index].label;
        selectClassByIndex(classNames_.indexOf(lastLabel_));
    }
    syncingListSelection_ = true;
    labelsList_->setCurrentRow(index);
    syncingListSelection_ = false;
    refreshActionState();
}

void MainWindow::onListSelectionChanged()
{
    if (syncingListSelection_) {
        return;
    }
    const int row = labelsList_->currentRow();
    if (row >= 0 && row < currentAnnotations().size()) {
        lastLabel_ = currentAnnotations()[row].label;
        selectClassByIndex(classNames_.indexOf(lastLabel_));
    }
    canvas_->setSelectedIndex(row);
    refreshActionState();
}

void MainWindow::onLabelItemChanged(QListWidgetItem* item)
{
    if (syncingListSelection_ || !item) {
        return;
    }

    const int index = labelsList_->row(item);
    if (index < 0 || index >= currentAnnotations().size()) {
        return;
    }

    QString label = item->text().trimmed();
    if (label.isEmpty()) {
        label = currentAnnotations()[index].label.trimmed();
        if (label.isEmpty()) {
            label = "未命名";
        }
        const QSignalBlocker blocker(labelsList_);
        item->setText(label);
        return;
    }

    if (label == currentAnnotations()[index].label) {
        return;
    }

    pushUndoState();
    currentAnnotations()[index].label = label;
    ensureClassExists(label);
    lastLabel_ = label;
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(index);
    item->setToolTip(annotationSummary(index));
    markCurrentDirty();
    refreshClassList();
    statusBar()->showMessage(QString("已更新标签：%1").arg(label), 2000);
    saveClassCatalog();
}

void MainWindow::onClassSelectionChanged()
{
    if (syncingClassSelection_) {
        return;
    }

    const int row = classesList_->currentRow();
    if (row < 0 || row >= classNames_.size()) {
        return;
    }

    lastLabel_ = classNames_[row];
    statusBar()->showMessage(QString("当前类别：%1").arg(lastLabel_), 2000);
}

void MainWindow::chooseClassColor(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    const int index = classesList_->row(item);
    if (index < 0 || index >= classNames_.size()) {
        return;
    }

    const QString label = classNames_[index];
    const QColor current = colorForLabel(label);
    const QColor color = QColorDialog::getColor(current, this, QString("选择“%1”的颜色").arg(label));
    if (!color.isValid()) {
        return;
    }

    classColors_[label] = color;
    canvas_->setLabelColors(classColors_);
    refreshClassList();
    refreshLabels();
    saveClassCatalog();
}

void MainWindow::updateCursorPosition(const QPointF& imagePosition)
{
    if (currentImageSize_.isEmpty()) {
        cursorInfoLabel_->setText("x: -, y: -");
        return;
    }

    cursorInfoLabel_->setText(QString("x: %1, y: %2")
                                  .arg(static_cast<int>(imagePosition.x()))
                                  .arg(static_cast<int>(imagePosition.y())));
}

void MainWindow::createActions()
{
    openFolderAction_ = new QAction("打开文件夹", this);
    openFolderAction_->setShortcut(QKeySequence("Ctrl+O"));
    connect(openFolderAction_, &QAction::triggered, this, &MainWindow::openImageFolder);

    outputFolderAction_ = new QAction("保存目录", this);
    outputFolderAction_->setShortcut(QKeySequence("Ctrl+R"));
    connect(outputFolderAction_, &QAction::triggered, this, &MainWindow::chooseOutputFolder);

    previousAction_ = new QAction("上一张", this);
    previousAction_->setShortcut(Qt::Key_A);
    connect(previousAction_, &QAction::triggered, this, &MainWindow::previousImage);

    nextAction_ = new QAction("下一张", this);
    nextAction_->setShortcut(Qt::Key_D);
    connect(nextAction_, &QAction::triggered, this, &MainWindow::nextImage);

    saveAction_ = new QAction("保存", this);
    saveAction_->setShortcut(Qt::Key_S);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveCurrentAnnotations);

    drawAction_ = new QAction("画框", this);
    drawAction_->setShortcut(Qt::Key_W);
    connect(drawAction_, &QAction::triggered, this, &MainWindow::setDrawMode);

    fitAction_ = new QAction("适应窗口", this);
    fitAction_->setShortcut(Qt::Key_F);
    connect(fitAction_, &QAction::triggered, this, &MainWindow::fitImage);

    zoomInAction_ = new QAction("放大", this);
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction_, &QAction::triggered, this, &MainWindow::zoomIn);

    zoomOutAction_ = new QAction("缩小", this);
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction_, &QAction::triggered, this, &MainWindow::zoomOut);

    deleteAction_ = new QAction("删除", this);
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteSelectedAnnotation);

    undoAction_ = new QAction("撤销 / 取消", this);
    undoAction_->setShortcut(Qt::Key_Q);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::cancelOrUndo);

    for (int i = 0; i < 10; ++i) {
        const int classIndex = i == 9 ? 9 : i;
        QAction* action = new QAction(this);
        const int key = i == 9 ? Qt::Key_0 : Qt::Key_1 + i;
        action->setShortcut(QKeySequence(Qt::CTRL | key));
        connect(action, &QAction::triggered, this, [this, classIndex]() {
            selectClassByIndex(classIndex);
        });
        addAction(action);
        classShortcutActions_.append(action);
    }

    QMenu* fileMenu = menuBar()->addMenu("文件");
    fileMenu->addAction(openFolderAction_);
    fileMenu->addAction(outputFolderAction_);
    fileMenu->addAction(saveAction_);

    QMenu* editMenu = menuBar()->addMenu("编辑");
    editMenu->addAction(drawAction_);
    editMenu->addAction(deleteAction_);
    editMenu->addAction(undoAction_);

    QMenu* viewMenu = menuBar()->addMenu("视图");
    viewMenu->addAction(fitAction_);
    viewMenu->addAction(zoomInAction_);
    viewMenu->addAction(zoomOutAction_);
}

void MainWindow::createToolbar()
{
    QToolBar* toolbar = addToolBar("主工具栏");
    toolbar->setMovable(false);
    toolbar->addAction(openFolderAction_);
    toolbar->addAction(outputFolderAction_);
    toolbar->addSeparator();
    toolbar->addAction(previousAction_);
    toolbar->addAction(nextAction_);
    toolbar->addSeparator();
    toolbar->addAction(drawAction_);
    toolbar->addAction(deleteAction_);
    toolbar->addAction(undoAction_);
    toolbar->addSeparator();
    toolbar->addAction(saveAction_);
    toolbar->addSeparator();
    toolbar->addAction(fitAction_);
    toolbar->addAction(zoomInAction_);
    toolbar->addAction(zoomOutAction_);
    toolbar->addSeparator();

    formatCombo_ = new QComboBox(toolbar);
    formatCombo_->addItem("XML", static_cast<int>(AnnotationIO::SaveFormat::VocXml));
    formatCombo_->addItem("YOLO", static_cast<int>(AnnotationIO::SaveFormat::Yolo));
    toolbar->addWidget(formatCombo_);
}

void MainWindow::createDock()
{
    QDockWidget* dock = new QDockWidget("标注列表", this);
    QWidget* panel = new QWidget(dock);
    QVBoxLayout* layout = new QVBoxLayout(panel);

    imageInfoLabel_ = new QLabel("未打开图片文件夹", panel);
    imageInfoLabel_->setWordWrap(true);
    outputInfoLabel_ = new QLabel("保存目录：未选择", panel);
    outputInfoLabel_->setWordWrap(true);
    cursorInfoLabel_ = new QLabel("x: -, y: -", panel);

    QLabel* classesTitle = new QLabel("数据集标签（双击改色，Ctrl+1..9/0 选择）", panel);
    classesTitle->setWordWrap(true);
    classesList_ = new QListWidget(panel);
    classesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    classesList_->setMaximumHeight(190);

    QLabel* labelsTitle = new QLabel("当前图片标注（双击文字可改标签）", panel);
    labelsTitle->setWordWrap(true);
    labelsList_ = new QListWidget(panel);
    labelsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    labelsList_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    layout->addWidget(imageInfoLabel_);
    layout->addWidget(outputInfoLabel_);
    layout->addWidget(cursorInfoLabel_);
    layout->addWidget(classesTitle);
    layout->addWidget(classesList_);
    layout->addWidget(labelsTitle);
    layout->addWidget(labelsList_, 1);
    panel->setLayout(layout);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(classesList_, &QListWidget::currentRowChanged, this, &MainWindow::onClassSelectionChanged);
    connect(classesList_, &QListWidget::itemDoubleClicked, this, &MainWindow::chooseClassColor);
    connect(labelsList_, &QListWidget::currentRowChanged, this, &MainWindow::onListSelectionChanged);
    connect(labelsList_, &QListWidget::itemChanged, this, &MainWindow::onLabelItemChanged);
}

void MainWindow::loadImageAt(int index)
{
    if (index < 0 || index >= imagePaths_.size()) {
        return;
    }

    QImage image;
    QString error;
    const QString path = imagePaths_[index];
    if (!ImageLoader::loadImage(path, &image, &error)) {
        QMessageBox::warning(this, "无法打开图片", QString("%1\n\n%2").arg(path, error));
        return;
    }

    currentIndex_ = index;
    currentImageSize_ = image.size();

    if (!annotationsByImage_.contains(path)) {
        QVector<Annotation> loaded;
        if (!outputFolder_.isEmpty()) {
            QString loadError;
            if (!AnnotationIO::load(currentFormat(), path, outputFolder_, currentImageSize_, &loaded, &loadError)) {
                statusBar()->showMessage(loadError, 4000);
            }
        }
        annotationsByImage_[path] = loaded;
    }

    for (const Annotation& annotation : annotationsByImage_[path]) {
        ensureClassExists(annotation.label);
    }

    canvas_->setImage(image);
    canvas_->setLabelColors(classColors_);
    canvas_->setAnnotations(currentAnnotations());
    if (!currentAnnotations().isEmpty()) {
        lastLabel_ = currentAnnotations().last().label;
        ensureClassExists(lastLabel_);
    }
    refreshClassList();
    refreshLabels();
    refreshWindowState();
    refreshActionState();
}

void MainWindow::refreshLabels()
{
    syncingListSelection_ = true;
    labelsList_->clear();
    const QVector<Annotation>& annotations = currentAnnotations();
    for (int i = 0; i < annotations.size(); ++i) {
        QListWidgetItem* item = new QListWidgetItem(annotations[i].label);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setToolTip(annotationSummary(i));
        const QColor color = colorForLabel(annotations[i].label);
        item->setBackground(color);
        const int luminance = (color.red() * 299 + color.green() * 587 + color.blue() * 114) / 1000;
        item->setForeground(luminance > 150 ? QColor(20, 24, 28) : QColor(246, 248, 250));
        labelsList_->addItem(item);
    }
    labelsList_->setCurrentRow(canvas_->selectedIndex());
    syncingListSelection_ = false;
}

void MainWindow::refreshClassList()
{
    if (!classesList_) {
        return;
    }

    syncingClassSelection_ = true;
    classesList_->clear();

    for (int i = 0; i < classNames_.size(); ++i) {
        const QString& label = classNames_[i];
        const QString prefix = i < 9 ? QString("Ctrl+%1  ").arg(i + 1)
                                     : (i == 9 ? QString("Ctrl+0  ") : QString());
        QListWidgetItem* item = new QListWidgetItem(prefix + label);
        item->setToolTip(classSummary(i));
        const QColor color = colorForLabel(label);
        item->setBackground(color);
        const int luminance = (color.red() * 299 + color.green() * 587 + color.blue() * 114) / 1000;
        item->setForeground(luminance > 150 ? QColor(20, 24, 28) : QColor(246, 248, 250));
        classesList_->addItem(item);
    }

    const int currentIndex = classNames_.indexOf(lastLabel_);
    classesList_->setCurrentRow(currentIndex);
    syncingClassSelection_ = false;
}

void MainWindow::refreshWindowState()
{
    const QString path = currentImagePath();
    const bool dirty = dirtyImages_.contains(path);
    const QString titlePath = path.isEmpty() ? QString("AnnotaFlow") : QFileInfo(path).fileName();
    setWindowTitle(QString("%1%2 - AnnotaFlow").arg(dirty ? "*" : "", titlePath));

    if (currentIndex_ >= 0) {
        imageInfoLabel_->setText(QString("图片 %1 / %2\n%3\n%4 x %5")
                                     .arg(currentIndex_ + 1)
                                     .arg(imagePaths_.size())
                                     .arg(QFileInfo(path).fileName())
                                     .arg(currentImageSize_.width())
                                     .arg(currentImageSize_.height()));
    } else {
        imageInfoLabel_->setText("未打开图片文件夹");
    }

    outputInfoLabel_->setText(outputFolder_.isEmpty()
                                  ? "保存目录：未选择"
                                  : QString("保存目录：%1").arg(QDir::toNativeSeparators(outputFolder_)));
}

void MainWindow::refreshActionState()
{
    const bool hasImage = currentIndex_ >= 0;
    previousAction_->setEnabled(hasImage && currentIndex_ > 0);
    nextAction_->setEnabled(hasImage && currentIndex_ < imagePaths_.size() - 1);
    saveAction_->setEnabled(hasImage);
    drawAction_->setEnabled(hasImage);
    fitAction_->setEnabled(hasImage);
    zoomInAction_->setEnabled(hasImage);
    zoomOutAction_->setEnabled(hasImage);
    deleteAction_->setEnabled(hasImage && canvas_->selectedIndex() >= 0);
    undoAction_->setEnabled(hasImage);
    for (int i = 0; i < classShortcutActions_.size(); ++i) {
        classShortcutActions_[i]->setEnabled(i < classNames_.size());
    }
}

void MainWindow::pushUndoState()
{
    if (currentIndex_ < 0) {
        return;
    }

    QVector<QVector<Annotation>>& history = undoByImage_[currentImagePath()];
    history.append(currentAnnotations());
    constexpr int kMaxUndoDepth = 50;
    if (history.size() > kMaxUndoDepth) {
        history.removeFirst();
    }
}

void MainWindow::markCurrentDirty()
{
    if (currentIndex_ >= 0) {
        dirtyImages_.insert(currentImagePath());
        refreshWindowState();
        refreshActionState();
    }
}

bool MainWindow::maybeSaveDirtyImages()
{
    if (dirtyImages_.isEmpty()) {
        return true;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle("存在未保存标注");
    box.setText("当前图片有未保存的标注，继续前是否保存？");
    QPushButton* saveButton = box.addButton("保存", QMessageBox::AcceptRole);
    QPushButton* discardButton = box.addButton("不保存", QMessageBox::DestructiveRole);
    QPushButton* cancelButton = box.addButton("取消", QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.exec();

    if (box.clickedButton() == cancelButton) {
        return false;
    }

    if (box.clickedButton() == discardButton) {
        dirtyImages_.clear();
        return true;
    }

    saveCurrentAnnotations();
    return !dirtyImages_.contains(currentImagePath());
}

bool MainWindow::ensureOutputFolder()
{
    if (!outputFolder_.isEmpty()) {
        return true;
    }

    chooseOutputFolder();
    return !outputFolder_.isEmpty();
}

QString MainWindow::annotationSummary(int index) const
{
    if (index < 0 || index >= currentAnnotations().size()) {
        return QString();
    }

    const Annotation& annotation = currentAnnotations()[index];
    const QRectF rect = annotation.rect.normalized();
    return QString("第 %1 个标注\n标签：%2\n位置：x=%3, y=%4, w=%5, h=%6\n双击可直接编辑标签")
        .arg(index + 1)
        .arg(annotation.label)
        .arg(static_cast<int>(rect.x()))
        .arg(static_cast<int>(rect.y()))
        .arg(static_cast<int>(rect.width()))
        .arg(static_cast<int>(rect.height()));
}

QString MainWindow::classSummary(int index) const
{
    if (index < 0 || index >= classNames_.size()) {
        return QString();
    }

    const QString shortcut = index < 9 ? QString("Ctrl+%1").arg(index + 1)
                                      : (index == 9 ? QString("Ctrl+0") : QString("无"));
    return QString("类别：%1\n快捷键：%2\n双击选择颜色")
        .arg(classNames_[index])
        .arg(shortcut);
}

void MainWindow::ensureClassExists(const QString& label)
{
    const QString normalized = label.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    if (!classNames_.contains(normalized)) {
        classNames_.append(normalized);
    }

    if (!classColors_.contains(normalized) || !classColors_[normalized].isValid()) {
        classColors_[normalized] = defaultColorForLabel(normalized);
    }

    canvas_->setLabelColors(classColors_);
}

void MainWindow::selectClassByIndex(int index)
{
    if (index < 0 || index >= classNames_.size()) {
        statusBar()->showMessage("这个快捷键还没有对应的类别", 2000);
        return;
    }

    lastLabel_ = classNames_[index];
    if (classesList_) {
        syncingClassSelection_ = true;
        classesList_->setCurrentRow(index);
        syncingClassSelection_ = false;
    }
    statusBar()->showMessage(QString("当前类别：%1").arg(lastLabel_), 2000);
}

QColor MainWindow::colorForLabel(const QString& label) const
{
    const QColor color = classColors_.value(label.trimmed());
    return color.isValid() ? color : defaultColorForLabel(label);
}

QColor MainWindow::defaultColorForLabel(const QString& label) const
{
    static const QVector<QColor> palette = {
        QColor("#2F80ED"),
        QColor("#27AE60"),
        QColor("#F2994A"),
        QColor("#EB5757"),
        QColor("#9B51E0"),
        QColor("#00A8A8"),
        QColor("#F2C94C"),
        QColor("#56CCF2"),
        QColor("#FF6B9A"),
        QColor("#7F8C8D")
    };

    uint hash = qHash(label.trimmed());
    return palette[static_cast<int>(hash % palette.size())];
}

void MainWindow::loadClassCatalog()
{
    if (outputFolder_.isEmpty()) {
        return;
    }

    QFile file(QDir(outputFolder_).filePath("annotaflow_labels.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }

    const QJsonArray labels = doc.object().value("labels").toArray();
    for (const QJsonValue& value : labels) {
        const QJsonObject item = value.toObject();
        const QString name = item.value("name").toString().trimmed();
        const QColor color(item.value("color").toString());
        if (name.isEmpty()) {
            continue;
        }
        ensureClassExists(name);
        if (color.isValid()) {
            classColors_[name] = color;
        }
    }

    canvas_->setLabelColors(classColors_);
}

void MainWindow::saveClassCatalog() const
{
    if (outputFolder_.isEmpty()) {
        return;
    }

    QFile file(QDir(outputFolder_).filePath("annotaflow_labels.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    QJsonArray labels;
    for (const QString& name : classNames_) {
        QJsonObject item;
        item["name"] = name;
        item["color"] = colorForLabel(name).name(QColor::HexRgb);
        labels.append(item);
    }

    QJsonObject root;
    root["labels"] = labels;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void MainWindow::addKnownLabelsFromOutput()
{
    if (outputFolder_.isEmpty()) {
        return;
    }

    const QString classesPath = QDir(outputFolder_).filePath("yolo_labels/classes.txt");
    QFile classesFile(classesPath);
    if (classesFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&classesFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            ensureClassExists(in.readLine());
        }
    }

    QDir xmlDir(QDir(outputFolder_).filePath("xml_labels"));
    const QStringList xmlFiles = xmlDir.entryList(QStringList{"*.xml"}, QDir::Files);
    for (const QString& xmlFileName : xmlFiles) {
        QFile xmlFile(xmlDir.filePath(xmlFileName));
        if (!xmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QXmlStreamReader xml(&xmlFile);
        while (xml.readNextStartElement()) {
            if (xml.name() == "annotation") {
                while (xml.readNextStartElement()) {
                    if (xml.name() == "object") {
                        while (xml.readNextStartElement()) {
                            if (xml.name() == "name") {
                                ensureClassExists(xml.readElementText());
                            } else {
                                xml.skipCurrentElement();
                            }
                        }
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            } else {
                xml.skipCurrentElement();
            }
        }
    }

    canvas_->setLabelColors(classColors_);
}

AnnotationIO::SaveFormat MainWindow::currentFormat() const
{
    return static_cast<AnnotationIO::SaveFormat>(formatCombo_->currentData().toInt());
}

QString MainWindow::currentImagePath() const
{
    if (currentIndex_ < 0 || currentIndex_ >= imagePaths_.size()) {
        return QString();
    }
    return imagePaths_[currentIndex_];
}

QVector<Annotation>& MainWindow::currentAnnotations()
{
    return annotationsByImage_[currentImagePath()];
}

const QVector<Annotation>& MainWindow::currentAnnotations() const
{
    static const QVector<Annotation> empty;
    const QString path = currentImagePath();
    auto it = annotationsByImage_.constFind(path);
    return it != annotationsByImage_.constEnd() ? it.value() : empty;
}
