#include "AnnotationIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
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
    return QDir(outputRoot).filePath(QString("xml_labels/%1.xml").arg(baseNameForImage(imagePath)));
}

QString yoloPathFor(const QString& imagePath, const QString& outputRoot)
{
    return QDir(outputRoot).filePath(QString("yolo_labels/%1.txt").arg(baseNameForImage(imagePath)));
}

QString classesPathFor(const QString& outputRoot)
{
    return QDir(outputRoot).filePath("yolo_labels/classes.txt");
}

QStringList vocLoadCandidates(const QString& imagePath, const QString& outputRoot)
{
    const QString baseName = baseNameForImage(imagePath) + ".xml";
    const QString imageDir = QFileInfo(imagePath).absolutePath();
    const QString datasetRoot = QFileInfo(imageDir).absolutePath();
    QStringList paths = {
        QDir(outputRoot).filePath("xml_labels/" + baseName),
        QDir(outputRoot).filePath(baseName),
        QDir(imageDir).filePath(baseName),
        QDir(imageDir).filePath("xml_labels/" + baseName),
        QDir(datasetRoot).filePath("Annotations/" + baseName),
        QDir(datasetRoot).filePath("annotations/" + baseName),
        QDir(datasetRoot).filePath("xml_labels/" + baseName)
    };
    paths.removeDuplicates();
    return paths;
}

QVector<QPair<QString, QString>> yoloLoadCandidates(const QString& imagePath, const QString& outputRoot)
{
    const QString baseName = baseNameForImage(imagePath) + ".txt";
    const QString imageDir = QFileInfo(imagePath).absolutePath();
    const QString datasetRoot = QFileInfo(imageDir).absolutePath();
    QVector<QPair<QString, QString>> candidates = {
        {QDir(outputRoot).filePath("yolo_labels/" + baseName), QDir(outputRoot).filePath("yolo_labels/classes.txt")},
        {QDir(outputRoot).filePath(baseName), QDir(outputRoot).filePath("classes.txt")},
        {QDir(imageDir).filePath("labels/" + baseName), QDir(imageDir).filePath("labels/classes.txt")},
        {QDir(imageDir).filePath(baseName), QDir(imageDir).filePath("classes.txt")},
        {QDir(datasetRoot).filePath("labels/" + baseName), QDir(datasetRoot).filePath("labels/classes.txt")},
        {QDir(datasetRoot).filePath("labels/" + baseName), QDir(datasetRoot).filePath("classes.txt")},
        {QDir(datasetRoot).filePath("yolo_labels/" + baseName), QDir(datasetRoot).filePath("yolo_labels/classes.txt")}
    };
    return candidates;
}

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
        const QString name = in.readLine().trimmed();
        if (!name.isEmpty() && !classes.contains(name)) {
            classes.append(name);
        }
    }
    return classes;
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
    const QString dirPath = QDir(outputRoot).filePath("xml_labels");
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
              QString* errorMessage)
{
    const QString dirPath = QDir(outputRoot).filePath("yolo_labels");
    if (!ensureDir(dirPath, errorMessage)) {
        return false;
    }

    const QString classesPath = classesPathFor(outputRoot);
    QStringList classes = readClasses(classesPath);
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
    for (const auto& candidate : yoloLoadCandidates(imagePath, outputRoot)) {
        if (QFileInfo::exists(candidate.first) && QFileInfo::exists(candidate.second)) {
            annotationPath = candidate.first;
            classesPath = candidate.second;
            break;
        }
    }
    if (annotationPath.isEmpty()) {
        annotations->clear();
        return true;
    }

    QFile annotationFile(annotationPath);
    const QStringList classes = readClasses(classesPath);
    if (classes.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "YOLO 的 classes.txt 不存在或为空。";
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

} // namespace

namespace AnnotationIO {

QString formatDisplayName(SaveFormat format)
{
    switch (format) {
    case SaveFormat::VocXml:
        return "XML";
    case SaveFormat::Yolo:
        return "YOLO";
    }
    return "未知";
}

bool save(SaveFormat format,
          const QString& imagePath,
          const QString& outputRoot,
          const QSize& imageSize,
          const QVector<Annotation>& annotations,
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
        return saveVoc(imagePath, outputRoot, imageSize, annotations, errorMessage);
    case SaveFormat::Yolo:
        return saveYolo(imagePath, outputRoot, imageSize, annotations, errorMessage);
    }

    if (errorMessage) {
        *errorMessage = "不支持的标注格式。";
    }
    return false;
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
            if (QFileInfo::exists(candidate.first) && QFileInfo::exists(candidate.second)) {
                return true;
            }
        }
        return false;
    }
    return false;
}

} // namespace AnnotationIO
