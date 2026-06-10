#pragma once

#include "Annotation.h"
#include "AnnotationIO.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <QVector>

class AnnotationCanvas;
class QAction;
class QDialog;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openDataset(const QString& imageFolder,
                     const QString& outputFolder = QString(),
                     QString* errorMessage = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openImageFolder();
    void chooseOutputFolder();
    void previousImage();
    void nextImage();
    void saveCurrentAnnotations();
    void setDrawMode();
    void setAiPointMode();
    void fitImage();
    void zoomIn();
    void zoomOut();
    void showShortcutOverview();
    void addRectangle(const QRectF& rect);
    void requestSamPrediction(const QPointF& imagePoint, int pointLabel);
    void handleSamPrediction(QNetworkReply* reply);
    void retrySamPredictionAfterServiceStart();
    void acceptSamProposal();
    void rejectSamProposal();
    void deleteSelectedAnnotation();
    void undoLastSamPrompt();
    void undoLastChange();
    void redoLastChange();
    void cancelOrUndo();
    void onCanvasSelectionChanged(int index);
    void onListSelectionChanged();
    void onClassSelectionChanged();
    void chooseClassColor(QListWidgetItem* item);
    void addClassFromCatalog();
    void editSelectedAnnotationLabel();
    void renameSelectedClass();
    void deleteSelectedClass();
    void saveAsAnnotationFormat();
    void updateCursorPosition(const QPointF& imagePosition);

private:
    void createActions();
    void createToolbar();
    void createDock();
    void loadImageAt(int index);
    void refreshLabels();
    void refreshClassList();
    void refreshWindowState();
    void refreshActionState();
    void pushUndoState();
    void markCurrentDirty();
    void autoSaveCurrentAnnotations();
    bool hasUsableCurrentLabel() const;
    bool startSamService();
    void stopSamService();
    void submitSamPredictionForCurrentPrompts();
    void cancelActiveSamRequest();
    void postSamPrediction(const QByteArray& payload);
    void postSamPrepare(const QString& imagePath);
    void scheduleSamPrepare(const QString& imagePath, bool ensureServiceStart, int delayMs);
    void setSamStatus(const QString& text, const QString& tooltip, const QString& colorHex);
    bool loadImageWithCache(const QString& path, QImage* image, QString* errorMessage = nullptr);
    void cacheImage(const QString& path, const QImage& image);
    void prefetchNearbyImages(int centerIndex);
    QByteArray loadShortcutOverviewSvg() const;
    int preloadDatasetAnnotations();
    bool maybeSaveDirtyImages();
    bool ensureOutputFolder();
    bool chooseFormatForLabelFolder(const QString& folder,
                                    AnnotationIO::SaveFormat* selectedFormat);
    QString annotationSummary(int index) const;
    QString classSummary(int index) const;
    QString promptForLabelName(const QString& title, const QString& initialText = QString());
    void ensureClassExists(const QString& label);
    int countCurrentImageAnnotationsForClass(const QString& label) const;
    int countAnnotationsForClass(const QString& label) const;
    void removeAnnotationsForClass(const QString& label);
    void persistDatasetAfterClassDeletion();
    bool saveActiveFormat(bool wholeDataset, QString* errorMessage = nullptr);
    bool loadAnnotationsFromDisk(const QString& imagePath,
                                 const QSize& imageSize,
                                 QVector<Annotation>* annotations,
                                 AnnotationIO::SaveFormat* detectedFormat = nullptr,
                                 QString* errorMessage = nullptr) const;
    void restoreDatasetSettings();
    void saveDatasetSettings() const;
    QString datasetSettingsKey() const;
    void selectClassByIndex(int index);
    QColor colorForLabel(const QString& label) const;
    QColor defaultColorForLabel(const QString& label) const;
    void loadClassCatalog();
    void saveClassCatalog() const;
    void saveClassCatalogTo(const QString& folder) const;
    void addKnownLabelsFromOutput();
    AnnotationIO::SaveFormat currentFormat() const;
    QString currentImagePath() const;
    QVector<Annotation>& currentAnnotations();
    const QVector<Annotation>& currentAnnotations() const;

    AnnotationCanvas* canvas_ = nullptr;
    QListWidget* classesList_ = nullptr;
    QListWidget* labelsList_ = nullptr;
    QLabel* imageInfoLabel_ = nullptr;
    QLabel* outputInfoLabel_ = nullptr;
    QLabel* cursorInfoLabel_ = nullptr;
    QLabel* formatLabel_ = nullptr;
    QLabel* samStatusLabel_ = nullptr;

    QAction* openFolderAction_ = nullptr;
    QAction* outputFolderAction_ = nullptr;
    QAction* previousAction_ = nullptr;
    QAction* nextAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* drawAction_ = nullptr;
    QAction* aiPointAction_ = nullptr;
    QAction* acceptAiAction_ = nullptr;
    QAction* rejectAiAction_ = nullptr;
    QAction* fitAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* shortcutOverviewAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* undoPointAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* cancelAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QVector<QAction*> classShortcutActions_;
    QDialog* shortcutOverviewDialog_ = nullptr;

    QString imageFolder_;
    QString outputFolder_;
    AnnotationIO::SaveFormat annotationFormat_ = AnnotationIO::SaveFormat::VocXml;
    QStringList imagePaths_;
    int currentIndex_ = -1;
    QSize currentImageSize_;
    bool syncingListSelection_ = false;
    QHash<QString, QImage> imageCache_;
    QStringList imageCacheOrder_;

    QHash<QString, QVector<Annotation>> annotationsByImage_;
    QHash<QString, QVector<QVector<Annotation>>> undoByImage_;
    QHash<QString, QVector<QVector<Annotation>>> redoByImage_;
    QSet<QString> dirtyImages_;
    QSet<QString> loadFailedImages_;
    QStringList classNames_;
    QHash<QString, QColor> classColors_;
    bool syncingClassSelection_ = false;
    QString lastLabel_;
    QNetworkAccessManager* networkManager_ = nullptr;
    QNetworkReply* samReply_ = nullptr;
    QByteArray samPendingPayload_;
    bool samRequestPending_ = false;
    bool samRetryAfterServiceStart_ = false;
    bool samServiceSessionActive_ = false;
    int samPreparePendingCount_ = 0;
    bool hasSamProposal_ = false;
    QRectF samProposalRect_;
    QString samRequestImagePath_;
    QVector<QPointF> samPromptPoints_;
    QVector<int> samPromptLabels_;
};
