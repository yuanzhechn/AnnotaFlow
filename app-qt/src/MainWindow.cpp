#include "MainWindow.h"

#include "AnnotationCanvas.h"
#include "ImageLoader.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

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

    const QString dir = QFileDialog::getExistingDirectory(this, "Open image folder", imageFolder_);
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
        QMessageBox::information(this, "AnnotaFlow", "No supported images were found in this folder.");
        return;
    }

    imageFolder_ = dir;
    imagePaths_ = files;
    annotationsByImage_.clear();
    undoByImage_.clear();
    dirtyImages_.clear();
    loadImageAt(0);
}

void MainWindow::chooseOutputFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Choose annotation output folder", outputFolder_.isEmpty() ? imageFolder_ : outputFolder_);
    if (dir.isEmpty()) {
        return;
    }

    outputFolder_ = dir;
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
        QMessageBox::warning(this, "Save failed", error);
        return;
    }

    dirtyImages_.remove(currentImagePath());
    statusBar()->showMessage(QString("Saved %1 annotations").arg(AnnotationIO::formatDisplayName(currentFormat())), 3000);
    refreshWindowState();
}

void MainWindow::setDrawMode()
{
    canvas_->setMode(AnnotationCanvas::Mode::DrawBox);
    statusBar()->showMessage("Draw mode: drag on the image to create a box. Q cancels.", 3000);
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
    bool ok = false;
    const QString label = QInputDialog::getText(this, "Annotation label", "Label:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || label.isEmpty()) {
        canvas_->setMode(AnnotationCanvas::Mode::Navigate);
        refreshActionState();
        return;
    }

    pushUndoState();

    Annotation annotation;
    annotation.rect = rect.normalized();
    annotation.label = label;
    currentAnnotations().append(annotation);
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(currentAnnotations().size() - 1);
    markCurrentDirty();
    refreshLabels();
    canvas_->setMode(AnnotationCanvas::Mode::Navigate);
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
        statusBar()->showMessage("Nothing to undo", 2000);
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
    canvas_->setSelectedIndex(labelsList_->currentRow());
    refreshActionState();
}

void MainWindow::editSelectedLabel(QListWidgetItem*)
{
    const int index = labelsList_->currentRow();
    if (index < 0 || index >= currentAnnotations().size()) {
        return;
    }

    bool ok = false;
    const QString label = QInputDialog::getText(this,
                                                "Edit label",
                                                "Label:",
                                                QLineEdit::Normal,
                                                currentAnnotations()[index].label,
                                                &ok).trimmed();
    if (!ok || label.isEmpty()) {
        return;
    }

    pushUndoState();
    currentAnnotations()[index].label = label;
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(index);
    markCurrentDirty();
    refreshLabels();
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
    openFolderAction_ = new QAction("Open Folder", this);
    openFolderAction_->setShortcut(QKeySequence("Ctrl+O"));
    connect(openFolderAction_, &QAction::triggered, this, &MainWindow::openImageFolder);

    outputFolderAction_ = new QAction("Output Folder", this);
    outputFolderAction_->setShortcut(QKeySequence("Ctrl+R"));
    connect(outputFolderAction_, &QAction::triggered, this, &MainWindow::chooseOutputFolder);

    previousAction_ = new QAction("Previous", this);
    previousAction_->setShortcut(Qt::Key_A);
    connect(previousAction_, &QAction::triggered, this, &MainWindow::previousImage);

    nextAction_ = new QAction("Next", this);
    nextAction_->setShortcut(Qt::Key_D);
    connect(nextAction_, &QAction::triggered, this, &MainWindow::nextImage);

    saveAction_ = new QAction("Save", this);
    saveAction_->setShortcut(Qt::Key_S);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveCurrentAnnotations);

    drawAction_ = new QAction("Draw Box", this);
    drawAction_->setShortcut(Qt::Key_W);
    connect(drawAction_, &QAction::triggered, this, &MainWindow::setDrawMode);

    fitAction_ = new QAction("Fit", this);
    fitAction_->setShortcut(Qt::Key_F);
    connect(fitAction_, &QAction::triggered, this, &MainWindow::fitImage);

    zoomInAction_ = new QAction("Zoom In", this);
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction_, &QAction::triggered, this, &MainWindow::zoomIn);

    zoomOutAction_ = new QAction("Zoom Out", this);
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction_, &QAction::triggered, this, &MainWindow::zoomOut);

    deleteAction_ = new QAction("Delete", this);
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteSelectedAnnotation);

    undoAction_ = new QAction("Undo / Cancel", this);
    undoAction_->setShortcut(Qt::Key_Q);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::cancelOrUndo);

    QMenu* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(openFolderAction_);
    fileMenu->addAction(outputFolderAction_);
    fileMenu->addAction(saveAction_);

    QMenu* editMenu = menuBar()->addMenu("Edit");
    editMenu->addAction(drawAction_);
    editMenu->addAction(deleteAction_);
    editMenu->addAction(undoAction_);

    QMenu* viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(fitAction_);
    viewMenu->addAction(zoomInAction_);
    viewMenu->addAction(zoomOutAction_);
}

void MainWindow::createToolbar()
{
    QToolBar* toolbar = addToolBar("Main");
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
    QDockWidget* dock = new QDockWidget("Annotations", this);
    QWidget* panel = new QWidget(dock);
    QVBoxLayout* layout = new QVBoxLayout(panel);

    imageInfoLabel_ = new QLabel("No folder opened", panel);
    imageInfoLabel_->setWordWrap(true);
    outputInfoLabel_ = new QLabel("Output: not selected", panel);
    outputInfoLabel_->setWordWrap(true);
    cursorInfoLabel_ = new QLabel("x: -, y: -", panel);

    labelsList_ = new QListWidget(panel);
    labelsList_->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(imageInfoLabel_);
    layout->addWidget(outputInfoLabel_);
    layout->addWidget(cursorInfoLabel_);
    layout->addWidget(labelsList_, 1);
    panel->setLayout(layout);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(labelsList_, &QListWidget::currentRowChanged, this, &MainWindow::onListSelectionChanged);
    connect(labelsList_, &QListWidget::itemDoubleClicked, this, &MainWindow::editSelectedLabel);
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
        QMessageBox::warning(this, "Could not open image", QString("%1\n\n%2").arg(path, error));
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

    canvas_->setImage(image);
    canvas_->setAnnotations(currentAnnotations());
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
        const QRectF rect = annotations[i].rect.normalized();
        labelsList_->addItem(QString("%1. %2  [%3,%4,%5,%6]")
                                 .arg(i + 1)
                                 .arg(annotations[i].label)
                                 .arg(static_cast<int>(rect.x()))
                                 .arg(static_cast<int>(rect.y()))
                                 .arg(static_cast<int>(rect.width()))
                                 .arg(static_cast<int>(rect.height())));
    }
    labelsList_->setCurrentRow(canvas_->selectedIndex());
    syncingListSelection_ = false;
}

void MainWindow::refreshWindowState()
{
    const QString path = currentImagePath();
    const bool dirty = dirtyImages_.contains(path);
    const QString titlePath = path.isEmpty() ? QString("AnnotaFlow") : QFileInfo(path).fileName();
    setWindowTitle(QString("%1%2 - AnnotaFlow").arg(dirty ? "*" : "", titlePath));

    if (currentIndex_ >= 0) {
        imageInfoLabel_->setText(QString("Image %1 / %2\n%3\n%4 x %5")
                                     .arg(currentIndex_ + 1)
                                     .arg(imagePaths_.size())
                                     .arg(QFileInfo(path).fileName())
                                     .arg(currentImageSize_.width())
                                     .arg(currentImageSize_.height()));
    } else {
        imageInfoLabel_->setText("No folder opened");
    }

    outputInfoLabel_->setText(outputFolder_.isEmpty()
                                  ? "Output: not selected"
                                  : QString("Output: %1").arg(QDir::toNativeSeparators(outputFolder_)));
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

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        "Unsaved annotations",
        "There are unsaved annotations. Save the current image before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Cancel) {
        return false;
    }

    if (choice == QMessageBox::Discard) {
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
