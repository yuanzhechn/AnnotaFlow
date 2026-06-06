#include "AnnotationIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

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

    QTextStream(stdout) << "AnnotationIOTests passed\n";
    return 0;
}
