#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
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
        return annotations->count() > 0 && classes->count() > 0 ? 0 : 1;
    }

    QTemporaryDir tempDir;
    if (!expect(tempDir.isValid(), "无法创建临时目录")) {
        return 1;
    }

    const QString imagesDir = QDir(tempDir.path()).filePath("images");
    const QString xmlDir = QDir(tempDir.path()).filePath("labels");
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
    if (!expect(window.openDataset(imagesDir, xmlDir, &error), "窗口打开数据集失败：" + error)) {
        return 1;
    }

    QListWidget* annotations = window.findChild<QListWidget*>("currentAnnotationsList");
    QListWidget* classes = window.findChild<QListWidget*>("datasetClassesList");
    QLabel* formatLabel = window.findChild<QLabel*>("annotationFormatLabel");
    QAction* saveAsAction = window.findChild<QAction*>("saveAsFormatAction");
    if (!expect(annotations != nullptr, "找不到当前图片标注列表") ||
        !expect(classes != nullptr, "找不到数据集标签列表") ||
        !expect(formatLabel != nullptr &&
                    formatLabel->text().contains("Pascal VOC XML"),
                "当前标签格式未锁定为 XML") ||
        !expect(saveAsAction != nullptr && saveAsAction->isEnabled(), "另存为操作不可用") ||
        !expect(window.findChild<QWidget*>("annotationFormatCombo") == nullptr,
                "工具栏不应再提供可切换格式的下拉框") ||
        !expect(annotations->count() == 2, "当前图片应显示 2 个历史标注") ||
        !expect(annotations->editTriggers() == QAbstractItemView::NoEditTriggers,
                "当前图片标注列表不应允许自由文字编辑") ||
        !expect(!(annotations->item(0)->flags() & Qt::ItemIsEditable),
                "当前图片标注项不应允许创建新标签") ||
        !expect(classes->count() == 2, "数据集标签汇总应显示 2 个历史类别") ||
        !expect(annotations->item(0)->text() == QString::fromUtf8("车辆"), "第一个历史标签不正确") ||
        !expect(annotations->item(1)->text() == QString::fromUtf8("行人"), "第二个历史标签不正确") ||
        !expect(classes->item(0)->toolTip().contains("全数据集标注：1 个"),
                "类别提示中的历史标注数不正确")) {
        return 1;
    }

    const QString yoloDir = QDir(tempDir.path()).filePath("labels_yolo");
    QHash<QString, QVector<Annotation>> yoloDataset;
    yoloDataset.insert(
        imagePath,
        {
            {QRectF(10, 20, 100, 200), QString::fromUtf8("车辆")},
            {QRectF(300, 100, 100, 350), QString::fromUtf8("行人")}
        });
    if (!expect(
            AnnotationIO::saveDataset(
                AnnotationIO::SaveFormat::Yolo,
                QStringList{imagePath},
                yoloDir,
                yoloDataset,
                QStringList{QString::fromUtf8("车辆"), QString::fromUtf8("行人")},
                &error),
            "无法创建标签目录切换测试所需的 YOLO 标注：" + error)) {
        return 1;
    }
    error.clear();
    if (!expect(
            window.openDataset(imagesDir, yoloDir, &error),
            "无法切换到 YOLO 标签目录：" + error) ||
        !expect(
            formatLabel->text().contains("YOLO TXT"),
            "切换标签目录后格式未变为 YOLO") ||
        !expect(annotations->count() == 2, "切换到 YOLO 后标注数量不正确")) {
        return 1;
    }
    error.clear();
    if (!expect(
            window.openDataset(imagesDir, xmlDir, &error),
            "无法切回 XML 标签目录：" + error) ||
        !expect(
            formatLabel->text().contains("Pascal VOC XML"),
            "切回原标签目录后格式未恢复为 XML") ||
        !expect(annotations->count() == 2, "切回原标签目录后标注数量不正确")) {
        return 1;
    }

    const QString siblingRoot = QDir(tempDir.path()).filePath("root_with_siblings");
    const QString siblingImages = QDir(siblingRoot).filePath("Image");
    const QString siblingLabels = QDir(siblingRoot).filePath("label");
    if (!expect(QDir().mkpath(siblingImages), "无法创建同级 Image 目录") ||
        !expect(QDir().mkpath(siblingLabels), "无法创建同级 label 目录")) {
        return 1;
    }
    const QString siblingImagePath = QDir(siblingImages).filePath("根目录打开.png");
    if (!expect(image.save(siblingImagePath), "无法保存根目录打开测试图片")) {
        return 1;
    }
    QFile siblingXml(QDir(siblingLabels).filePath("根目录打开.xml"));
    if (!expect(
            siblingXml.open(QIODevice::WriteOnly | QIODevice::Text),
            "无法写入根目录打开测试 XML")) {
        return 1;
    }
    siblingXml.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<annotation>
  <object><name>设备</name><bndbox><xmin>20</xmin><ymin>30</ymin><xmax>120</xmax><ymax>130</ymax></bndbox></object>
</annotation>
)");
    siblingXml.close();

    MainWindow siblingWindow;
    error.clear();
    if (!expect(
            siblingWindow.openDataset(siblingRoot, siblingLabels, &error),
            "无法使用 Image/label 两个目录打开数据集：" + error)) {
        return 1;
    }
    QListWidget* siblingAnnotations =
        siblingWindow.findChild<QListWidget*>("currentAnnotationsList");
    QListWidget* siblingClasses =
        siblingWindow.findChild<QListWidget*>("datasetClassesList");
    if (!expect(
            siblingAnnotations && siblingAnnotations->count() == 1,
            "选择同级 label 目录后未加载标注") ||
        !expect(
            siblingClasses && siblingClasses->count() == 1,
            "选择同级 label 目录后未汇总类别")) {
        return 1;
    }

    QTextStream(stdout) << "MainWindowTests passed\n";
    return 0;
}
