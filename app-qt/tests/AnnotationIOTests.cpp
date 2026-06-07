#include "AnnotationIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtMath>

namespace {

bool writeTextFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(contents) == contents.size();
}

bool expect(bool condition, const QString& message)
{
    if (condition) {
        return true;
    }
    QTextStream(stderr) << "FAIL: " << message << "\n";
    return false;
}

bool rectClose(const QRectF& left, const QRectF& right)
{
    constexpr double tolerance = 0.01;
    return qAbs(left.x() - right.x()) < tolerance &&
           qAbs(left.y() - right.y()) < tolerance &&
           qAbs(left.width() - right.width()) < tolerance &&
           qAbs(left.height() - right.height()) < tolerance;
}

bool stringListsEqual(const QStringList& left, const QStringList& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (int i = 0; i < left.size(); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tempDir;
    if (!expect(tempDir.isValid(), "无法创建临时目录")) {
        return 1;
    }

    const QString xmlDir = QDir(tempDir.path()).filePath("xml_labels");
    if (!expect(QDir().mkpath(xmlDir), "无法创建 xml_labels")) {
        return 1;
    }

    const QSize imageSize(640, 480);
    const QString imagePath = QDir(tempDir.path()).filePath("示例图片.png");
    const QString xmlPath = QDir(xmlDir).filePath("示例图片.xml");
    const QByteArray validXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<annotation>
  <filename>示例图片.png</filename>
  <size><width>640</width><height>480</height><depth>3</depth></size>
  <object>
    <name>车辆</name>
    <bndbox><xmin>10</xmin><ymin>20</ymin><xmax>110</xmax><ymax>220</ymax></bndbox>
  </object>
  <object>
    <name>行人</name>
    <pose>Unspecified</pose>
    <bndbox><xmin>300</xmin><ymin>100</ymin><xmax>400</xmax><ymax>450</ymax></bndbox>
  </object>
</annotation>
)";

    if (!expect(writeTextFile(xmlPath, validXml), "无法写入合法 XML")) {
        return 1;
    }

    QVector<Annotation> annotations;
    QString error;
    bool ok = AnnotationIO::load(
        AnnotationIO::SaveFormat::VocXml,
        imagePath,
        tempDir.path(),
        imageSize,
        &annotations,
        &error);
    if (!expect(ok, "合法 XML 加载失败：" + error) ||
        !expect(annotations.size() == 2, "合法 XML 应加载 2 个框") ||
        !expect(annotations[0].label == QString::fromUtf8("车辆"), "第一个标签不正确") ||
        !expect(annotations[0].rect == QRectF(10, 20, 100, 200), "第一个框坐标不正确") ||
        !expect(annotations[1].label == QString::fromUtf8("行人"), "第二个标签不正确")) {
        return 1;
    }

    const QByteArray emptyXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<annotation>
  <filename>示例图片.png</filename>
  <size><width>640</width><height>480</height><depth>3</depth></size>
</annotation>
)";
    if (!expect(writeTextFile(xmlPath, emptyXml), "无法写入空标注 XML")) {
        return 1;
    }
    annotations = {{QRectF(1, 1, 10, 10), "旧数据"}};
    error.clear();
    ok = AnnotationIO::load(
        AnnotationIO::SaveFormat::VocXml,
        imagePath,
        tempDir.path(),
        imageSize,
        &annotations,
        &error);
    if (!expect(ok, "合法空 XML 应加载成功") ||
        !expect(annotations.isEmpty(), "合法空 XML 应返回 0 个框")) {
        return 1;
    }

    if (!expect(writeTextFile(xmlPath, "<annotation><object>"), "无法写入损坏 XML")) {
        return 1;
    }
    annotations = {{QRectF(1, 1, 10, 10), "保留数据"}};
    error.clear();
    ok = AnnotationIO::load(
        AnnotationIO::SaveFormat::VocXml,
        imagePath,
        tempDir.path(),
        imageSize,
        &annotations,
        &error);
    if (!expect(!ok, "损坏 XML 必须返回失败") ||
        !expect(annotations.size() == 1 && annotations[0].label == "保留数据",
                "加载失败时不能清空调用方已有数据")) {
        return 1;
    }

    const QString siblingDatasetRoot = QDir(tempDir.path()).filePath("sibling_dataset");
    const QString siblingImageDir = QDir(siblingDatasetRoot).filePath("Image");
    const QString siblingLabelDir = QDir(siblingDatasetRoot).filePath("label");
    if (!expect(QDir().mkpath(siblingImageDir), "无法创建同级 Image 目录") ||
        !expect(QDir().mkpath(siblingLabelDir), "无法创建同级 label 目录")) {
        return 1;
    }
    const QString siblingImagePath = QDir(siblingImageDir).filePath("同级目录样例.png");
    QImage siblingImage(imageSize, QImage::Format_RGB32);
    siblingImage.fill(Qt::white);
    if (!expect(siblingImage.save(siblingImagePath), "无法保存同级目录测试图片") ||
        !expect(
            writeTextFile(
                QDir(siblingLabelDir).filePath("同级目录样例.xml"),
                validXml),
            "无法写入同级 label XML")) {
        return 1;
    }
    QVector<Annotation> siblingAnnotations;
    error.clear();
    if (!expect(
            AnnotationIO::exists(
                AnnotationIO::SaveFormat::VocXml,
                siblingImagePath,
                siblingImageDir),
            "未发现 Image/label 同级布局中的 XML") ||
        !expect(
            AnnotationIO::load(
                AnnotationIO::SaveFormat::VocXml,
                siblingImagePath,
                siblingImageDir,
                imageSize,
                &siblingAnnotations,
                &error),
            "无法加载 Image/label 同级布局中的 XML：" + error) ||
        !expect(siblingAnnotations.size() == 2, "同级 label XML 的框数量不正确")) {
        return 1;
    }

    const QString siblingYoloImagePath = QDir(siblingImageDir).filePath("同级YOLO.png");
    if (!expect(siblingImage.save(siblingYoloImagePath), "无法保存同级 YOLO 测试图片") ||
        !expect(
            writeTextFile(
                QDir(siblingLabelDir).filePath("同级YOLO.txt"),
                "0 0.5 0.5 0.25 0.5\n"),
            "无法写入同级 label YOLO") ||
        !expect(
            writeTextFile(
                QDir(siblingLabelDir).filePath("classes.txt"),
                "车辆\n"),
            "无法写入同级 label 类别文件")) {
        return 1;
    }
    siblingAnnotations.clear();
    error.clear();
    if (!expect(
            AnnotationIO::load(
                AnnotationIO::SaveFormat::Yolo,
                siblingYoloImagePath,
                siblingImageDir,
                imageSize,
                &siblingAnnotations,
                &error),
            "无法加载 Image/label 同级布局中的 YOLO：" + error) ||
        !expect(
            siblingAnnotations.size() == 1 &&
                siblingAnnotations[0].label == QString::fromUtf8("车辆"),
            "同级 label YOLO 标签不正确")) {
        return 1;
    }

    const QString fourPointImagePath =
        QDir(siblingImageDir).filePath("四点矩形.png");
    if (!expect(siblingImage.save(fourPointImagePath), "无法保存四点矩形测试图片")) {
        return 1;
    }
    const QByteArray fourPointLabelMe = R"({
  "version": "4.0.0-beta.5",
  "shapes": [
    {
      "label": "battery",
      "points": [[100, 120], [180, 120], [180, 220], [100, 220]],
      "shape_type": "rectangle"
    }
  ],
  "imageWidth": 640,
  "imageHeight": 480
})";
    if (!expect(
            writeTextFile(
                QDir(siblingLabelDir).filePath("四点矩形.json"),
                fourPointLabelMe),
            "无法写入四点矩形 LabelMe JSON")) {
        return 1;
    }
    siblingAnnotations.clear();
    error.clear();
    if (!expect(
            AnnotationIO::load(
                AnnotationIO::SaveFormat::LabelMeJson,
                fourPointImagePath,
                siblingLabelDir,
                imageSize,
                &siblingAnnotations,
                &error),
            "无法加载四点矩形 LabelMe JSON：" + error) ||
        !expect(
            siblingAnnotations.size() == 1 &&
                siblingAnnotations[0].label == "battery" &&
                rectClose(
                    siblingAnnotations[0].rect,
                    QRectF(100, 120, 80, 100)),
            "四点矩形 LabelMe 外接框解析不正确")) {
        return 1;
    }

    const QString matrixImagePath = QDir(tempDir.path()).filePath("格式矩阵.png");
    QImage matrixImage(imageSize, QImage::Format_RGB32);
    matrixImage.fill(Qt::white);
    if (!expect(matrixImage.save(matrixImagePath), "无法保存格式矩阵测试图片")) {
        return 1;
    }

    const QVector<Annotation> expectedAnnotations = {
        {QRectF(10, 20, 100, 200), QString::fromUtf8("车辆")},
        {QRectF(300, 100, 100, 350), QString::fromUtf8("行人")}
    };
    const QStringList expectedClasses = {
        QString::fromUtf8("车辆"),
        QString::fromUtf8("行人")
    };
    const QStringList imagePaths = {matrixImagePath};
    QHash<QString, QVector<Annotation>> dataset;
    dataset.insert(matrixImagePath, expectedAnnotations);

    for (const AnnotationIO::SaveFormat format : AnnotationIO::supportedFormats()) {
        const QString formatRoot = QDir(tempDir.path()).filePath(
            QString("matrix_%1").arg(static_cast<int>(format)));
        if (!expect(
                AnnotationIO::saveDataset(
                    format,
                    imagePaths,
                    formatRoot,
                    dataset,
                    expectedClasses,
                    &error),
                QString("%1 保存失败：%2")
                    .arg(AnnotationIO::formatDisplayName(format), error))) {
            return 1;
        }

        const QString legacyFormatDir = QDir(formatRoot).filePath(
            AnnotationIO::formatDirectoryName(format));
        if (!expect(!QDir(legacyFormatDir).exists(),
                    QString("%1 不应再创建格式子目录").arg(AnnotationIO::formatDisplayName(format))) ||
            !expect(!QDir(formatRoot).entryList(QDir::Files).isEmpty(),
                    QString("%1 未直接写入所选标签目录").arg(AnnotationIO::formatDisplayName(format)))) {
            return 1;
        }

        QVector<Annotation> roundTrip;
        error.clear();
        if (!expect(
                AnnotationIO::exists(format, matrixImagePath, formatRoot),
                QString("%1 保存后无法发现标注").arg(AnnotationIO::formatDisplayName(format))) ||
            !expect(
                AnnotationIO::load(
                    format,
                    matrixImagePath,
                    formatRoot,
                    imageSize,
                    &roundTrip,
                    &error),
                QString("%1 回读失败：%2").arg(AnnotationIO::formatDisplayName(format), error)) ||
            !expect(
                roundTrip.size() == expectedAnnotations.size(),
                QString("%1 回读框数量不正确").arg(AnnotationIO::formatDisplayName(format)))) {
            return 1;
        }

        for (int i = 0; i < roundTrip.size(); ++i) {
            if (!expect(roundTrip[i].label == expectedAnnotations[i].label,
                        QString("%1 第 %2 个标签不正确")
                            .arg(AnnotationIO::formatDisplayName(format))
                            .arg(i + 1)) ||
                !expect(rectClose(roundTrip[i].rect, expectedAnnotations[i].rect),
                        QString("%1 第 %2 个框坐标不正确")
                            .arg(AnnotationIO::formatDisplayName(format))
                            .arg(i + 1))) {
                return 1;
            }
        }

        const QStringList roundTripClasses = AnnotationIO::readClassNames(format, formatRoot, &error);
        if (!expect(
                stringListsEqual(roundTripClasses, expectedClasses),
                QString("%1 类别顺序不正确").arg(AnnotationIO::formatDisplayName(format)))) {
            return 1;
        }
    }

    const QString conversionRoot = QDir(tempDir.path()).filePath("conversion");
    error.clear();
    if (!expect(
            AnnotationIO::saveDataset(
                AnnotationIO::SaveFormat::Yolo,
                imagePaths,
                conversionRoot,
                dataset,
                expectedClasses,
                &error),
            "无法创建转换源 YOLO：" + error)) {
        return 1;
    }
    for (const AnnotationIO::SaveFormat target : AnnotationIO::supportedFormats()) {
        if (target == AnnotationIO::SaveFormat::Yolo) {
            continue;
        }
        const QString conversionTarget = QDir(tempDir.path()).filePath(
            QString("conversion_target_%1").arg(static_cast<int>(target)));
        int convertedCount = 0;
        QStringList convertedClasses;
        error.clear();
        if (!expect(
                AnnotationIO::convertDataset(
                    AnnotationIO::SaveFormat::Yolo,
                    target,
                    imagePaths,
                    conversionRoot,
                    conversionTarget,
                    &convertedCount,
                    &convertedClasses,
                    &error),
                QString("YOLO 转 %1 失败：%2")
                    .arg(AnnotationIO::formatDisplayName(target), error)) ||
            !expect(convertedCount == expectedAnnotations.size(), "转换后的标注计数不正确") ||
            !expect(stringListsEqual(convertedClasses, expectedClasses), "转换后的类别顺序不正确")) {
            return 1;
        }

        QVector<Annotation> converted;
        if (!expect(
                AnnotationIO::load(
                    target,
                    matrixImagePath,
                    conversionTarget,
                    imageSize,
                    &converted,
                    &error),
                QString("转换后的 %1 无法读取").arg(AnnotationIO::formatDisplayName(target))) ||
            !expect(converted.size() == expectedAnnotations.size(), "转换后的文件丢失标注")) {
            return 1;
        }
    }

    const QString externalYoloRoot = QDir(tempDir.path()).filePath("external_yolo");
    const QString externalImagesDir = QDir(externalYoloRoot).filePath("images/train");
    const QString externalLabelsDir = QDir(externalYoloRoot).filePath("labels/train");
    if (!expect(QDir().mkpath(externalImagesDir), "无法创建外部 YOLO 图片目录") ||
        !expect(QDir().mkpath(externalLabelsDir), "无法创建外部 YOLO 标签目录")) {
        return 1;
    }
    const QString externalImagePath = QDir(externalImagesDir).filePath("yaml_sample.png");
    if (!expect(matrixImage.save(externalImagePath), "无法保存外部 YOLO 测试图片") ||
        !expect(
            writeTextFile(
                QDir(externalLabelsDir).filePath("yaml_sample.txt"),
                "1 0.5 0.5 0.25 0.5\n"),
            "无法写入外部 YOLO 标签") ||
        !expect(
            writeTextFile(
                QDir(externalYoloRoot).filePath("data.yaml"),
                "names:\n  0: person\n  1: car\n"),
            "无法写入 data.yaml")) {
        return 1;
    }
    QVector<Annotation> externalYoloAnnotations;
    error.clear();
    if (!expect(
            AnnotationIO::exists(
                AnnotationIO::SaveFormat::Yolo,
                externalImagePath,
                externalYoloRoot),
            "未发现 images/train -> labels/train YOLO 标注") ||
        !expect(
            AnnotationIO::load(
                AnnotationIO::SaveFormat::Yolo,
                externalImagePath,
                externalYoloRoot,
                imageSize,
                &externalYoloAnnotations,
                &error),
            "无法读取 data.yaml YOLO：" + error) ||
        !expect(
            externalYoloAnnotations.size() == 1 &&
            externalYoloAnnotations[0].label == "car",
            "data.yaml 类别映射不正确")) {
        return 1;
    }

    QTextStream(stdout) << "AnnotationIOTests passed\n";
    return 0;
}
