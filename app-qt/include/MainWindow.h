#pragma once

#include "Annotation.h"
#include "AnnotationIO.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <QVector>

class AnnotationCanvas;
class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openImageFolder();
    void chooseOutputFolder();
    void previousImage();
    void nextImage();
    void saveCurrentAnnotations();
    void setDrawMode();
    void fitImage();
    void zoomIn();
    void zoomOut();
    void addRectangle(const QRectF& rect);
    void deleteSelectedAnnotation();
    void undoLastChange();
    void cancelOrUndo();
    void onCanvasSelectionChanged(int index);
    void onListSelectionChanged();
    void editSelectedLabel(QListWidgetItem* item);
    void updateCursorPosition(const QPointF& imagePosition);

private:
    void createActions();
    void createToolbar();
    void createDock();
    void loadImageAt(int index);
    void refreshLabels();
    void refreshWindowState();
    void refreshActionState();
    void pushUndoState();
    void markCurrentDirty();
    bool maybeSaveDirtyImages();
    bool ensureOutputFolder();
    AnnotationIO::SaveFormat currentFormat() const;
    QString currentImagePath() const;
    QVector<Annotation>& currentAnnotations();
    const QVector<Annotation>& currentAnnotations() const;

    AnnotationCanvas* canvas_ = nullptr;
    QListWidget* labelsList_ = nullptr;
    QLabel* imageInfoLabel_ = nullptr;
    QLabel* outputInfoLabel_ = nullptr;
    QLabel* cursorInfoLabel_ = nullptr;
    QComboBox* formatCombo_ = nullptr;

    QAction* openFolderAction_ = nullptr;
    QAction* outputFolderAction_ = nullptr;
    QAction* previousAction_ = nullptr;
    QAction* nextAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* drawAction_ = nullptr;
    QAction* fitAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* undoAction_ = nullptr;

    QString imageFolder_;
    QString outputFolder_;
    QStringList imagePaths_;
    int currentIndex_ = -1;
    QSize currentImageSize_;
    bool syncingListSelection_ = false;

    QHash<QString, QVector<Annotation>> annotationsByImage_;
    QHash<QString, QVector<QVector<Annotation>>> undoByImage_;
    QSet<QString> dirtyImages_;
};
