#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

namespace {

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
    QApplication app(argc, argv);
    QApplication::setApplicationName("AnnotaFlowTests");
    QApplication::setOrganizationName("AnnotaFlowTests");

    const QStringList args = app.arguments();
    if (args.size() == 3) {
        QTimer messageBoxWatcher;
        QObject::connect(&messageBoxWatcher, &QTimer::timeout, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                QMessageBox* box = qobject_cast<QMessageBox*>(widget);
                if (!box) {
                    continue;
                }
                QTextStream(stderr)
                    << "MESSAGE_BOX: " << box->windowTitle() << " | " << box->text() << "\n";
                box->reject();
            }
        });
        messageBoxWatcher.start(50);

        MainWindow window;
        QString error;
        if (!expect(window.openDataset(args[1], args[2], &error), "窗口打开真实数据集失败：" + error)) {
            return 1;
        }

        QListWidget* annotations = window.findChild<QListWidget*>("currentAnnotationsList");
        QListWidget* classes = window.findChild<QListWidget*>("datasetClassesList");
        if (!expect(annotations != nullptr, "找不到当前图片标注列表") ||
            !expect(classes != nullptr, "找不到数据集标签列表")) {
            return 1;
        }

        QTextStream out(stdout);
        out << "annotations=" << annotations->count() << "\n";
        out << "classes=" << classes->count() << "\n";
        for (int i = 0; i < classes->count(); ++i) {
            out << "class[" << i << "]=" << classes->item(i)->text() << "\n";
            out << "classTooltip[" << i << "]=" << classes->item(i)->toolTip() << "\n";
        }
        return annotations->count() > 0 &&
               classes->count() > 0 &&
               classes->item(0)->toolTip().contains(
                   QString("全数据集标注：%1 个").arg(annotations->count()))
            ? 0
            : 1;
    }

    QTemporaryDir tempDir;
    if (!expect(tempDir.isValid(), "无法创建临时目录")) {
        return 1;
    }

    const QString imagesDir = QDir(tempDir.path()).filePath("images");
    const QString outputDir = QDir(tempDir.path()).filePath("output");
    const QString xmlDir = QDir(outputDir).filePath("xml_labels");
    if (!expect(QDir().mkpath(imagesDir), "无法创建图片目录") ||
        !expect(QDir().mkpath(xmlDir), "无法创建标注目录")) {
        return 1;
    }

    const QString imagePath = QDir(imagesDir).filePath("已有标注.png");
    QImage image(640, 480, QImage::Format_RGB32);
    image.fill(Qt::white);
    if (!expect(image.save(imagePath), "无法保存测试图片")) {
        return 1;
    }

    QFile xmlFile(QDir(xmlDir).filePath("已有标注.xml"));
    if (!expect(xmlFile.open(QIODevice::WriteOnly | QIODevice::Text), "无法写入测试 XML")) {
        return 1;
    }
    xmlFile.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<annotation>
  <object><name>车辆</name><bndbox><xmin>10</xmin><ymin>20</ymin><xmax>110</xmax><ymax>220</ymax></bndbox></object>
  <object><name>行人</name><bndbox><xmin>300</xmin><ymin>100</ymin><xmax>400</xmax><ymax>450</ymax></bndbox></object>
</annotation>
)");
    xmlFile.close();

    MainWindow window;
    QString error;
    if (!expect(window.openDataset(imagesDir, outputDir, &error), "窗口打开数据集失败：" + error)) {
        return 1;
    }

    QListWidget* annotations = window.findChild<QListWidget*>("currentAnnotationsList");
    QListWidget* classes = window.findChild<QListWidget*>("datasetClassesList");
    if (!expect(annotations != nullptr, "找不到当前图片标注列表") ||
        !expect(classes != nullptr, "找不到数据集标签列表") ||
        !expect(annotations->count() == 2, "当前图片应显示 2 个历史标注") ||
        !expect(classes->count() == 2, "数据集标签汇总应显示 2 个历史类别") ||
        !expect(annotations->item(0)->text() == QString::fromUtf8("车辆"), "第一个历史标签不正确") ||
        !expect(annotations->item(1)->text() == QString::fromUtf8("行人"), "第二个历史标签不正确") ||
        !expect(classes->item(0)->toolTip().contains("全数据集标注：1 个"),
                "类别提示中的历史标注数不正确")) {
        return 1;
    }

    QTextStream(stdout) << "MainWindowTests passed\n";
    return 0;
}
