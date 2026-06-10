#include "MainWindow.h"

#include "AnnotationCanvas.h"
#include "ImageLoader.h"

#include <QAction>
#include <QColorDialog>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QInputDialog>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const QStringList kImageFilters = {
    "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.gif", "*.tif", "*.tiff"
};

bool containsImages(const QString& path)
{
    return !QDir(path).entryList(kImageFilters, QDir::Files, QDir::Name).isEmpty();
}

QString resolvedImageFolder(const QString& selectedPath)
{
    const QString cleanPath = QDir::cleanPath(selectedPath);
    if (containsImages(cleanPath)) {
        return cleanPath;
    }

    const QDir selectedDir(cleanPath);
    const QStringList children = selectedDir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);
    const QStringList aliases = {"image", "images", "JPEGImages"};
    for (const QString& alias : aliases) {
        for (const QString& child : children) {
            if (child.compare(alias, Qt::CaseInsensitive) == 0) {
                const QString candidate = selectedDir.filePath(child);
                if (containsImages(candidate)) {
                    return QDir::cleanPath(candidate);
                }
            }
        }
    }
    return cleanPath;
}

QString normalizedOutputRoot(QString path)
{
    return path.trimmed().isEmpty() ? QString() : QDir::cleanPath(path);
}

void appendFormat(QVector<AnnotationIO::SaveFormat>* formats,
                  AnnotationIO::SaveFormat format)
{
    if (!formats->contains(format)) {
        formats->append(format);
    }
}

QVector<AnnotationIO::SaveFormat> detectFormatsInLabelFolder(
    const QString& folder,
    const QStringList& imagePaths)
{
    QVector<AnnotationIO::SaveFormat> formats;
    const QDir labelsDir(folder);
    QFile metadataFile(labelsDir.filePath("annotaflow_labels.json"));
    if (metadataFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument metadata =
            QJsonDocument::fromJson(metadataFile.readAll());
        if (metadata.isObject()) {
            const int formatValue = metadata.object().value("format").toInt(-1);
            const auto savedFormat =
                static_cast<AnnotationIO::SaveFormat>(formatValue);
            if (AnnotationIO::supportedFormats().contains(savedFormat)) {
                appendFormat(&formats, savedFormat);
            }
        }
    }
    if (QFileInfo::exists(labelsDir.filePath("instances.json"))) {
        appendFormat(&formats, AnnotationIO::SaveFormat::CocoJson);
    }
    if (QFileInfo::exists(labelsDir.filePath("annotations.csv"))) {
        appendFormat(&formats, AnnotationIO::SaveFormat::Csv);
    }

    for (const QString& imagePath : imagePaths) {
        const QString baseName = QFileInfo(imagePath).completeBaseName();
        if (QFileInfo::exists(labelsDir.filePath(baseName + ".xml"))) {
            appendFormat(&formats, AnnotationIO::SaveFormat::VocXml);
        }
        if (QFileInfo::exists(labelsDir.filePath(baseName + ".json"))) {
            appendFormat(&formats, AnnotationIO::SaveFormat::LabelMeJson);
        }

        QFile txtFile(labelsDir.filePath(baseName + ".txt"));
        if (!txtFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream in(&txtFile);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const QStringList parts = line.simplified().split(' ', Qt::SkipEmptyParts);
            if (parts.size() == 5) {
                appendFormat(&formats, AnnotationIO::SaveFormat::Yolo);
            } else if (parts.size() >= 8) {
                appendFormat(&formats, AnnotationIO::SaveFormat::KittiTxt);
            }
            break;
        }
    }

    if (formats.isEmpty()) {
        for (const AnnotationIO::SaveFormat format : AnnotationIO::supportedFormats()) {
            for (const QString& imagePath : imagePaths) {
                if (AnnotationIO::exists(format, imagePath, folder)) {
                    appendFormat(&formats, format);
                    break;
                }
            }
        }
    }
    return formats;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    canvas_ = new AnnotationCanvas(this);
    setCentralWidget(canvas_);
    networkManager_ = new QNetworkAccessManager(this);

    createActions();
    createToolbar();
    createDock();

    connect(canvas_, &AnnotationCanvas::rectangleCreated, this, &MainWindow::addRectangle);
    connect(canvas_, &AnnotationCanvas::pointPromptCreated, this, &MainWindow::requestSamPrediction);
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

    QString error;
    if (!openDataset(dir, QString(), &error)) {
        QMessageBox::information(this, "AnnotaFlow", error);
        return;
    }
    if (outputFolder_.isEmpty()) {
        chooseOutputFolder();
    }
}

bool MainWindow::openDataset(const QString& imageFolder,
                             const QString& outputFolder,
                             QString* errorMessage)
{
    const QString actualImageFolder = resolvedImageFolder(imageFolder);
    QStringList files;
    QDirIterator it(actualImageFolder, kImageFilters, QDir::Files, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        files.append(QDir::toNativeSeparators(it.next()));
    }
    files.sort(Qt::CaseInsensitive);

    if (files.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "这个文件夹里没有找到支持的图片。";
        }
        return false;
    }

    imageFolder_ = actualImageFolder;
    imagePaths_ = files;
    outputFolder_.clear();
    annotationsByImage_.clear();
    undoByImage_.clear();
    dirtyImages_.clear();
    loadFailedImages_.clear();
    classNames_.clear();
    classColors_.clear();
    lastLabel_.clear();
    if (outputFolder.trimmed().isEmpty()) {
        restoreDatasetSettings();
        saveDatasetSettings();
    } else {
        outputFolder_ = normalizedOutputRoot(outputFolder);
        const QVector<AnnotationIO::SaveFormat> detected =
            detectFormatsInLabelFolder(outputFolder_, imagePaths_);
        if (detected.size() == 1) {
            annotationFormat_ = detected.first();
        } else if (!detected.isEmpty() && !detected.contains(annotationFormat_)) {
            annotationFormat_ = detected.first();
        }
        saveDatasetSettings();
    }
    if (!outputFolder_.isEmpty()) {
        loadClassCatalog();
        addKnownLabelsFromOutput();
    }
    preloadDatasetAnnotations();
    loadImageAt(0);
    if (!outputFolder_.isEmpty()) {
        saveClassCatalog();
    }
    return true;
}

void MainWindow::chooseOutputFolder()
{
    if (!outputFolder_.isEmpty() && !maybeSaveDirtyImages()) {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        "选择标签文件夹",
        outputFolder_.isEmpty() ? QFileInfo(imageFolder_).absolutePath() : outputFolder_);
    if (dir.isEmpty()) {
        return;
    }

    AnnotationIO::SaveFormat selectedFormat = annotationFormat_;
    if (!chooseFormatForLabelFolder(dir, &selectedFormat)) {
        return;
    }
    outputFolder_ = normalizedOutputRoot(dir);
    annotationFormat_ = selectedFormat;
    saveDatasetSettings();
    const QStringList cachedPaths = annotationsByImage_.keys();
    for (const QString& cachedPath : cachedPaths) {
        if (!dirtyImages_.contains(cachedPath)) {
            annotationsByImage_.remove(cachedPath);
        }
    }
    classNames_.clear();
    classColors_.clear();
    lastLabel_.clear();
    loadClassCatalog();
    addKnownLabelsFromOutput();
    const int loadedCount = preloadDatasetAnnotations();
    if (currentIndex_ >= 0) {
        loadImageAt(currentIndex_);
    }

    refreshClassList();
    refreshWindowState();
    refreshActionState();
    statusBar()->showMessage(
        QString("标签目录已设置为 %1，格式为 %2；加载 %3 个已有标注框")
            .arg(QDir::toNativeSeparators(outputFolder_))
            .arg(AnnotationIO::formatDisplayName(currentFormat()))
            .arg(loadedCount),
        5000);
}

bool MainWindow::chooseFormatForLabelFolder(
    const QString& folder,
    AnnotationIO::SaveFormat* selectedFormat)
{
    if (!selectedFormat) {
        return false;
    }

    const QVector<AnnotationIO::SaveFormat> detected =
        detectFormatsInLabelFolder(folder, imagePaths_);
    if (detected.size() == 1) {
        *selectedFormat = detected.first();
        return true;
    }

    const QVector<AnnotationIO::SaveFormat> choices =
        detected.isEmpty() ? AnnotationIO::supportedFormats() : detected;
    QStringList names;
    int currentIndex = 0;
    for (int i = 0; i < choices.size(); ++i) {
        names.append(AnnotationIO::formatDisplayName(choices[i]));
        if (choices[i] == *selectedFormat) {
            currentIndex = i;
        }
    }

    bool accepted = false;
    const QString title = detected.isEmpty() ? "设置标签格式" : "确认标签格式";
    const QString prompt = detected.isEmpty()
        ? "目录中没有可识别的标注文件，请选择要使用的格式："
        : "目录中检测到多种标注格式，请选择本次要使用的格式：";
    const QString selectedName = QInputDialog::getItem(
        this,
        title,
        prompt,
        names,
        currentIndex,
        false,
        &accepted);
    if (!accepted) {
        return false;
    }

    const int selectedIndex = names.indexOf(selectedName);
    if (selectedIndex < 0) {
        return false;
    }
    *selectedFormat = choices[selectedIndex];
    return true;
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
    if (loadFailedImages_.contains(currentImagePath())) {
        QMessageBox::warning(
            this,
            "已阻止保存",
            "当前图片的历史标注读取失败。为避免覆盖原标注文件，本次不会保存，请先检查标注格式或目录。");
        return;
    }

    QString error;
    if (!saveActiveFormat(AnnotationIO::isDatasetLevelFormat(currentFormat()), &error)) {
        QMessageBox::warning(this, "保存失败", error);
        return;
    }

    if (AnnotationIO::isDatasetLevelFormat(currentFormat())) {
        dirtyImages_.clear();
    } else {
        dirtyImages_.remove(currentImagePath());
    }
    saveClassCatalog();
    statusBar()->showMessage(QString("已保存 %1 标注").arg(AnnotationIO::formatDisplayName(currentFormat())), 3000);
    refreshWindowState();
}

void MainWindow::setDrawMode()
{
    rejectSamProposal();
    canvas_->setMode(AnnotationCanvas::Mode::DrawBox);
    statusBar()->showMessage("画框模式：在图片上拖拽创建标注框，按 Q 退出。", 3000);
    refreshActionState();
}

void MainWindow::setAiPointMode()
{
    if (currentIndex_ < 0) {
        return;
    }
    canvas_->setMode(AnnotationCanvas::Mode::AiPoint);
    scheduleSamPrepare(currentImagePath(), true, 0);
    scheduleSamPrepare(currentImagePath(), false, 1500);
    statusBar()->showMessage("AI 点选模式：左键加目标点，右键加排除点；绿色边缘稳定后按 R 接受。", 6000);
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
    const QString label = lastLabel_.trimmed();
    if (label.isEmpty() || !classNames_.contains(label)) {
        QMessageBox::information(
            this,
            "请先选择标签",
            "当前还没有可用标签。请先在右侧“数据集标签”区域新增标签，或选择已有标签。");
        canvas_->setMode(AnnotationCanvas::Mode::DrawBox);
        return;
    }

    pushUndoState();

    ensureClassExists(label);
    Annotation annotation;
    annotation.rect = rect.normalized();
    annotation.label = label;
    currentAnnotations().append(annotation);
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(currentAnnotations().size() - 1);
    canvas_->repaint();
    markCurrentDirty();
    refreshClassList();
    refreshLabels();
    canvas_->setMode(AnnotationCanvas::Mode::DrawBox);
    statusBar()->showMessage(QString("已添加标注，标签沿用：%1。不同的话可在右侧列表双击选择已有标签。").arg(label), 3500);
    refreshActionState();
    labelsList_->viewport()->repaint();
    statusBar()->repaint();
    autoSaveCurrentAnnotations();
}

void MainWindow::requestSamPrediction(const QPointF& imagePoint, int pointLabel)
{
    if (currentIndex_ < 0) {
        return;
    }
    if (!hasUsableCurrentLabel()) {
        QMessageBox::information(
            this,
            "请先选择标签",
            "使用 AI 点选前，请先在右侧“数据集标签”里新增或选择一个标签。");
        canvas_->clearPromptPoint();
        samPromptPoints_.clear();
        samPromptLabels_.clear();
        return;
    }
    if (samRequestPending_) {
        statusBar()->showMessage("SAM2 正在推理上一点，请稍等。", 2000);
        return;
    }

    samPromptPoints_.append(imagePoint);
    samPromptLabels_.append(pointLabel == 0 ? 0 : 1);
    canvas_->setPromptPoints(samPromptPoints_, samPromptLabels_);
    samRequestPending_ = true;
    samRequestImagePath_ = currentImagePath();

    QJsonObject payload;
    payload.insert("image_path", samRequestImagePath_);
    QJsonArray point;
    point.append(imagePoint.x());
    point.append(imagePoint.y());
    payload.insert("point", point);
    payload.insert("label", pointLabel == 0 ? 0 : 1);
    QJsonArray points;
    QJsonArray labels;
    for (int i = 0; i < samPromptPoints_.size(); ++i) {
        QJsonArray item;
        item.append(samPromptPoints_[i].x());
        item.append(samPromptPoints_[i].y());
        points.append(item);
        labels.append(samPromptLabels_.value(i, 1));
    }
    payload.insert("points", points);
    payload.insert("point_labels", labels);

    samPendingPayload_ = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    samRetryAfterServiceStart_ = false;
    postSamPrediction(samPendingPayload_);
    statusBar()->showMessage("SAM2 推理中...", 2000);
    refreshActionState();
}

void MainWindow::handleSamPrediction(QNetworkReply* reply)
{
    if (!reply) {
        return;
    }
    const bool isCurrentReply = reply == samReply_;
    if (isCurrentReply) {
        samReply_ = nullptr;
    }
    reply->deleteLater();

    if (!isCurrentReply) {
        return;
    }
    samRequestPending_ = false;
    auto rollbackLastPrompt = [this]() {
        if (!samPromptPoints_.isEmpty()) {
            samPromptPoints_.removeLast();
        }
        if (!samPromptLabels_.isEmpty()) {
            samPromptLabels_.removeLast();
        }
        canvas_->setPromptPoints(samPromptPoints_, samPromptLabels_);
    };
    auto clearFailedRequest = [this]() {
        samPendingPayload_.clear();
        samRetryAfterServiceStart_ = false;
    };

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const bool canAutoStart =
            reply->error() == QNetworkReply::ConnectionRefusedError &&
            !samRetryAfterServiceStart_ &&
            !samPendingPayload_.isEmpty();
        if (canAutoStart && startSamService()) {
            samRetryAfterServiceStart_ = true;
            samRequestPending_ = true;
            statusBar()->showMessage("SAM2 服务未运行，正在自动启动并重试本次点选...", 8000);
            QTimer::singleShot(6000, this, &MainWindow::retrySamPredictionAfterServiceStart);
            refreshActionState();
            return;
        }
        rollbackLastPrompt();
        clearFailedRequest();
        statusBar()->showMessage(
            QString("SAM2 服务不可用：%1。已尝试自动启动；仍失败时请运行 D:\\AnnotaFlow\\Run-AnnotaFlow.bat")
                .arg(reply->errorString()),
            7000);
        refreshActionState();
        return;
    }
    if (currentImagePath().compare(samRequestImagePath_, Qt::CaseInsensitive) != 0) {
        rejectSamProposal();
        statusBar()->showMessage("SAM2 返回时图片已切换，本次候选框已忽略。", 3000);
        refreshActionState();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        rollbackLastPrompt();
        clearFailedRequest();
        statusBar()->showMessage(QString("SAM2 返回格式无法解析：%1").arg(parseError.errorString()), 5000);
        refreshActionState();
        return;
    }

    const QJsonObject object = document.object();
    if (!object.value("ok").toBool(true)) {
        rollbackLastPrompt();
        clearFailedRequest();
        statusBar()->showMessage(QString("SAM2 推理失败：%1").arg(object.value("error").toString("未知错误")), 7000);
        refreshActionState();
        return;
    }

    const QJsonArray bbox = object.value("bbox").toArray();
    if (bbox.size() != 4) {
        rollbackLastPrompt();
        clearFailedRequest();
        statusBar()->showMessage("SAM2 返回中没有有效 bbox。", 5000);
        refreshActionState();
        return;
    }

    QRectF rect;
    const QString bboxFormat = object.value("bbox_format").toString("xywh").toLower();
    if (bboxFormat == "xyxy") {
        const double x1 = bbox[0].toDouble();
        const double y1 = bbox[1].toDouble();
        rect = QRectF(QPointF(x1, y1), QPointF(bbox[2].toDouble(), bbox[3].toDouble())).normalized();
    } else {
        rect = QRectF(bbox[0].toDouble(), bbox[1].toDouble(), bbox[2].toDouble(), bbox[3].toDouble()).normalized();
    }
    rect = rect.intersected(QRectF(0, 0, currentImageSize_.width(), currentImageSize_.height()));
    if (rect.width() < 2 || rect.height() < 2) {
        rollbackLastPrompt();
        clearFailedRequest();
        statusBar()->showMessage("SAM2 返回的候选框太小，已忽略。", 5000);
        refreshActionState();
        return;
    }

    samProposalRect_ = rect;
    hasSamProposal_ = true;
    samPendingPayload_.clear();
    samRetryAfterServiceStart_ = false;
    canvas_->setProposalRect(rect);
    QVector<QVector<QPointF>> contours;
    const QJsonArray contourArray = object.value("contours").toArray();
    for (const QJsonValue& contourValue : contourArray) {
        QVector<QPointF> contour;
        const QJsonArray contourPoints = contourValue.toArray();
        for (const QJsonValue& pointValue : contourPoints) {
            const QJsonArray pointArray = pointValue.toArray();
            if (pointArray.size() == 2) {
                contour.append(QPointF(pointArray[0].toDouble(), pointArray[1].toDouble()));
            }
        }
        if (contour.size() >= 2) {
            contours.append(contour);
        }
    }
    canvas_->setProposalContours(contours);
    const QString scoreText = object.contains("score")
        ? QString("，score=%1").arg(object.value("score").toDouble(), 0, 'f', 3)
        : QString();
    statusBar()->showMessage(QString("已更新 AI 候选框%1。可继续点选修正，按 R 接受为“%2”，按 Q/Esc 取消。")
                                 .arg(scoreText, lastLabel_),
                             7000);
    refreshActionState();
}

void MainWindow::retrySamPredictionAfterServiceStart()
{
    if (!samRequestPending_ || samPendingPayload_.isEmpty()) {
        return;
    }
    postSamPrediction(samPendingPayload_);
    statusBar()->showMessage("正在重试 SAM2 点选请求...", 3000);
}

bool MainWindow::startSamService()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    QString launcherPath = QDir::cleanPath(appDir.absoluteFilePath("../sam2-service/start_hidden.py"));
    if (!QFileInfo::exists(launcherPath)) {
        launcherPath = QStringLiteral("D:/AnnotaFlow/sam2-service/start_hidden.py");
    }
    if (!QFileInfo::exists(launcherPath)) {
        return false;
    }

    QString pythonw = QStringLiteral("D:/anaconda2025.06-1/pythonw.exe");
    if (!QFileInfo::exists(pythonw)) {
        pythonw = QStringLiteral("pythonw.exe");
    }
    return QProcess::startDetached(
        pythonw,
        QStringList{QDir::toNativeSeparators(launcherPath)},
        QFileInfo(launcherPath).absolutePath());
}

void MainWindow::postSamPrepare(const QString& imagePath)
{
    if (imagePath.isEmpty()) {
        return;
    }

    QJsonObject payload;
    payload.insert("image_path", imagePath);

    QNetworkRequest request(QUrl("http://127.0.0.1:8765/prepare"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply =
        networkManager_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        reply->deleteLater();
    });
}

void MainWindow::scheduleSamPrepare(const QString& imagePath, bool ensureServiceStart, int delayMs)
{
    if (imagePath.isEmpty()) {
        return;
    }
    if (ensureServiceStart) {
        startSamService();
    }

    const QString scheduledPath = imagePath;
    QTimer::singleShot(std::max(0, delayMs), this, [this, scheduledPath]() {
        if (scheduledPath.isEmpty()) {
            return;
        }
        postSamPrepare(scheduledPath);
    });
}

void MainWindow::postSamPrediction(const QByteArray& payload)
{
    if (payload.isEmpty()) {
        return;
    }
    samRequestPending_ = true;
    QNetworkRequest request(QUrl("http://127.0.0.1:8765/predict"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    samReply_ = networkManager_->post(request, payload);
    connect(samReply_, &QNetworkReply::finished, this, [this]() {
        handleSamPrediction(qobject_cast<QNetworkReply*>(sender()));
    });
    refreshActionState();
}

void MainWindow::acceptSamProposal()
{
    if (!hasSamProposal_) {
        return;
    }
    if (!hasUsableCurrentLabel()) {
        QMessageBox::information(
            this,
            "请先选择标签",
            "接受 AI 候选框前，请先在右侧“数据集标签”里选择一个已有标签。");
        return;
    }

    const QRectF rect = samProposalRect_;
    hasSamProposal_ = false;
    samPromptPoints_.clear();
    samPromptLabels_.clear();
    canvas_->clearPromptPoint();
    canvas_->clearProposalRect();
    addRectangle(rect);
    if (currentIndex_ >= 0) {
        canvas_->setMode(AnnotationCanvas::Mode::AiPoint);
        statusBar()->showMessage(QString("已接受 AI 候选框，标签：%1。继续点击下一个目标。").arg(lastLabel_), 3000);
    }
    refreshActionState();
}

void MainWindow::rejectSamProposal()
{
    if (samReply_) {
        QNetworkReply* reply = samReply_;
        samReply_ = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    samRequestPending_ = false;
    samRetryAfterServiceStart_ = false;
    samPendingPayload_.clear();
    hasSamProposal_ = false;
    samPromptPoints_.clear();
    samPromptLabels_.clear();
    canvas_->clearPromptPoint();
    canvas_->clearProposalRect();
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
    autoSaveCurrentAnnotations();
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
    if (hasSamProposal_ || samRequestPending_) {
        rejectSamProposal();
        statusBar()->showMessage("已取消 AI 候选框。", 2000);
        return;
    }
    if (!samPromptPoints_.isEmpty()) {
        rejectSamProposal();
        statusBar()->showMessage("已清空 AI 采样点。", 2000);
        return;
    }
    if (canvas_->mode() == AnnotationCanvas::Mode::DrawBox || canvas_->mode() == AnnotationCanvas::Mode::AiPoint) {
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

void MainWindow::addClassFromCatalog()
{
    const QString label = promptForLabelName("新增数据集标签");
    if (label.isEmpty()) {
        return;
    }

    if (classNames_.contains(label)) {
        selectClassByIndex(classNames_.indexOf(label));
        QMessageBox::information(this, "标签已存在", QString("标签“%1”已经存在，已切换为当前类别。").arg(label));
        return;
    }

    ensureClassExists(label);
    lastLabel_ = label;
    refreshClassList();
    refreshActionState();
    saveClassCatalog();
    statusBar()->showMessage(QString("已新增标签：%1").arg(label), 2000);
}

void MainWindow::editSelectedAnnotationLabel()
{
    const int row = labelsList_->currentRow();
    if (row < 0 || row >= currentAnnotations().size()) {
        QMessageBox::information(this, "请选择标注", "请先在“当前图片标注”列表中选择一个标注。");
        return;
    }

    if (classNames_.isEmpty()) {
        QMessageBox::information(
            this,
            "没有可选标签",
            "数据集还没有标签，请先在上方“数据集标签”区域点击“新增标签”。");
        return;
    }

    const QString currentLabel = currentAnnotations()[row].label;
    const int currentClassIndex = qMax(0, classNames_.indexOf(currentLabel));
    bool accepted = false;
    const QString selectedLabel = QInputDialog::getItem(
        this,
        "修改标注标签",
        "选择已有标签：",
        classNames_,
        currentClassIndex,
        false,
        &accepted);
    if (!accepted || selectedLabel.isEmpty() || selectedLabel == currentLabel) {
        return;
    }

    pushUndoState();
    currentAnnotations()[row].label = selectedLabel;
    lastLabel_ = selectedLabel;
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(row);
    markCurrentDirty();
    refreshLabels();
    refreshClassList();
    selectClassByIndex(classNames_.indexOf(selectedLabel));
    statusBar()->showMessage(QString("已将标注标签改为：%1").arg(selectedLabel), 2000);
    autoSaveCurrentAnnotations();
}

void MainWindow::renameSelectedClass()
{
    const int index = classesList_->currentRow();
    if (index < 0 || index >= classNames_.size()) {
        QMessageBox::information(this, "请选择标签", "请先选择要重命名的标签。");
        return;
    }
    if (!loadFailedImages_.isEmpty()) {
        QMessageBox::warning(
            this,
            "无法安全重命名",
            QString("有 %1 张图片的历史标注读取失败，请先解决加载错误。")
                .arg(loadFailedImages_.size()));
        return;
    }

    const QString oldName = classNames_[index];
    const QString newName = promptForLabelName("重命名标签", oldName);
    if (newName.isEmpty() || newName == oldName) {
        return;
    }

    const int existingIndex = classNames_.indexOf(newName);
    if (existingIndex >= 0) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            "合并标签",
            QString("标签“%1”已经存在。是否把“%2”的所有标注合并到“%1”？")
                .arg(newName, oldName),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    for (const QString& imagePath : imagePaths_) {
        QVector<Annotation>& annotations = annotationsByImage_[imagePath];
        bool changed = false;
        for (Annotation& annotation : annotations) {
            if (annotation.label.trimmed() == oldName) {
                annotation.label = newName;
                changed = true;
            }
        }
        if (changed) {
            dirtyImages_.insert(imagePath);
        }
    }

    const QColor oldColor = classColors_.value(oldName);
    classColors_.remove(oldName);
    if (existingIndex >= 0) {
        classNames_.removeAt(index);
    } else {
        classNames_[index] = newName;
        if (oldColor.isValid()) {
            classColors_[newName] = oldColor;
        }
    }
    if (lastLabel_ == oldName) {
        lastLabel_ = newName;
    }

    persistDatasetAfterClassDeletion();
    saveClassCatalog();
    canvas_->setLabelColors(classColors_);
    canvas_->setAnnotations(currentAnnotations());
    refreshClassList();
    refreshLabels();
    refreshWindowState();
    statusBar()->showMessage(QString("已将标签“%1”重命名为“%2”").arg(oldName, newName), 3000);
}

void MainWindow::deleteSelectedClass()
{
    const int index = classesList_->currentRow();
    if (index < 0 || index >= classNames_.size()) {
        QMessageBox::information(this, "请选择标签", "请先在数据集标签列表中选择要删除的标签。");
        return;
    }

    const QString label = classNames_[index];
    if (!loadFailedImages_.isEmpty()) {
        QMessageBox::warning(
            this,
            "无法安全统计",
            QString("有 %1 张图片的历史标注读取失败，因此无法准确统计或删除标签。请先解决加载错误。")
                .arg(loadFailedImages_.size()));
        return;
    }
    const int currentImageCount = countCurrentImageAnnotationsForClass(label);
    const int count = countAnnotationsForClass(label);

    QMessageBox box(this);
    box.setIcon(count > 0 ? QMessageBox::Warning : QMessageBox::Information);
    box.setWindowTitle("确认删除标签");
    if (count > 0) {
        box.setText(QString("标签“%1”在当前图片有 %2 个标注，整个数据集共 %3 个标注。\n\n删除标签会同时删除这些标注，请谨慎操作。")
                        .arg(label)
                        .arg(currentImageCount)
                        .arg(count));
    } else {
        box.setText(QString("标签“%1”在当前图片有 0 个标注，整个数据集共 0 个标注。\n\n确认从数据集标签中删除吗？").arg(label));
    }

    QPushButton* deleteButton = box.addButton("确认删除", QMessageBox::DestructiveRole);
    QPushButton* cancelButton = box.addButton("取消", QMessageBox::RejectRole);
    box.setDefaultButton(cancelButton);
    box.exec();

    if (box.clickedButton() != deleteButton) {
        return;
    }

    removeAnnotationsForClass(label);
    classNames_.removeAt(index);
    classColors_.remove(label);
    if (lastLabel_ == label) {
        lastLabel_ = classNames_.isEmpty() ? QString() : classNames_.value(qMin(index, classNames_.size() - 1));
    }

    persistDatasetAfterClassDeletion();
    canvas_->setLabelColors(classColors_);
    canvas_->setAnnotations(currentAnnotations());
    canvas_->setSelectedIndex(-1);
    refreshClassList();
    refreshLabels();
    refreshActionState();
    saveClassCatalog();
    statusBar()->showMessage(QString("已删除标签：%1").arg(label), 2500);
}

void MainWindow::saveAsAnnotationFormat()
{
    if (imagePaths_.isEmpty()) {
        return;
    }
    if (!loadFailedImages_.isEmpty()) {
        QMessageBox::warning(
            this,
            "无法完整另存为",
            QString("有 %1 张图片的原标注读取失败，请先解决这些错误。")
                .arg(loadFailedImages_.size()));
        return;
    }

    QStringList formatNames;
    QVector<AnnotationIO::SaveFormat> formats = AnnotationIO::supportedFormats();
    int currentIndex = 0;
    for (int i = 0; i < formats.size(); ++i) {
        formatNames.append(AnnotationIO::formatDisplayName(formats[i]));
        if (formats[i] == currentFormat()) {
            currentIndex = i;
        }
    }
    bool accepted = false;
    const QString selectedName = QInputDialog::getItem(
        this,
        "另存为标注格式",
        "目标格式：",
        formatNames,
        currentIndex,
        false,
        &accepted);
    if (!accepted) {
        return;
    }
    const int formatIndex = formatNames.indexOf(selectedName);
    if (formatIndex < 0) {
        return;
    }
    const AnnotationIO::SaveFormat targetFormat = formats[formatIndex];

    const QString targetFolder = QFileDialog::getExistingDirectory(
        this,
        QString("选择 %1 标签文件夹")
            .arg(AnnotationIO::formatDisplayName(targetFormat)),
        outputFolder_.isEmpty() ? QFileInfo(imageFolder_).absolutePath() : outputFolder_);
    if (targetFolder.isEmpty()) {
        return;
    }
    const QString normalizedTarget = QDir::cleanPath(targetFolder);
    if (!outputFolder_.isEmpty() &&
        normalizedTarget.compare(QDir::cleanPath(outputFolder_), Qt::CaseInsensitive) == 0 &&
        targetFormat != currentFormat()) {
        QMessageBox::warning(
            this,
            "请选择其他标签目录",
            "不同格式不能另存到当前标签目录，否则同名文件可能互相覆盖。");
        return;
    }

    const QDir targetDir(normalizedTarget);
    if (!targetDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this,
            "目标标签目录已有文件",
            QString("目录中已有内容：\n%1\n\n继续会覆盖同名标注文件，但不会删除其他文件。")
                .arg(QDir::toNativeSeparators(normalizedTarget)),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    QString error;
    if (!AnnotationIO::saveDataset(
            targetFormat,
            imagePaths_,
            normalizedTarget,
            annotationsByImage_,
            classNames_,
            &error)) {
        QMessageBox::warning(this, "另存为失败", error);
        return;
    }
    outputFolder_ = normalizedTarget;
    annotationFormat_ = targetFormat;
    dirtyImages_.clear();
    saveClassCatalog();
    saveDatasetSettings();
    refreshWindowState();
    refreshActionState();

    QMessageBox::information(
        this,
        "另存为完成",
        QString("已将 %1 张图片的标注另存为 %2。\n\n当前标签文件夹已切换为：\n%3")
            .arg(imagePaths_.size())
            .arg(AnnotationIO::formatDisplayName(targetFormat))
            .arg(QDir::toNativeSeparators(normalizedTarget)));
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

    outputFolderAction_ = new QAction("选择标签文件夹", this);
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

    saveAsAction_ = new QAction("另存为", this);
    saveAsAction_->setObjectName("saveAsFormatAction");
    saveAsAction_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveAsAnnotationFormat);

    drawAction_ = new QAction("画框", this);
    drawAction_->setShortcut(Qt::Key_W);
    connect(drawAction_, &QAction::triggered, this, &MainWindow::setDrawMode);

    aiPointAction_ = new QAction("AI点选", this);
    aiPointAction_->setObjectName("aiPointAction");
    aiPointAction_->setShortcut(Qt::Key_E);
    connect(aiPointAction_, &QAction::triggered, this, &MainWindow::setAiPointMode);

    acceptAiAction_ = new QAction("接受候选框", this);
    acceptAiAction_->setObjectName("acceptAiProposalAction");
    QList<QKeySequence> acceptShortcuts;
    acceptShortcuts << QKeySequence(Qt::Key_R) << QKeySequence(Qt::Key_Return) << QKeySequence(Qt::Key_Enter);
    acceptAiAction_->setShortcuts(acceptShortcuts);
    connect(acceptAiAction_, &QAction::triggered, this, &MainWindow::acceptSamProposal);
    addAction(acceptAiAction_);

    rejectAiAction_ = new QAction("取消候选框", this);
    rejectAiAction_->setObjectName("rejectAiProposalAction");
    rejectAiAction_->setShortcut(Qt::Key_Escape);
    connect(rejectAiAction_, &QAction::triggered, this, [this]() {
        rejectSamProposal();
        statusBar()->showMessage("已取消 AI 候选框。", 2000);
    });
    addAction(rejectAiAction_);

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
    fileMenu->addAction(saveAsAction_);

    QMenu* editMenu = menuBar()->addMenu("编辑");
    editMenu->addAction(drawAction_);
    editMenu->addAction(aiPointAction_);
    editMenu->addAction(acceptAiAction_);
    editMenu->addAction(rejectAiAction_);
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
    toolbar->addAction(aiPointAction_);
    toolbar->addAction(acceptAiAction_);
    toolbar->addAction(deleteAction_);
    toolbar->addAction(undoAction_);
    toolbar->addSeparator();
    toolbar->addAction(saveAction_);
    toolbar->addAction(saveAsAction_);
    toolbar->addSeparator();
    toolbar->addAction(fitAction_);
    toolbar->addAction(zoomInAction_);
    toolbar->addAction(zoomOutAction_);
    toolbar->addSeparator();

    formatLabel_ = new QLabel(toolbar);
    formatLabel_->setObjectName("annotationFormatLabel");
    formatLabel_->setToolTip("当前格式由标签文件夹决定；选择其他标签文件夹可切换已有版本，使用“另存为”生成新格式。");
    toolbar->addWidget(formatLabel_);
}

void MainWindow::createDock()
{
    QDockWidget* dock = new QDockWidget("标注列表", this);
    QWidget* panel = new QWidget(dock);
    QVBoxLayout* layout = new QVBoxLayout(panel);

    imageInfoLabel_ = new QLabel("未打开图片文件夹", panel);
    imageInfoLabel_->setWordWrap(true);
    outputInfoLabel_ = new QLabel("标签文件夹：未选择", panel);
    outputInfoLabel_->setWordWrap(true);
    cursorInfoLabel_ = new QLabel("x: -, y: -", panel);

    QLabel* classesTitle = new QLabel("数据集标签（双击改色，Ctrl+1..9/0 选择）", panel);
    classesTitle->setWordWrap(true);
    classesList_ = new QListWidget(panel);
    classesList_->setObjectName("datasetClassesList");
    classesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    classesList_->setMaximumHeight(190);
    QHBoxLayout* classButtons = new QHBoxLayout();
    QPushButton* addClassButton = new QPushButton("新增标签", panel);
    QPushButton* renameClassButton = new QPushButton("重命名", panel);
    QPushButton* deleteClassButton = new QPushButton("删除标签", panel);
    classButtons->addWidget(addClassButton);
    classButtons->addWidget(renameClassButton);
    classButtons->addWidget(deleteClassButton);

    QLabel* labelsTitle = new QLabel("当前图片标注（双击选择已有标签）", panel);
    labelsTitle->setWordWrap(true);
    labelsList_ = new QListWidget(panel);
    labelsList_->setObjectName("currentAnnotationsList");
    labelsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    labelsList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QPushButton* editAnnotationLabelButton = new QPushButton("修改为已有标签", panel);

    layout->addWidget(imageInfoLabel_);
    layout->addWidget(outputInfoLabel_);
    layout->addWidget(cursorInfoLabel_);
    layout->addWidget(classesTitle);
    layout->addWidget(classesList_);
    layout->addLayout(classButtons);
    layout->addWidget(labelsTitle);
    layout->addWidget(labelsList_, 1);
    layout->addWidget(editAnnotationLabelButton);
    panel->setLayout(layout);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(classesList_, &QListWidget::currentRowChanged, this, &MainWindow::onClassSelectionChanged);
    connect(classesList_, &QListWidget::itemDoubleClicked, this, &MainWindow::chooseClassColor);
    connect(addClassButton, &QPushButton::clicked, this, &MainWindow::addClassFromCatalog);
    connect(renameClassButton, &QPushButton::clicked, this, &MainWindow::renameSelectedClass);
    connect(deleteClassButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedClass);
    connect(labelsList_, &QListWidget::currentRowChanged, this, &MainWindow::onListSelectionChanged);
    connect(labelsList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        editSelectedAnnotationLabel();
    });
    connect(editAnnotationLabelButton, &QPushButton::clicked, this, &MainWindow::editSelectedAnnotationLabel);
}

void MainWindow::loadImageAt(int index)
{
    if (index < 0 || index >= imagePaths_.size()) {
        return;
    }
    rejectSamProposal();

    QImage image;
    QString error;
    const QString path = imagePaths_[index];
    if (!ImageLoader::loadImage(path, &image, &error)) {
        QMessageBox::warning(this, "无法打开图片", QString("%1\n\n%2").arg(path, error));
        return;
    }

    currentIndex_ = index;
    currentImageSize_ = image.size();

    if (!dirtyImages_.contains(path) && !annotationsByImage_.contains(path)) {
        QVector<Annotation> loaded;
        AnnotationIO::SaveFormat detectedFormat = currentFormat();
        QString loadError;
        if (!loadAnnotationsFromDisk(path, currentImageSize_, &loaded, &detectedFormat, &loadError)) {
            loadFailedImages_.insert(path);
            statusBar()->showMessage(QString("读取已有标注失败：%1").arg(loadError), 6000);
            if (!annotationsByImage_.contains(path)) {
                annotationsByImage_[path] = {};
            }
        } else {
            loadFailedImages_.remove(path);
            annotationsByImage_[path] = loaded;
        }
        if (!loaded.isEmpty()) {
            statusBar()->showMessage(
                QString("已加载 %1 个已有标注框（%2）")
                    .arg(loaded.size())
                    .arg(AnnotationIO::formatDisplayName(detectedFormat)),
                3500);
        }
    } else if (!annotationsByImage_.contains(path)) {
        annotationsByImage_[path] = {};
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
    if (canvas_->mode() == AnnotationCanvas::Mode::AiPoint) {
        scheduleSamPrepare(path, false, 0);
    }
}

void MainWindow::refreshLabels()
{
    syncingListSelection_ = true;
    labelsList_->clear();
    const QVector<Annotation>& annotations = currentAnnotations();
    for (int i = 0; i < annotations.size(); ++i) {
        QListWidgetItem* item = new QListWidgetItem(annotations[i].label);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
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
    setWindowTitle(QString("%1%2 - AnnotaFlow 0.4.1").arg(dirty ? "*" : "", titlePath));

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
                                  ? "标签文件夹：未选择"
                                  : QString("标签文件夹：%1\n标签格式：%2")
                                        .arg(QDir::toNativeSeparators(outputFolder_))
                                        .arg(AnnotationIO::formatDisplayName(currentFormat())));
    if (formatLabel_) {
        formatLabel_->setText(
            QString("标签格式：%1")
                .arg(outputFolder_.isEmpty()
                         ? QString("未确定")
                         : AnnotationIO::formatDisplayName(currentFormat())));
    }
}

void MainWindow::refreshActionState()
{
    const bool hasImage = currentIndex_ >= 0;
    previousAction_->setEnabled(hasImage && currentIndex_ > 0);
    nextAction_->setEnabled(hasImage && currentIndex_ < imagePaths_.size() - 1);
    saveAction_->setEnabled(hasImage);
    saveAsAction_->setEnabled(hasImage);
    drawAction_->setEnabled(hasImage);
    aiPointAction_->setEnabled(hasImage);
    acceptAiAction_->setEnabled(hasImage && hasSamProposal_ && !samRequestPending_);
    rejectAiAction_->setEnabled(hasImage && (hasSamProposal_ || samRequestPending_ || !samPromptPoints_.isEmpty()));
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

void MainWindow::autoSaveCurrentAnnotations()
{
    if (currentIndex_ < 0 || outputFolder_.isEmpty()) {
        return;
    }
    if (loadFailedImages_.contains(currentImagePath())) {
        statusBar()->showMessage("当前图片的历史标注读取失败，已阻止自动保存以保护原文件。", 6000);
        return;
    }

    QString error;
    const bool wholeDataset = AnnotationIO::isDatasetLevelFormat(currentFormat());
    if (saveActiveFormat(wholeDataset, &error)) {
        if (wholeDataset) {
            dirtyImages_.clear();
        } else {
            dirtyImages_.remove(currentImagePath());
        }
        saveClassCatalog();
        refreshWindowState();
    } else {
        statusBar()->showMessage(QString("自动保存失败：%1").arg(error), 4000);
    }
}

bool MainWindow::hasUsableCurrentLabel() const
{
    const QString label = lastLabel_.trimmed();
    return !label.isEmpty() && classNames_.contains(label);
}

int MainWindow::preloadDatasetAnnotations()
{
    int totalAnnotations = 0;
    for (const QString& imagePath : imagePaths_) {
        if (dirtyImages_.contains(imagePath)) {
            totalAnnotations += annotationsByImage_.value(imagePath).size();
            continue;
        }

        QImageReader reader(imagePath);
        const QSize imageSize = reader.size();
        if (imageSize.isEmpty()) {
            continue;
        }

        QVector<Annotation> loaded;
        QString loadError;
        if (!loadAnnotationsFromDisk(imagePath, imageSize, &loaded, nullptr, &loadError)) {
            loadFailedImages_.insert(imagePath);
            continue;
        }

        loadFailedImages_.remove(imagePath);
        annotationsByImage_[imagePath] = loaded;
        totalAnnotations += loaded.size();
        for (const Annotation& annotation : loaded) {
            ensureClassExists(annotation.label);
        }
    }

    refreshClassList();
    return totalAnnotations;
}

bool MainWindow::saveActiveFormat(bool wholeDataset, QString* errorMessage)
{
    if (outputFolder_.isEmpty() || currentIndex_ < 0) {
        if (errorMessage) {
            *errorMessage = "尚未选择图片或标签文件夹。";
        }
        return false;
    }

    if (wholeDataset) {
        return AnnotationIO::saveDataset(
            currentFormat(),
            imagePaths_,
            outputFolder_,
            annotationsByImage_,
            classNames_,
            errorMessage);
    }

    return AnnotationIO::saveImage(
        currentFormat(),
        currentImagePath(),
        outputFolder_,
        currentImageSize_,
        currentAnnotations(),
        classNames_,
        errorMessage);
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
    return QString("第 %1 个标注\n标签：%2\n位置：x=%3, y=%4, w=%5, h=%6\n双击可选择已有标签")
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
    return QString("类别：%1\n全数据集标注：%2 个\n快捷键：%3\n双击选择颜色")
        .arg(classNames_[index])
        .arg(countAnnotationsForClass(classNames_[index]))
        .arg(shortcut);
}

QString MainWindow::promptForLabelName(const QString& title, const QString& initialText)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.resize(340, 125);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* prompt = new QLabel("标签名称：", &dialog);
    QLineEdit* edit = new QLineEdit(&dialog);
    edit->setText(initialText);
    edit->selectAll();

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton* confirmButton = new QPushButton("确定", &dialog);
    QPushButton* cancelButton = new QPushButton("取消", &dialog);
    buttons->addWidget(confirmButton);
    buttons->addWidget(cancelButton);

    layout->addWidget(prompt);
    layout->addWidget(edit);
    layout->addLayout(buttons);

    connect(confirmButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(edit, &QLineEdit::returnPressed, &dialog, &QDialog::accept);

    edit->setFocus();
    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }

    return edit->text().trimmed();
}

void MainWindow::ensureClassExists(const QString& label)
{
    const QString normalized = label.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    bool changed = false;
    if (!classNames_.contains(normalized)) {
        classNames_.append(normalized);
        changed = true;
    }

    if (!classColors_.contains(normalized) || !classColors_[normalized].isValid()) {
        classColors_[normalized] = defaultColorForLabel(normalized);
        changed = true;
    }

    if (changed) {
        canvas_->setLabelColors(classColors_);
    }
}

int MainWindow::countCurrentImageAnnotationsForClass(const QString& label) const
{
    const QString normalizedLabel = label.trimmed();
    int count = 0;
    for (const Annotation& annotation : currentAnnotations()) {
        if (annotation.label.trimmed() == normalizedLabel) {
            ++count;
        }
    }
    return count;
}

int MainWindow::countAnnotationsForClass(const QString& label) const
{
    int count = 0;
    const QString normalizedLabel = label.trimmed();
    for (const QString& imagePath : imagePaths_) {
        const bool hasCachedAnnotations = annotationsByImage_.contains(imagePath);

        if (hasCachedAnnotations) {
            const QVector<Annotation>& annotations = annotationsByImage_.value(imagePath);
            for (const Annotation& annotation : annotations) {
                if (annotation.label.trimmed() == normalizedLabel) {
                    ++count;
                }
            }
            continue;
        }

        QImageReader reader(imagePath);
        const QSize imageSize = reader.size();
        if (imageSize.isEmpty()) {
            continue;
        }

        QVector<Annotation> annotations;
        if (loadAnnotationsFromDisk(imagePath, imageSize, &annotations)) {
            for (const Annotation& annotation : annotations) {
                if (annotation.label.trimmed() == normalizedLabel) {
                    ++count;
                }
            }
        }
    }
    return count;
}

void MainWindow::removeAnnotationsForClass(const QString& label)
{
    const QString normalizedLabel = label.trimmed();
    for (const QString& imagePath : imagePaths_) {
        const bool hasCachedAnnotations = annotationsByImage_.contains(imagePath);

        if (!hasCachedAnnotations) {
            QVector<Annotation> loaded;
            QImageReader reader(imagePath);
            const QSize imageSize = reader.size();
            if (!imageSize.isEmpty()) {
                loadAnnotationsFromDisk(imagePath, imageSize, &loaded);
            }
            annotationsByImage_[imagePath] = loaded;
        }

        QVector<Annotation>& annotations = annotationsByImage_[imagePath];
        const int previousSize = annotations.size();
        for (int i = annotations.size() - 1; i >= 0; --i) {
            if (annotations[i].label.trimmed() == normalizedLabel) {
                annotations.remove(i);
            }
        }

        if (annotations.size() != previousSize) {
            dirtyImages_.insert(imagePath);
        }
    }
}

void MainWindow::persistDatasetAfterClassDeletion()
{
    if (outputFolder_.isEmpty()) {
        return;
    }

    QString error;
    if (AnnotationIO::saveDataset(
            currentFormat(),
            imagePaths_,
            outputFolder_,
            annotationsByImage_,
            classNames_,
            &error)) {
        dirtyImages_.clear();
    } else {
        QMessageBox::warning(
            this,
            "保存标签删除结果失败",
            error);
    }
}

bool MainWindow::loadAnnotationsFromDisk(const QString& imagePath,
                                         const QSize& imageSize,
                                         QVector<Annotation>* annotations,
                                         AnnotationIO::SaveFormat* detectedFormat,
                                         QString* errorMessage) const
{
    if (!annotations) {
        return false;
    }
    if (outputFolder_.isEmpty()) {
        annotations->clear();
        if (detectedFormat) {
            *detectedFormat = currentFormat();
        }
        return true;
    }

    const AnnotationIO::SaveFormat preferred = currentFormat();
    const QString lookupRoot = outputFolder_;
    QStringList errors;

    QVector<AnnotationIO::SaveFormat> formats = {preferred};
    for (const AnnotationIO::SaveFormat format : formats) {
        if (!AnnotationIO::exists(format, imagePath, lookupRoot)) {
            continue;
        }
        QString error;
        if (AnnotationIO::load(format, imagePath, lookupRoot, imageSize, annotations, &error)) {
            if (detectedFormat) {
                *detectedFormat = format;
            }
            return true;
        }
        errors.append(QString("%1：%2")
                          .arg(AnnotationIO::formatDisplayName(format), error));
    }

    if (!errors.isEmpty()) {
        if (errorMessage) {
            *errorMessage = errors.join("；");
        }
        return false;
    }

    annotations->clear();
    if (detectedFormat) {
        *detectedFormat = preferred;
    }
    return true;
}

void MainWindow::restoreDatasetSettings()
{
    if (imageFolder_.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.beginGroup("datasets");
    settings.beginGroup(datasetSettingsKey());
    outputFolder_ = settings.value("outputFolder").toString();
    const int formatValue = settings.value(
        "format",
        static_cast<int>(AnnotationIO::SaveFormat::VocXml)).toInt();
    settings.endGroup();
    settings.endGroup();

    outputFolder_ = normalizedOutputRoot(outputFolder_);
    if (!outputFolder_.isEmpty() && !QDir(outputFolder_).exists()) {
        outputFolder_.clear();
    }
    const AnnotationIO::SaveFormat savedFormat =
        static_cast<AnnotationIO::SaveFormat>(formatValue);
    if (AnnotationIO::supportedFormats().contains(savedFormat)) {
        annotationFormat_ = savedFormat;
    }
}

void MainWindow::saveDatasetSettings() const
{
    if (imageFolder_.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.beginGroup("datasets");
    settings.beginGroup(datasetSettingsKey());
    settings.setValue("outputFolder", outputFolder_);
    settings.setValue("format", static_cast<int>(currentFormat()));
    settings.endGroup();
    settings.endGroup();
}

QString MainWindow::datasetSettingsKey() const
{
    return QString::fromLatin1(
        QUrl::toPercentEncoding(QDir::cleanPath(imageFolder_).toLower()));
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

    QStringList candidates = {
        QDir(outputFolder_).filePath("annotaflow_labels.json")
    };
    for (const AnnotationIO::SaveFormat format : AnnotationIO::supportedFormats()) {
        candidates.append(
            QDir(outputFolder_).filePath(
                AnnotationIO::formatDirectoryName(format) + "/annotaflow_labels.json"));
    }

    for (const QString& candidate : candidates) {
        QFile file(candidate);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            continue;
        }
        const QJsonArray labels = doc.object().value("labels").toArray();
        bool loadedAny = false;
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
            loadedAny = true;
        }
        if (loadedAny) {
            break;
        }
    }

    canvas_->setLabelColors(classColors_);
}

void MainWindow::saveClassCatalog() const
{
    if (outputFolder_.isEmpty()) {
        return;
    }
    saveClassCatalogTo(outputFolder_);
}

void MainWindow::saveClassCatalogTo(const QString& folder) const
{
    if (folder.isEmpty()) {
        return;
    }

    QFile file(QDir(folder).filePath("annotaflow_labels.json"));
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
    root["format"] = static_cast<int>(currentFormat());
    root["labels"] = labels;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void MainWindow::addKnownLabelsFromOutput()
{
    if (outputFolder_.isEmpty()) {
        return;
    }

    const QStringList names =
        AnnotationIO::readClassNames(currentFormat(), outputFolder_);
    for (const QString& name : names) {
        ensureClassExists(name);
    }

    canvas_->setLabelColors(classColors_);
}

AnnotationIO::SaveFormat MainWindow::currentFormat() const
{
    return annotationFormat_;
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
