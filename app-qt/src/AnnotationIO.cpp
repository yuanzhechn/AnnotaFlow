#include "AnnotationIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegExp>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QtGlobal>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>

namespace {

QRectF clampRect(const QRectF& input, const QSize& imageSize)
{
    const QRectF bounds(0.0, 0.0, imageSize.width(), imageSize.height());
    return normalizedAnnotationRect(input).intersected(bounds);
}

QString baseNameForImage(const QString& imagePath)
{
    return QFileInfo(imagePath).completeBaseName();
}

bool isConventionalImageDirectoryName(const QString& name)
{
    return name.compare("image", Qt::CaseInsensitive) == 0 ||
           name.compare("images", Qt::CaseInsensitive) == 0 ||
           name.compare("JPEGImages", Qt::CaseInsensitive) == 0;
}

QStringList matchingAnnotationDirectories(const QString& root)
{
    QStringList paths;
    QDir rootDir(root);
    const QStringList directoryNames = rootDir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);
    const QStringList aliases = {
        "label",
        "labels",
        "annotation",
        "annotations"
    };
    for (const QString& directoryName : directoryNames) {
        for (const QString& alias : aliases) {
            if (directoryName.compare(alias, Qt::CaseInsensitive) == 0) {
                paths.append(rootDir.filePath(directoryName));
                break;
            }
        }
    }
    paths.removeDuplicates();
    return paths;
}

QStringList annotationRootsForImage(const QString& imagePath, const QString& outputRoot)
{
    QStringList roots = {QDir::cleanPath(outputRoot)};
    const QString imageDir = QFileInfo(imagePath).absolutePath();
    roots.append(imageDir);
    roots.append(QFileInfo(imageDir).absolutePath());
    roots.removeAll(QString());
    roots.removeDuplicates();
    return roots;
}

QStringList annotationDirectoriesForImage(const QString& imagePath, const QString& outputRoot)
{
    QStringList paths;
    for (const QString& root : annotationRootsForImage(imagePath, outputRoot)) {
        paths.append(matchingAnnotationDirectories(root));
    }
    paths.removeDuplicates();
    return paths;
}

bool ensureDir(const QString& path, QString* errorMessage)
{
    QDir dir(path);
    if (dir.exists() || dir.mkpath(".")) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QString("无法创建输出目录：%1").arg(path);
    }
    return false;
}

QString vocPathFor(const QString& imagePath, const QString& outputRoot)
{
    return QDir(outputRoot).filePath(baseNameForImage(imagePath) + ".xml");
}

QString yoloPathFor(const QString& imagePath, const QString& outputRoot)
{
    return QDir(outputRoot).filePath(baseNameForImage(imagePath) + ".txt");
}

QString classesPathFor(const QString& outputRoot)
{
    return QDir(outputRoot).filePath("classes.txt");
}

QString formatDirPath(AnnotationIO::SaveFormat format, const QString& outputRoot)
{
    Q_UNUSED(format);
    return QDir::cleanPath(outputRoot);
}

QString classCatalogPathFor(AnnotationIO::SaveFormat format, const QString& outputRoot)
{
    return QDir(formatDirPath(format, outputRoot)).filePath("classes.txt");
}

QStringList nestedLegacyRoots(const QString& outputRoot)
{
    QStringList roots;
    for (const AnnotationIO::SaveFormat format : AnnotationIO::supportedFormats()) {
        const QString candidate = QDir(outputRoot).filePath(
            AnnotationIO::formatDirectoryName(format));
        if (QDir(candidate).exists()) {
            roots.append(candidate);
        }
    }
    return roots;
}

QString labelMePathFor(const QString& imagePath, const QString& outputRoot)
{
    return QDir(outputRoot).filePath(baseNameForImage(imagePath) + ".json");
}

QString kittiPathFor(const QString& imagePath, const QString& outputRoot)
{
    return QDir(outputRoot).filePath(baseNameForImage(imagePath) + ".txt");
}

QString cocoPathFor(const QString& outputRoot)
{
    return QDir(outputRoot).filePath("instances.json");
}

QString findCocoPath(const QString& outputRoot)
{
    QStringList directCandidates = {
        cocoPathFor(outputRoot),
        QDir(outputRoot).filePath("coco_labels/instances.json"),
        QDir(outputRoot).filePath("annotations/instances.json"),
        QDir(outputRoot).filePath("instances.json")
    };
    QStringList roots = {QDir::cleanPath(outputRoot)};
    const QFileInfo outputInfo(outputRoot);
    if (isConventionalImageDirectoryName(outputInfo.fileName())) {
        roots.append(outputInfo.absolutePath());
    }
    for (const QString& root : roots) {
        for (const QString& annotationDir : matchingAnnotationDirectories(root)) {
            directCandidates.append(QDir(annotationDir).filePath("instances.json"));
            directCandidates.append(QDir(annotationDir).filePath("annotations.json"));
        }
    }
    directCandidates.removeDuplicates();
    for (const QString& candidate : directCandidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    for (const QString& nestedRoot : nestedLegacyRoots(outputRoot)) {
        const QString candidate = QDir(nestedRoot).filePath("coco_labels/instances.json");
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    QDir annotationsDir(QDir(outputRoot).filePath("annotations"));
    const QStringList files = annotationsDir.entryList(
        QStringList{"instances*.json"},
        QDir::Files,
        QDir::Name);
    return files.isEmpty() ? QString() : annotationsDir.filePath(files.first());
}

QString csvPathFor(const QString& outputRoot)
{
    return QDir(outputRoot).filePath("annotations.csv");
}

QString findCsvPath(const QString& outputRoot)
{
    QStringList candidates = {
        csvPathFor(outputRoot),
        QDir(outputRoot).filePath("csv_labels/annotations.csv"),
        QDir(outputRoot).filePath("annotations.csv")
    };
    const QFileInfo outputInfo(outputRoot);
    QStringList roots = {QDir::cleanPath(outputRoot)};
    if (isConventionalImageDirectoryName(outputInfo.fileName())) {
        roots.append(outputInfo.absolutePath());
    }
    for (const QString& root : roots) {
        for (const QString& annotationDir : matchingAnnotationDirectories(root)) {
            candidates.append(QDir(annotationDir).filePath("annotations.csv"));
        }
    }
    candidates.removeDuplicates();
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    for (const QString& nestedRoot : nestedLegacyRoots(outputRoot)) {
        const QString candidate = QDir(nestedRoot).filePath("csv_labels/annotations.csv");
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QStringList vocLoadCandidates(const QString& imagePath, const QString& outputRoot)
{
    const QString baseName = baseNameForImage(imagePath) + ".xml";
    const QString imageDir = QFileInfo(imagePath).absolutePath();
    const QString datasetRoot = QFileInfo(imageDir).absolutePath();
    QStringList paths = {
        QDir(outputRoot).filePath(baseName),
        QDir(outputRoot).filePath("xml_labels/" + baseName),
        QDir(imageDir).filePath(baseName),
        QDir(imageDir).filePath("xml_labels/" + baseName),
        QDir(datasetRoot).filePath("Annotations/" + baseName),
        QDir(datasetRoot).filePath("annotations/" + baseName),
        QDir(datasetRoot).filePath("xml_labels/" + baseName)
    };
    for (const QString& annotationDir : annotationDirectoriesForImage(imagePath, outputRoot)) {
        paths.append(QDir(annotationDir).filePath(baseName));
    }

    const QFileInfo imageDirInfo(imageDir);
    if (imageDirInfo.fileName().compare("JPEGImages", Qt::CaseInsensitive) == 0 ||
        imageDirInfo.fileName().compare("images", Qt::CaseInsensitive) == 0) {
        const QString root = imageDirInfo.absolutePath();
        paths.append(QDir(root).filePath("Annotations/" + baseName));
        paths.append(QDir(root).filePath("annotations/" + baseName));
    }
    const QFileInfo imagesParentInfo(imageDirInfo.absolutePath());
    if (imagesParentInfo.fileName().compare("JPEGImages", Qt::CaseInsensitive) == 0 ||
        imagesParentInfo.fileName().compare("images", Qt::CaseInsensitive) == 0) {
        const QString root = imagesParentInfo.absolutePath();
        const QString split = imageDirInfo.fileName();
        paths.append(QDir(root).filePath("Annotations/" + split + "/" + baseName));
        paths.append(QDir(root).filePath("annotations/" + split + "/" + baseName));
    }
    for (const QString& nestedRoot : nestedLegacyRoots(outputRoot)) {
        paths.append(QDir(nestedRoot).filePath("xml_labels/" + baseName));
    }
    paths.removeDuplicates();
    return paths;
}

QVector<QPair<QString, QString>> yoloLoadCandidates(const QString& imagePath, const QString& outputRoot)
{
    const QString baseName = baseNameForImage(imagePath) + ".txt";
    const QString imageDir = QFileInfo(imagePath).absolutePath();
    const QString datasetRoot = QFileInfo(imageDir).absolutePath();
    QVector<QPair<QString, QString>> candidates = {
        {QDir(outputRoot).filePath(baseName), QDir(outputRoot).filePath("classes.txt")},
        {QDir(outputRoot).filePath("yolo_labels/" + baseName), QDir(outputRoot).filePath("yolo_labels/classes.txt")},
        {QDir(imageDir).filePath("labels/" + baseName), QDir(imageDir).filePath("labels/classes.txt")},
        {QDir(imageDir).filePath(baseName), QDir(imageDir).filePath("classes.txt")},
        {QDir(datasetRoot).filePath("labels/" + baseName), QDir(datasetRoot).filePath("labels/classes.txt")},
        {QDir(datasetRoot).filePath("labels/" + baseName), QDir(datasetRoot).filePath("classes.txt")},
        {QDir(datasetRoot).filePath("yolo_labels/" + baseName), QDir(datasetRoot).filePath("yolo_labels/classes.txt")}
    };
    for (const QString& root : annotationRootsForImage(imagePath, outputRoot)) {
        for (const QString& annotationDir : matchingAnnotationDirectories(root)) {
            const QString annotationPath = QDir(annotationDir).filePath(baseName);
            candidates.append({annotationPath, QDir(annotationDir).filePath("classes.txt")});
            candidates.append({annotationPath, QDir(root).filePath("classes.txt")});
            candidates.append({annotationPath, QDir(annotationDir).filePath("data.yaml")});
            candidates.append({annotationPath, QDir(root).filePath("data.yaml")});
            candidates.append({annotationPath, QDir(root).filePath("dataset.yaml")});
        }
    }

    const QFileInfo imageDirInfo(imageDir);
    if (isConventionalImageDirectoryName(imageDirInfo.fileName())) {
        const QString splitRoot = imageDirInfo.absolutePath();
        const QString labelsPath = QDir(splitRoot).filePath("labels/" + baseName);
        candidates.append({labelsPath, QDir(splitRoot).filePath("classes.txt")});
        candidates.append({labelsPath, QDir(splitRoot).filePath("data.yaml")});
        candidates.append({labelsPath, QDir(QFileInfo(splitRoot).absolutePath()).filePath("data.yaml")});
    }

    const QFileInfo imagesParentInfo(imageDirInfo.absolutePath());
    if (imagesParentInfo.fileName().compare("images", Qt::CaseInsensitive) == 0) {
        const QString root = imagesParentInfo.absolutePath();
        const QString split = imageDirInfo.fileName();
        const QString labelsPath = QDir(root).filePath("labels/" + split + "/" + baseName);
        candidates.append({labelsPath, QDir(root).filePath("classes.txt")});
        candidates.append({labelsPath, QDir(root).filePath("data.yaml")});
        candidates.append({labelsPath, QDir(root).filePath("dataset.yaml")});
    }

    candidates.append({QDir(outputRoot).filePath("yolo_labels/" + baseName),
                       QDir(outputRoot).filePath("data.yaml")});
    for (const QString& nestedRoot : nestedLegacyRoots(outputRoot)) {
        candidates.append({
            QDir(nestedRoot).filePath("yolo_labels/" + baseName),
            QDir(nestedRoot).filePath("yolo_labels/classes.txt")
        });
    }
    return candidates;
}

QStringList labelMeLoadCandidates(const QString& imagePath, const QString& outputRoot)
{
    const QString fileName = baseNameForImage(imagePath) + ".json";
    const QString imageDir = QFileInfo(imagePath).absolutePath();
    const QString datasetRoot = QFileInfo(imageDir).absolutePath();
    QStringList paths = {
        QDir(outputRoot).filePath(fileName),
        QDir(outputRoot).filePath("labelme_labels/" + fileName),
        QDir(outputRoot).filePath("labelme/" + fileName),
        QDir(imageDir).filePath(fileName),
        QDir(datasetRoot).filePath("labelme_labels/" + fileName),
        QDir(datasetRoot).filePath("labelme/" + fileName)
    };
    for (const QString& annotationDir : annotationDirectoriesForImage(imagePath, outputRoot)) {
        paths.append(QDir(annotationDir).filePath(fileName));
    }
    for (const QString& nestedRoot : nestedLegacyRoots(outputRoot)) {
        paths.append(QDir(nestedRoot).filePath("labelme_labels/" + fileName));
    }
    paths.removeDuplicates();
    return paths;
}

QStringList kittiLoadCandidates(const QString& imagePath, const QString& outputRoot)
{
    const QString fileName = baseNameForImage(imagePath) + ".txt";
    const QString imageDir = QFileInfo(imagePath).absolutePath();
    const QString datasetRoot = QFileInfo(imageDir).absolutePath();
    QStringList paths = {
        QDir(outputRoot).filePath(fileName),
        QDir(outputRoot).filePath("kitti_labels/" + fileName),
        QDir(outputRoot).filePath("kitti/" + fileName),
        QDir(imageDir).filePath("label_2/" + fileName),
        QDir(datasetRoot).filePath("label_2/" + fileName),
        QDir(datasetRoot).filePath("kitti_labels/" + fileName)
    };
    for (const QString& annotationDir : annotationDirectoriesForImage(imagePath, outputRoot)) {
        paths.append(QDir(annotationDir).filePath(fileName));
    }
    for (const QString& nestedRoot : nestedLegacyRoots(outputRoot)) {
        paths.append(QDir(nestedRoot).filePath("kitti_labels/" + fileName));
    }
    paths.removeDuplicates();
    return paths;
}

QString findFirstExisting(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

bool writeClasses(const QString& classesPath, const QStringList& classes, QString* errorMessage);

QStringList readClasses(const QString& classesPath)
{
    QFile file(classesPath);
    QStringList classes;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return classes;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    while (!in.atEnd()) {
        classes.append(in.readLine().trimmed());
    }
    return classes;
}

QStringList cleanClassNames(const QStringList& rawClasses)
{
    QStringList classes;
    for (const QString& rawClass : rawClasses) {
        const QString name = rawClass.trimmed();
        if (!name.isEmpty() && !classes.contains(name)) {
            classes.append(name);
        }
    }
    return classes;
}

QString unquoteYamlValue(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 &&
        ((value.startsWith('"') && value.endsWith('"')) ||
         (value.startsWith('\'') && value.endsWith('\'')))) {
        value = value.mid(1, value.size() - 2);
    }
    return value.trimmed();
}

QStringList readYoloClasses(const QString& path)
{
    if (!path.endsWith(".yaml", Qt::CaseInsensitive) &&
        !path.endsWith(".yml", Qt::CaseInsensitive)) {
        return readClasses(path);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QMap<int, QString> indexedNames;
    QStringList inlineNames;
    bool inNamesBlock = false;
    while (!in.atEnd()) {
        const QString rawLine = in.readLine();
        const QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        if (trimmed.startsWith("names:")) {
            inNamesBlock = true;
            QString remainder = trimmed.mid(QString("names:").size()).trimmed();
            if (remainder.startsWith('[') && remainder.endsWith(']')) {
                remainder = remainder.mid(1, remainder.size() - 2);
                for (const QString& item : remainder.split(',', Qt::SkipEmptyParts)) {
                    inlineNames.append(unquoteYamlValue(item));
                }
                break;
            }
            continue;
        }
        if (!inNamesBlock) {
            continue;
        }
        if (!rawLine.startsWith(' ') && !rawLine.startsWith('\t')) {
            break;
        }
        const int colon = trimmed.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        bool ok = false;
        const int index = trimmed.left(colon).trimmed().toInt(&ok);
        const QString name = unquoteYamlValue(trimmed.mid(colon + 1));
        if (ok && !name.isEmpty()) {
            indexedNames.insert(index, name);
        }
    }
    if (!inlineNames.isEmpty()) {
        return inlineNames;
    }
    QStringList classes;
    for (auto it = indexedNames.cbegin(); it != indexedNames.cend(); ++it) {
        while (classes.size() < it.key()) {
            classes.append(QString());
        }
        classes.append(it.value());
    }
    return classes;
}

QStringList normalizedClasses(const QStringList& requested,
                              const QHash<QString, QVector<Annotation>>& annotationsByImage)
{
    QStringList classes;
    for (const QString& requestedClass : requested) {
        const QString name = requestedClass.trimmed();
        if (!name.isEmpty() && !classes.contains(name)) {
            classes.append(name);
        }
    }
    for (auto it = annotationsByImage.cbegin(); it != annotationsByImage.cend(); ++it) {
        for (const Annotation& annotation : it.value()) {
            const QString name = annotation.label.trimmed();
            if (!name.isEmpty() && !classes.contains(name)) {
                classes.append(name);
            }
        }
    }
    return classes;
}

bool writeFormatClasses(AnnotationIO::SaveFormat format,
                        const QString& outputRoot,
                        const QStringList& classes,
                        QString* errorMessage)
{
    const QString dirPath = formatDirPath(format, outputRoot);
    if (!ensureDir(dirPath, errorMessage)) {
        return false;
    }
    return writeClasses(classCatalogPathFor(format, outputRoot), classes, errorMessage);
}

bool writeClasses(const QString& classesPath, const QStringList& classes, QString* errorMessage)
{
    QFile file(classesPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = "无法写入 classes.txt。";
        }
        return false;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    for (const QString& cls : classes) {
        out << cls << '\n';
    }
    return true;
}

bool saveVoc(const QString& imagePath,
             const QString& outputRoot,
             const QSize& imageSize,
             const QVector<Annotation>& annotations,
             QString* errorMessage)
{
    const QString dirPath = outputRoot;
    if (!ensureDir(dirPath, errorMessage)) {
        return false;
    }

    const QString filePath = vocPathFor(imagePath, outputRoot);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = "无法写入 XML 标注文件。";
        }
        return false;
    }

    QFileInfo imageInfo(imagePath);
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("annotation");
    xml.writeTextElement("folder", imageInfo.absolutePath());
    xml.writeTextElement("filename", imageInfo.fileName());
    xml.writeTextElement("path", imageInfo.absoluteFilePath());

    xml.writeStartElement("size");
    xml.writeTextElement("width", QString::number(imageSize.width()));
    xml.writeTextElement("height", QString::number(imageSize.height()));
    xml.writeTextElement("depth", "3");
    xml.writeEndElement();

    for (const Annotation& annotation : annotations) {
        const QRectF rect = clampRect(annotation.rect, imageSize);
        if (rect.width() < 1.0 || rect.height() < 1.0 || annotation.label.trimmed().isEmpty()) {
            continue;
        }

        const int xMin = std::clamp(static_cast<int>(std::floor(rect.left())), 0, imageSize.width());
        const int yMin = std::clamp(static_cast<int>(std::floor(rect.top())), 0, imageSize.height());
        const int xMax = std::clamp(static_cast<int>(std::ceil(rect.left() + rect.width())), 0, imageSize.width());
        const int yMax = std::clamp(static_cast<int>(std::ceil(rect.top() + rect.height())), 0, imageSize.height());

        xml.writeStartElement("object");
        xml.writeTextElement("name", annotation.label.trimmed());
        xml.writeTextElement("pose", "Unspecified");
        xml.writeTextElement("truncated", "0");
        xml.writeTextElement("difficult", "0");
        xml.writeStartElement("bndbox");
        xml.writeTextElement("xmin", QString::number(xMin));
        xml.writeTextElement("ymin", QString::number(yMin));
        xml.writeTextElement("xmax", QString::number(xMax));
        xml.writeTextElement("ymax", QString::number(yMax));
        xml.writeEndElement();
        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndDocument();
    return true;
}

bool saveYolo(const QString& imagePath,
              const QString& outputRoot,
              const QSize& imageSize,
              const QVector<Annotation>& annotations,
              const QStringList& requestedClasses,
              QString* errorMessage)
{
    const QString dirPath = outputRoot;
    if (!ensureDir(dirPath, errorMessage)) {
        return false;
    }

    const QString classesPath = classesPathFor(outputRoot);
    QStringList classes = requestedClasses;
    if (classes.isEmpty()) {
        classes = readClasses(classesPath);
    }
    for (const Annotation& annotation : annotations) {
        const QString label = annotation.label.trimmed();
        if (!label.isEmpty() && !classes.contains(label)) {
            classes.append(label);
        }
    }

    if (!writeClasses(classesPath, classes, errorMessage)) {
        return false;
    }

    QFile file(yoloPathFor(imagePath, outputRoot));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = "无法写入 YOLO 标注文件。";
        }
        return false;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out.setRealNumberPrecision(8);

    for (const Annotation& annotation : annotations) {
        const QRectF rect = clampRect(annotation.rect, imageSize);
        const QString label = annotation.label.trimmed();
        if (rect.width() < 1.0 || rect.height() < 1.0 || label.isEmpty()) {
            continue;
        }

        const int classId = classes.indexOf(label);
        if (classId < 0) {
            continue;
        }

        const double centerX = (rect.left() + rect.width() / 2.0) / imageSize.width();
        const double centerY = (rect.top() + rect.height() / 2.0) / imageSize.height();
        const double width = rect.width() / imageSize.width();
        const double height = rect.height() / imageSize.height();
        out << classId << ' ' << centerX << ' ' << centerY << ' ' << width << ' ' << height << '\n';
    }

    return true;
}

bool writeJsonFile(const QString& filePath,
                   const QJsonDocument& document,
                   QString* errorMessage)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QString("无法写入 JSON 文件：%1").arg(filePath);
        }
        return false;
    }
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (errorMessage) {
            *errorMessage = QString("提交 JSON 文件失败：%1").arg(filePath);
        }
        return false;
    }
    return true;
}

bool saveLabelMe(const QString& imagePath,
                 const QString& outputRoot,
                 const QSize& imageSize,
                 const QVector<Annotation>& annotations,
                 const QStringList& classes,
                 QString* errorMessage)
{
    const QString dirPath = formatDirPath(AnnotationIO::SaveFormat::LabelMeJson, outputRoot);
    if (!ensureDir(dirPath, errorMessage) ||
        !writeFormatClasses(AnnotationIO::SaveFormat::LabelMeJson, outputRoot, classes, errorMessage)) {
        return false;
    }

    QJsonArray shapes;
    for (const Annotation& annotation : annotations) {
        const QRectF rect = clampRect(annotation.rect, imageSize);
        const QString label = annotation.label.trimmed();
        if (label.isEmpty() || rect.width() < 1.0 || rect.height() < 1.0) {
            continue;
        }
        QJsonObject shape;
        shape["label"] = label;
        shape["shape_type"] = "rectangle";
        shape["flags"] = QJsonObject();
        QJsonArray points;
        points.append(QJsonArray{rect.left(), rect.top()});
        points.append(QJsonArray{rect.right(), rect.bottom()});
        shape["points"] = points;
        shapes.append(shape);
    }

    QJsonObject root;
    root["version"] = "5.0.0";
    root["flags"] = QJsonObject();
    root["shapes"] = shapes;
    root["imagePath"] = QFileInfo(imagePath).fileName();
    root["imageData"] = QJsonValue::Null;
    root["imageHeight"] = imageSize.height();
    root["imageWidth"] = imageSize.width();
    return writeJsonFile(labelMePathFor(imagePath, outputRoot), QJsonDocument(root), errorMessage);
}

bool saveKitti(const QString& imagePath,
               const QString& outputRoot,
               const QSize& imageSize,
               const QVector<Annotation>& annotations,
               const QStringList& classes,
               QString* errorMessage)
{
    const QString dirPath = formatDirPath(AnnotationIO::SaveFormat::KittiTxt, outputRoot);
    if (!ensureDir(dirPath, errorMessage) ||
        !writeFormatClasses(AnnotationIO::SaveFormat::KittiTxt, outputRoot, classes, errorMessage)) {
        return false;
    }

    QSaveFile file(kittiPathFor(imagePath, outputRoot));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法写入 KITTI 标注文件。";
        }
        return false;
    }
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out.setRealNumberPrecision(8);
    for (const Annotation& annotation : annotations) {
        const QRectF rect = clampRect(annotation.rect, imageSize);
        const QString label = annotation.label.trimmed();
        if (label.isEmpty() || rect.width() < 1.0 || rect.height() < 1.0) {
            continue;
        }
        out << label << " 0 0 0 "
            << rect.left() << ' ' << rect.top() << ' '
            << rect.right() << ' ' << rect.bottom()
            << " 0 0 0 0 0 0 0\n";
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = "提交 KITTI 标注文件失败。";
        }
        return false;
    }
    return true;
}

Annotation parseVocObject(QXmlStreamReader& xml)
{
    Annotation annotation;
    double xMin = 0.0;
    double yMin = 0.0;
    double xMax = 0.0;
    double yMax = 0.0;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == "object") {
            break;
        }
        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == "name") {
            annotation.label = xml.readElementText().trimmed();
            continue;
        }

        if (xml.name() != "bndbox") {
            xml.skipCurrentElement();
            continue;
        }

        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == "bndbox") {
                break;
            }
            if (!xml.isStartElement()) {
                continue;
            }

            const QString name = xml.name().toString();
            bool ok = false;
            const double value = xml.readElementText().toDouble(&ok);
            if (!ok) {
                continue;
            }
            if (name == "xmin") {
                xMin = value;
            } else if (name == "ymin") {
                yMin = value;
            } else if (name == "xmax") {
                xMax = value;
            } else if (name == "ymax") {
                yMax = value;
            }
        }
    }

    annotation.rect = QRectF(QPointF(xMin, yMin), QPointF(xMax, yMax)).normalized();
    return annotation;
}

bool loadVoc(const QString& imagePath,
             const QString& outputRoot,
             const QSize& imageSize,
             QVector<Annotation>* annotations,
             QString* errorMessage)
{
    QString filePath;
    for (const QString& candidate : vocLoadCandidates(imagePath, outputRoot)) {
        if (QFileInfo::exists(candidate)) {
            filePath = candidate;
            break;
        }
    }
    if (filePath.isEmpty()) {
        annotations->clear();
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法打开 XML 标注文件。";
        }
        return false;
    }

    QVector<Annotation> loaded;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != "object") {
            continue;
        }

        Annotation annotation = parseVocObject(xml);
        annotation.rect = clampRect(annotation.rect, imageSize);
        if (!annotation.label.isEmpty() &&
            annotation.rect.width() >= 1.0 &&
            annotation.rect.height() >= 1.0) {
            loaded.append(annotation);
        }
    }

    if (xml.hasError()) {
        if (errorMessage) {
            *errorMessage = xml.errorString();
        }
        return false;
    }

    *annotations = loaded;
    return true;
}

bool loadYolo(const QString& imagePath,
              const QString& outputRoot,
              const QSize& imageSize,
              QVector<Annotation>* annotations,
              QString* errorMessage)
{
    QString annotationPath;
    QString classesPath;
    QString firstAnnotationPath;
    QString firstExpectedClassesPath;
    for (const auto& candidate : yoloLoadCandidates(imagePath, outputRoot)) {
        if (QFileInfo::exists(candidate.first)) {
            if (firstAnnotationPath.isEmpty()) {
                firstAnnotationPath = candidate.first;
                firstExpectedClassesPath = candidate.second;
            }
            if (QFileInfo::exists(candidate.second)) {
                annotationPath = candidate.first;
                classesPath = candidate.second;
                break;
            }
        }
    }
    if (firstAnnotationPath.isEmpty()) {
        annotations->clear();
        return true;
    }
    if (annotationPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString("YOLO 标注存在，但找不到类别文件。首先检查的位置：%1")
                                .arg(firstExpectedClassesPath);
        }
        return false;
    }

    QFile annotationFile(annotationPath);
    const QStringList classes = readYoloClasses(classesPath);
    if (classes.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString("YOLO 标注存在，但类别文件不存在或为空：%1").arg(classesPath);
        }
        return false;
    }

    if (!annotationFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法打开 YOLO 标注文件。";
        }
        return false;
    }

    QVector<Annotation> loaded;
    QTextStream in(&annotationFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QStringList parts = line.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() != 5) {
            continue;
        }

        bool okClass = false;
        bool okCenterX = false;
        bool okCenterY = false;
        bool okWidth = false;
        bool okHeight = false;
        const int classId = parts[0].toInt(&okClass);
        const double centerX = parts[1].toDouble(&okCenterX);
        const double centerY = parts[2].toDouble(&okCenterY);
        const double width = parts[3].toDouble(&okWidth);
        const double height = parts[4].toDouble(&okHeight);

        if (!okClass || !okCenterX || !okCenterY || !okWidth || !okHeight || classId < 0 || classId >= classes.size()) {
            continue;
        }

        const double pixelWidth = width * imageSize.width();
        const double pixelHeight = height * imageSize.height();
        const double x = centerX * imageSize.width() - pixelWidth / 2.0;
        const double y = centerY * imageSize.height() - pixelHeight / 2.0;

        Annotation annotation;
        annotation.label = classes[classId];
        annotation.rect = clampRect(QRectF(x, y, pixelWidth, pixelHeight), imageSize);
        if (annotation.rect.width() >= 1.0 && annotation.rect.height() >= 1.0) {
            loaded.append(annotation);
        }
    }

    *annotations = loaded;
    return true;
}

bool loadLabelMe(const QString& imagePath,
                 const QString& outputRoot,
                 const QSize& imageSize,
                 QVector<Annotation>* annotations,
                 QString* errorMessage)
{
    const QString filePath = findFirstExisting(labelMeLoadCandidates(imagePath, outputRoot));
    if (filePath.isEmpty()) {
        annotations->clear();
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法打开 LabelMe JSON 文件。";
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QString("LabelMe JSON 解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }

    QVector<Annotation> loaded;
    const QJsonArray shapes = document.object().value("shapes").toArray();
    for (const QJsonValue& value : shapes) {
        const QJsonObject shape = value.toObject();
        if (shape.value("shape_type").toString("rectangle") != "rectangle") {
            continue;
        }
        const QJsonArray points = shape.value("points").toArray();
        if (points.size() < 2) {
            continue;
        }

        bool hasPoint = false;
        double minX = 0.0;
        double minY = 0.0;
        double maxX = 0.0;
        double maxY = 0.0;
        for (const QJsonValue& pointValue : points) {
            const QJsonArray point = pointValue.toArray();
            if (point.size() < 2) {
                continue;
            }
            const double x = point[0].toDouble();
            const double y = point[1].toDouble();
            if (!std::isfinite(x) || !std::isfinite(y)) {
                continue;
            }
            if (!hasPoint) {
                minX = maxX = x;
                minY = maxY = y;
                hasPoint = true;
            } else {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
        if (!hasPoint) {
            continue;
        }

        Annotation annotation;
        annotation.label = shape.value("label").toString().trimmed();
        annotation.rect = clampRect(
            QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized(),
            imageSize);
        if (!annotation.label.isEmpty() &&
            annotation.rect.width() >= 1.0 &&
            annotation.rect.height() >= 1.0) {
            loaded.append(annotation);
        }
    }
    *annotations = loaded;
    return true;
}

bool loadKitti(const QString& imagePath,
               const QString& outputRoot,
               const QSize& imageSize,
               QVector<Annotation>* annotations,
               QString* errorMessage)
{
    const QString filePath = findFirstExisting(kittiLoadCandidates(imagePath, outputRoot));
    if (filePath.isEmpty()) {
        annotations->clear();
        return true;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法打开 KITTI TXT 文件。";
        }
        return false;
    }

    QVector<Annotation> loaded;
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 8) {
            continue;
        }
        bool okLeft = false;
        bool okTop = false;
        bool okRight = false;
        bool okBottom = false;
        const double left = parts[4].toDouble(&okLeft);
        const double top = parts[5].toDouble(&okTop);
        const double right = parts[6].toDouble(&okRight);
        const double bottom = parts[7].toDouble(&okBottom);
        if (!okLeft || !okTop || !okRight || !okBottom) {
            continue;
        }
        Annotation annotation;
        annotation.label = parts[0].trimmed();
        annotation.rect = clampRect(
            QRectF(QPointF(left, top), QPointF(right, bottom)).normalized(),
            imageSize);
        if (!annotation.label.isEmpty() &&
            annotation.rect.width() >= 1.0 &&
            annotation.rect.height() >= 1.0) {
            loaded.append(annotation);
        }
    }
    *annotations = loaded;
    return true;
}

bool parseCoco(const QString& outputRoot,
               QJsonObject* root,
               QString* errorMessage)
{
    const QString filePath = findCocoPath(outputRoot);
    if (filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "未找到 COCO JSON 文件。";
        }
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法打开 COCO instances.json。";
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QString("COCO JSON 解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }
    *root = document.object();
    return true;
}

bool loadCoco(const QString& imagePath,
              const QString& outputRoot,
              const QSize& imageSize,
              QVector<Annotation>* annotations,
              QString* errorMessage)
{
    if (findCocoPath(outputRoot).isEmpty()) {
        annotations->clear();
        return true;
    }

    QJsonObject root;
    if (!parseCoco(outputRoot, &root, errorMessage)) {
        return false;
    }
    QHash<int, QString> categories;
    for (const QJsonValue& value : root.value("categories").toArray()) {
        const QJsonObject category = value.toObject();
        categories.insert(category.value("id").toInt(), category.value("name").toString().trimmed());
    }

    int imageId = -1;
    const QString imageName = QFileInfo(imagePath).fileName();
    for (const QJsonValue& value : root.value("images").toArray()) {
        const QJsonObject image = value.toObject();
        const QString fileName = QDir::fromNativeSeparators(image.value("file_name").toString());
        if (QFileInfo(fileName).fileName().compare(imageName, Qt::CaseInsensitive) == 0) {
            imageId = image.value("id").toInt(-1);
            break;
        }
    }

    QVector<Annotation> loaded;
    if (imageId < 0) {
        *annotations = loaded;
        return true;
    }
    for (const QJsonValue& value : root.value("annotations").toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value("image_id").toInt(-1) != imageId) {
            continue;
        }
        const QJsonArray bbox = item.value("bbox").toArray();
        if (bbox.size() < 4) {
            continue;
        }
        Annotation annotation;
        annotation.label = categories.value(item.value("category_id").toInt()).trimmed();
        annotation.rect = clampRect(
            QRectF(bbox[0].toDouble(),
                   bbox[1].toDouble(),
                   bbox[2].toDouble(),
                   bbox[3].toDouble()),
            imageSize);
        if (!annotation.label.isEmpty() &&
            annotation.rect.width() >= 1.0 &&
            annotation.rect.height() >= 1.0) {
            loaded.append(annotation);
        }
    }
    *annotations = loaded;
    return true;
}

QString csvEscape(const QString& value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return '"' + escaped + '"';
}

QStringList parseCsvLine(const QString& line)
{
    QStringList values;
    QString current;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                current.append('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            values.append(current);
            current.clear();
        } else {
            current.append(ch);
        }
    }
    values.append(current);
    return values;
}

bool loadCsv(const QString& imagePath,
             const QString& outputRoot,
             const QSize& imageSize,
             QVector<Annotation>* annotations,
             QString* errorMessage)
{
    const QString filePath = findCsvPath(outputRoot);
    if (filePath.isEmpty()) {
        annotations->clear();
        return true;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法打开 CSV 标注文件。";
        }
        return false;
    }
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    if (!in.atEnd()) {
        in.readLine();
    }
    const QString imageName = QFileInfo(imagePath).fileName();
    QVector<Annotation> loaded;
    while (!in.atEnd()) {
        const QStringList parts = parseCsvLine(in.readLine());
        if (parts.size() < 8 || parts[0].compare(imageName, Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool okLeft = false;
        bool okTop = false;
        bool okRight = false;
        bool okBottom = false;
        const double left = parts[4].toDouble(&okLeft);
        const double top = parts[5].toDouble(&okTop);
        const double right = parts[6].toDouble(&okRight);
        const double bottom = parts[7].toDouble(&okBottom);
        if (!okLeft || !okTop || !okRight || !okBottom) {
            continue;
        }
        Annotation annotation;
        annotation.label = parts[3].trimmed();
        annotation.rect = clampRect(
            QRectF(QPointF(left, top), QPointF(right, bottom)).normalized(),
            imageSize);
        if (!annotation.label.isEmpty() &&
            annotation.rect.width() >= 1.0 &&
            annotation.rect.height() >= 1.0) {
            loaded.append(annotation);
        }
    }
    *annotations = loaded;
    return true;
}

bool saveCocoDataset(const QStringList& imagePaths,
                     const QString& outputRoot,
                     const QHash<QString, QVector<Annotation>>& annotationsByImage,
                     const QStringList& requestedClasses,
                     QString* errorMessage)
{
    const QStringList classes = normalizedClasses(requestedClasses, annotationsByImage);
    if (!writeFormatClasses(AnnotationIO::SaveFormat::CocoJson, outputRoot, classes, errorMessage)) {
        return false;
    }

    QJsonArray categories;
    for (int i = 0; i < classes.size(); ++i) {
        QJsonObject category;
        category["id"] = i + 1;
        category["name"] = classes[i];
        category["supercategory"] = "object";
        categories.append(category);
    }

    QJsonArray images;
    QJsonArray annotations;
    int annotationId = 1;
    for (int imageIndex = 0; imageIndex < imagePaths.size(); ++imageIndex) {
        const QString& imagePath = imagePaths[imageIndex];
        const QSize imageSize = QImageReader(imagePath).size();
        if (imageSize.isEmpty()) {
            continue;
        }
        const int imageId = imageIndex + 1;
        QJsonObject image;
        image["id"] = imageId;
        image["file_name"] = QFileInfo(imagePath).fileName();
        image["width"] = imageSize.width();
        image["height"] = imageSize.height();
        images.append(image);

        for (const Annotation& source : annotationsByImage.value(imagePath)) {
            const QRectF rect = clampRect(source.rect, imageSize);
            const int categoryIndex = classes.indexOf(source.label.trimmed());
            if (categoryIndex < 0 || rect.width() < 1.0 || rect.height() < 1.0) {
                continue;
            }
            QJsonObject annotation;
            annotation["id"] = annotationId++;
            annotation["image_id"] = imageId;
            annotation["category_id"] = categoryIndex + 1;
            annotation["bbox"] = QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
            annotation["area"] = rect.width() * rect.height();
            annotation["iscrowd"] = 0;
            annotation["segmentation"] = QJsonArray();
            annotations.append(annotation);
        }
    }

    QJsonObject info;
    info["description"] = "AnnotaFlow object detection export";
    info["version"] = "1.0";
    QJsonObject root;
    root["info"] = info;
    root["licenses"] = QJsonArray();
    root["images"] = images;
    root["annotations"] = annotations;
    root["categories"] = categories;
    return writeJsonFile(cocoPathFor(outputRoot), QJsonDocument(root), errorMessage);
}

bool saveCsvDataset(const QStringList& imagePaths,
                    const QString& outputRoot,
                    const QHash<QString, QVector<Annotation>>& annotationsByImage,
                    const QStringList& requestedClasses,
                    QString* errorMessage)
{
    const QStringList classes = normalizedClasses(requestedClasses, annotationsByImage);
    if (!writeFormatClasses(AnnotationIO::SaveFormat::Csv, outputRoot, classes, errorMessage)) {
        return false;
    }
    QSaveFile file(csvPathFor(outputRoot));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = "无法写入 CSV 标注文件。";
        }
        return false;
    }
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out.setRealNumberPrecision(8);
    out << "filename,width,height,label,xmin,ymin,xmax,ymax\n";
    for (const QString& imagePath : imagePaths) {
        const QSize imageSize = QImageReader(imagePath).size();
        if (imageSize.isEmpty()) {
            continue;
        }
        for (const Annotation& source : annotationsByImage.value(imagePath)) {
            const QRectF rect = clampRect(source.rect, imageSize);
            const QString label = source.label.trimmed();
            if (label.isEmpty() || rect.width() < 1.0 || rect.height() < 1.0) {
                continue;
            }
            out << csvEscape(QFileInfo(imagePath).fileName()) << ','
                << imageSize.width() << ','
                << imageSize.height() << ','
                << csvEscape(label) << ','
                << rect.left() << ','
                << rect.top() << ','
                << rect.right() << ','
                << rect.bottom() << '\n';
        }
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = "提交 CSV 标注文件失败。";
        }
        return false;
    }
    return true;
}

} // namespace

namespace AnnotationIO {

QVector<SaveFormat> supportedFormats()
{
    return {
        SaveFormat::VocXml,
        SaveFormat::Yolo,
        SaveFormat::CocoJson,
        SaveFormat::LabelMeJson,
        SaveFormat::KittiTxt,
        SaveFormat::Csv
    };
}

QString formatDisplayName(SaveFormat format)
{
    switch (format) {
    case SaveFormat::VocXml:
        return "Pascal VOC XML";
    case SaveFormat::Yolo:
        return "YOLO TXT";
    case SaveFormat::CocoJson:
        return "COCO JSON";
    case SaveFormat::LabelMeJson:
        return "LabelMe JSON";
    case SaveFormat::KittiTxt:
        return "KITTI TXT";
    case SaveFormat::Csv:
        return "CSV";
    }
    return "未知";
}

QString formatDirectoryName(SaveFormat format)
{
    switch (format) {
    case SaveFormat::VocXml:
        return "xml_labels";
    case SaveFormat::Yolo:
        return "yolo_labels";
    case SaveFormat::CocoJson:
        return "coco_labels";
    case SaveFormat::LabelMeJson:
        return "labelme_labels";
    case SaveFormat::KittiTxt:
        return "kitti_labels";
    case SaveFormat::Csv:
        return "csv_labels";
    }
    return "unknown_labels";
}

QString formatDescription(SaveFormat format)
{
    switch (format) {
    case SaveFormat::VocXml:
        return "每张图片一个 Pascal VOC XML 文件";
    case SaveFormat::Yolo:
        return "每张图片一个归一化检测框 TXT，并维护 classes.txt";
    case SaveFormat::CocoJson:
        return "整个数据集写入一个 instances.json";
    case SaveFormat::LabelMeJson:
        return "每张图片一个 LabelMe rectangle JSON";
    case SaveFormat::KittiTxt:
        return "每张图片一个 KITTI object detection TXT";
    case SaveFormat::Csv:
        return "整个数据集写入一个 annotations.csv";
    }
    return QString();
}

bool isDatasetLevelFormat(SaveFormat format)
{
    return format == SaveFormat::CocoJson || format == SaveFormat::Csv;
}

bool save(SaveFormat format,
          const QString& imagePath,
          const QString& outputRoot,
          const QSize& imageSize,
          const QVector<Annotation>& annotations,
          QString* errorMessage)
{
    QStringList classes;
    for (const Annotation& annotation : annotations) {
        const QString label = annotation.label.trimmed();
        if (!label.isEmpty() && !classes.contains(label)) {
            classes.append(label);
        }
    }
    return saveImage(
        format, imagePath, outputRoot, imageSize, annotations, classes, errorMessage);
}

bool saveImage(SaveFormat format,
               const QString& imagePath,
               const QString& outputRoot,
               const QSize& imageSize,
               const QVector<Annotation>& annotations,
               const QStringList& classNames,
               QString* errorMessage)
{
    if (imagePath.isEmpty() || outputRoot.isEmpty() || imageSize.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "缺少图片、输出目录或图片尺寸。";
        }
        return false;
    }

    switch (format) {
    case SaveFormat::VocXml:
        if (!writeFormatClasses(format, outputRoot, classNames, errorMessage)) {
            return false;
        }
        return saveVoc(imagePath, outputRoot, imageSize, annotations, errorMessage);
    case SaveFormat::Yolo:
        return saveYolo(imagePath, outputRoot, imageSize, annotations, classNames, errorMessage);
    case SaveFormat::LabelMeJson:
        return saveLabelMe(imagePath, outputRoot, imageSize, annotations, classNames, errorMessage);
    case SaveFormat::KittiTxt:
        return saveKitti(imagePath, outputRoot, imageSize, annotations, classNames, errorMessage);
    case SaveFormat::CocoJson:
    case SaveFormat::Csv:
        if (errorMessage) {
            *errorMessage = QString("%1 是整数据集格式，请使用整数据集保存。")
                                .arg(formatDisplayName(format));
        }
        return false;
    }

    if (errorMessage) {
        *errorMessage = "不支持的标注格式。";
    }
    return false;
}

bool saveDataset(SaveFormat format,
                 const QStringList& imagePaths,
                 const QString& outputRoot,
                 const QHash<QString, QVector<Annotation>>& annotationsByImage,
                 const QStringList& classNames,
                 QString* errorMessage)
{
    if (imagePaths.isEmpty() || outputRoot.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "缺少图片列表或输出目录。";
        }
        return false;
    }
    const QStringList classes = normalizedClasses(classNames, annotationsByImage);
    if (format == SaveFormat::CocoJson) {
        return saveCocoDataset(imagePaths, outputRoot, annotationsByImage, classes, errorMessage);
    }
    if (format == SaveFormat::Csv) {
        return saveCsvDataset(imagePaths, outputRoot, annotationsByImage, classes, errorMessage);
    }

    QSet<QString> baseNames;
    for (const QString& imagePath : imagePaths) {
        const QString baseName = baseNameForImage(imagePath).toLower();
        if (baseNames.contains(baseName)) {
            if (errorMessage) {
                *errorMessage = QString(
                    "%1 使用每图一个标签文件，图片中存在重复主文件名：%2")
                                    .arg(formatDisplayName(format), baseName);
            }
            return false;
        }
        baseNames.insert(baseName);
    }

    if (!writeFormatClasses(format, outputRoot, classes, errorMessage)) {
        return false;
    }
    for (const QString& imagePath : imagePaths) {
        const QSize imageSize = QImageReader(imagePath).size();
        if (imageSize.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QString("无法读取图片尺寸：%1").arg(imagePath);
            }
            return false;
        }
        if (!saveImage(
                format,
                imagePath,
                outputRoot,
                imageSize,
                annotationsByImage.value(imagePath),
                classes,
                errorMessage)) {
            return false;
        }
    }
    return true;
}

bool convertDataset(SaveFormat sourceFormat,
                    SaveFormat targetFormat,
                    const QStringList& imagePaths,
                    const QString& sourceRoot,
                    const QString& targetRoot,
                    int* annotationCount,
                    QStringList* classNames,
                    QString* errorMessage)
{
    if (sourceFormat == targetFormat) {
        if (errorMessage) {
            *errorMessage = "源格式和目标格式相同。";
        }
        return false;
    }

    QStringList classes = readClassNames(sourceFormat, sourceRoot, errorMessage);
    QHash<QString, QVector<Annotation>> dataset;
    int total = 0;
    bool foundAny = false;
    for (const QString& imagePath : imagePaths) {
        const QSize imageSize = QImageReader(imagePath).size();
        if (imageSize.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QString("无法读取图片尺寸：%1").arg(imagePath);
            }
            return false;
        }

        QVector<Annotation> annotations;
        if (exists(sourceFormat, imagePath, sourceRoot)) {
            foundAny = true;
            QString loadError;
            if (!load(
                    sourceFormat,
                    imagePath,
                    sourceRoot,
                    imageSize,
                    &annotations,
                    &loadError)) {
                if (errorMessage) {
                    *errorMessage = QString("%1：%2")
                                        .arg(QFileInfo(imagePath).fileName(), loadError);
                }
                return false;
            }
        }
        dataset.insert(imagePath, annotations);
        total += annotations.size();
        for (const Annotation& annotation : annotations) {
            const QString label = annotation.label.trimmed();
            if (!label.isEmpty() && !classes.contains(label)) {
                classes.append(label);
            }
        }
    }

    if (!foundAny) {
        if (errorMessage) {
            *errorMessage = QString("没有找到 %1 标注。").arg(formatDisplayName(sourceFormat));
        }
        return false;
    }
    if (!saveDataset(
            targetFormat,
            imagePaths,
            targetRoot,
            dataset,
            classes,
            errorMessage)) {
        return false;
    }
    if (annotationCount) {
        *annotationCount = total;
    }
    if (classNames) {
        *classNames = classes;
    }
    return true;
}

bool load(SaveFormat format,
          const QString& imagePath,
          const QString& outputRoot,
          const QSize& imageSize,
          QVector<Annotation>* annotations,
          QString* errorMessage)
{
    if (!annotations) {
        if (errorMessage) {
            *errorMessage = "内部错误：标注目标为空。";
        }
        return false;
    }

    if (imagePath.isEmpty() || outputRoot.isEmpty() || imageSize.isEmpty()) {
        annotations->clear();
        return true;
    }

    switch (format) {
    case SaveFormat::VocXml:
        return loadVoc(imagePath, outputRoot, imageSize, annotations, errorMessage);
    case SaveFormat::Yolo:
        return loadYolo(imagePath, outputRoot, imageSize, annotations, errorMessage);
    case SaveFormat::CocoJson:
        return loadCoco(imagePath, outputRoot, imageSize, annotations, errorMessage);
    case SaveFormat::LabelMeJson:
        return loadLabelMe(imagePath, outputRoot, imageSize, annotations, errorMessage);
    case SaveFormat::KittiTxt:
        return loadKitti(imagePath, outputRoot, imageSize, annotations, errorMessage);
    case SaveFormat::Csv:
        return loadCsv(imagePath, outputRoot, imageSize, annotations, errorMessage);
    }

    if (errorMessage) {
        *errorMessage = "不支持的标注格式。";
    }
    return false;
}

bool exists(SaveFormat format,
            const QString& imagePath,
            const QString& outputRoot)
{
    switch (format) {
    case SaveFormat::VocXml:
        for (const QString& candidate : vocLoadCandidates(imagePath, outputRoot)) {
            if (QFileInfo::exists(candidate)) {
                return true;
            }
        }
        return false;
    case SaveFormat::Yolo:
        for (const auto& candidate : yoloLoadCandidates(imagePath, outputRoot)) {
            if (QFileInfo::exists(candidate.first)) {
                return true;
            }
        }
        return false;
    case SaveFormat::CocoJson:
        return !findCocoPath(outputRoot).isEmpty();
    case SaveFormat::LabelMeJson:
        return !findFirstExisting(labelMeLoadCandidates(imagePath, outputRoot)).isEmpty();
    case SaveFormat::KittiTxt:
        return !findFirstExisting(kittiLoadCandidates(imagePath, outputRoot)).isEmpty();
    case SaveFormat::Csv:
        return !findCsvPath(outputRoot).isEmpty();
    }
    return false;
}

QStringList readClassNames(SaveFormat format,
                           const QString& outputRoot,
                           QString* errorMessage)
{
    QStringList classes = cleanClassNames(
        readClasses(classCatalogPathFor(format, outputRoot)));
    if (classes.isEmpty()) {
        for (const QString& nestedRoot : nestedLegacyRoots(outputRoot)) {
            if (QFileInfo(nestedRoot).fileName().compare(
                    formatDirectoryName(format),
                    Qt::CaseInsensitive) != 0) {
                continue;
            }
            classes = cleanClassNames(
                readClasses(QDir(nestedRoot).filePath("classes.txt")));
            if (!classes.isEmpty()) {
                break;
            }
        }
    }
    if (!classes.isEmpty()) {
        return classes;
    }

    if (format == SaveFormat::Yolo) {
        const QStringList candidates = {
            classesPathFor(outputRoot),
            QDir(outputRoot).filePath("classes.txt"),
            QDir(outputRoot).filePath("data.yaml"),
            QDir(outputRoot).filePath("dataset.yaml")
        };
        for (const QString& candidate : candidates) {
            classes = cleanClassNames(readYoloClasses(candidate));
            if (!classes.isEmpty()) {
                return classes;
            }
        }
        return {};
    }
    if (format == SaveFormat::CocoJson && !findCocoPath(outputRoot).isEmpty()) {
        QJsonObject root;
        if (!parseCoco(outputRoot, &root, errorMessage)) {
            return {};
        }
        QVector<QPair<int, QString>> ordered;
        for (const QJsonValue& value : root.value("categories").toArray()) {
            const QJsonObject category = value.toObject();
            ordered.append({category.value("id").toInt(), category.value("name").toString().trimmed()});
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        for (const auto& item : ordered) {
            if (!item.second.isEmpty() && !classes.contains(item.second)) {
                classes.append(item.second);
            }
        }
    }
    return classes;
}

} // namespace AnnotationIO
