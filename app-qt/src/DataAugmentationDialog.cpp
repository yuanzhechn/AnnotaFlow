#include "DataAugmentationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

struct RangeControl {
    QString key;
    QString name;
    QCheckBox* enabled = nullptr;
    QDoubleSpinBox* minimum = nullptr;
    QDoubleSpinBox* maximum = nullptr;
};

struct ToggleControl {
    QString key;
    QString name;
    QCheckBox* enabled = nullptr;
};

QJsonObject editSchemeConfiguration(
    int schemeNumber,
    const QJsonObject& existing,
    QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QString("增强方案 %1").arg(schemeNumber));
    dialog.resize(760, 720);
    QVBoxLayout* root = new QVBoxLayout(&dialog);

    QLabel* title = new QLabel(
        QString("增强方案 %1\n这组设置会生成一套增强版本；新增下一个方案时可以选择完全不同的方法。")
            .arg(schemeNumber),
        &dialog);
    title->setWordWrap(true);
    title->setStyleSheet(
        "QLabel { background: #f1f3f5; border: 1px solid #c9ced3;"
        " padding: 9px; font-weight: 600; }");
    root->addWidget(title);

    QWidget* general = new QWidget(&dialog);
    QFormLayout* generalLayout = new QFormLayout(general);
    QSpinBox* copies = new QSpinBox(general);
    copies->setRange(1, 100);
    copies->setValue(existing.value("copies_per_image").toInt(1));
    QDoubleSpinBox* probability = new QDoubleSpinBox(general);
    probability->setRange(0.05, 1.0);
    probability->setSingleStep(0.05);
    probability->setValue(existing.value("probability").toDouble(1.0));
    generalLayout->addRow("生成数量：", copies);
    generalLayout->addRow("每项应用概率：", probability);
    root->addWidget(general);

    QScrollArea* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    QWidget* content = new QWidget(scroll);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    QVector<RangeControl> ranges;
    QVector<ToggleControl> toggles;
    const QJsonObject existingOperations = existing.value("operations").toObject();

    auto addToggle = [&](QVBoxLayout* layout, const QString& key, const QString& name, const QString& description) {
        QCheckBox* box = new QCheckBox(name + "  " + description, content);
        box->setChecked(existingOperations.contains(key));
        layout->addWidget(box);
        toggles.append({key, name, box});
    };
    auto addRange = [&](QVBoxLayout* layout,
                        const QString& key,
                        const QString& name,
                        const QString& unit,
                        double low,
                        double high,
                        double step,
                        double defaultLow,
                        double defaultHigh) {
        QWidget* row = new QWidget(content);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 2, 0, 2);
        QCheckBox* enabled = new QCheckBox(name, row);
        QDoubleSpinBox* minimum = new QDoubleSpinBox(row);
        QDoubleSpinBox* maximum = new QDoubleSpinBox(row);
        const QJsonObject saved = existingOperations.value(key).toObject();
        enabled->setChecked(existingOperations.contains(key));
        for (QDoubleSpinBox* spin : {minimum, maximum}) {
            spin->setRange(low, high);
            spin->setSingleStep(step);
            spin->setDecimals(step < 0.1 ? 2 : (step < 1 ? 1 : 0));
        }
        minimum->setValue(saved.value("min").toDouble(defaultLow));
        maximum->setValue(saved.value("max").toDouble(defaultHigh));
        rowLayout->addWidget(enabled, 1);
        rowLayout->addWidget(
            new QLabel(
                QString("%1（可选 %2 ～ %3）")
                    .arg(unit)
                    .arg(low, 0, 'f', step < 0.1 ? 2 : (step < 1 ? 1 : 0))
                    .arg(high, 0, 'f', step < 0.1 ? 2 : (step < 1 ? 1 : 0)),
                row));
        rowLayout->addWidget(new QLabel("最小", row));
        rowLayout->addWidget(minimum);
        rowLayout->addWidget(new QLabel("最大", row));
        rowLayout->addWidget(maximum);
        layout->addWidget(row);
        ranges.append({key, name, enabled, minimum, maximum});
    };
    auto group = [&](const QString& name) {
        QGroupBox* box = new QGroupBox(name, content);
        QVBoxLayout* layout = new QVBoxLayout(box);
        contentLayout->addWidget(box);
        return layout;
    };

    QVBoxLayout* geometry = group("几何变换");
    addToggle(geometry, "horizontal_flip", "水平翻转", "同步检测框");
    addToggle(geometry, "vertical_flip", "垂直翻转", "同步检测框");
    addRange(geometry, "rotate", "随机旋转", "角度", -45, 45, 1, -15, 15);
    addRange(geometry, "translate", "随机平移", "比例", -0.5, 0.5, 0.01, -0.1, 0.1);
    addRange(geometry, "scale", "随机缩放", "倍率", 0.2, 2, 0.05, 0.8, 1.2);
    addRange(geometry, "crop", "随机/中心裁剪", "保留比例", 0.3, 1, 0.05, 0.7, 0.95);
    addRange(geometry, "shear", "仿射剪切", "角度", -30, 30, 1, -10, 10);
    addRange(geometry, "perspective", "透视变换", "比例", 0, 0.3, 0.01, 0.02, 0.08);

    QVBoxLayout* color = group("颜色增强");
    addRange(color, "brightness", "亮度", "系数", 0.2, 2, 0.05, 0.7, 1.3);
    addRange(color, "contrast", "对比度", "系数", 0.2, 2, 0.05, 0.7, 1.3);
    addRange(color, "saturation", "饱和度", "系数", 0, 2, 0.05, 0.6, 1.4);
    addRange(color, "hue", "色相", "角度", -90, 90, 1, -15, 15);
    addRange(color, "gamma", "Gamma", "系数", 0.2, 3, 0.05, 0.7, 1.5);
    addToggle(color, "grayscale", "灰度化", "三通道灰度");

    QVBoxLayout* noise = group("噪声、模糊与压缩");
    addRange(noise, "gaussian_noise", "高斯噪声", "标准差", 1, 80, 1, 5, 20);
    addToggle(noise, "salt_pepper", "椒盐噪声", "随机黑白点");
    addRange(noise, "gaussian_blur", "高斯模糊", "核尺寸", 1, 31, 2, 3, 9);
    addRange(noise, "motion_blur", "运动模糊", "核尺寸", 1, 41, 2, 5, 15);
    addRange(noise, "jpeg", "JPEG 压缩", "质量", 10, 100, 1, 45, 90);

    QVBoxLayout* occlusion = group("遮挡增强");
    addRange(occlusion, "cutout", "Cutout", "边长比例，单框遮挡不超过 40%", 0.02, 0.7, 0.01, 0.08, 0.25);
    addToggle(occlusion, "random_erasing", "Random Erasing", "随机擦除");
    addToggle(occlusion, "gridmask", "GridMask", "网格遮挡");
    addToggle(occlusion, "hide_seek", "Hide-and-Seek", "隐藏多个小块");

    QVBoxLayout* mixing = group("混合样本增强");
    addToggle(mixing, "mixup", "MixUp", "混合另一张图");
    addToggle(mixing, "cutmix", "CutMix", "粘贴另一张图区域");
    addToggle(mixing, "mosaic", "Mosaic", "四图拼接");
    addToggle(mixing, "copy_paste", "Copy-Paste", "复制目标实例");

    contentLayout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText("保存方案设置");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    QJsonObject operations;
    for (const ToggleControl& control : toggles) {
        if (control.enabled->isChecked()) {
            operations[control.key] = QJsonObject{{"enabled", true}};
        }
    }
    for (const RangeControl& control : ranges) {
        if (control.enabled->isChecked()) {
            operations[control.key] = QJsonObject{
                {"enabled", true},
                {"min", control.minimum->value()},
                {"max", control.maximum->value()}
            };
        }
    }
    if (operations.isEmpty()) {
        QMessageBox::information(parent, "未保存设置", "这个增强方案没有选择任何增强方法。");
        return {};
    }
    return {
        {"copies_per_image", copies->value()},
        {"probability", probability->value()},
        {"operations", operations}
    };
}

} // namespace

DataAugmentationDialog::DataAugmentationDialog(
    const QString& datasetRoot,
    const QStringList& imagePaths,
    QWidget* parent)
    : QDialog(parent),
      imageCount_(imagePaths.size())
{
    setWindowTitle("数据增强方案");
    resize(850, 680);
    QVBoxLayout* root = new QVBoxLayout(this);
    QLabel* notice = new QLabel(
        "建议完成全部标注并检查后再增强。点击“新增增强方案”时会弹出参数窗口；"
        "第一次、第二次及后续增强可以使用完全不同的方法和范围。每个方案会应用到全部原图。",
        this);
    notice->setWordWrap(true);
    notice->setStyleSheet(
        "QLabel { background: #f1f3f5; border: 1px solid #c9ced3;"
        " padding: 10px; font-weight: 600; }");
    root->addWidget(notice);

    QGroupBox* output = new QGroupBox("公共输出设置", this);
    QFormLayout* outputLayout = new QFormLayout(output);
    auto addFolder = [&](const QString& label, QLineEdit** edit, auto slot) {
        QWidget* row = new QWidget(output);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        *edit = new QLineEdit(row);
        QPushButton* choose = new QPushButton("选择...", row);
        rowLayout->addWidget(*edit, 1);
        rowLayout->addWidget(choose);
        connect(choose, &QPushButton::clicked, this, slot);
        outputLayout->addRow(label, row);
    };
    addFolder("增强图片目录：", &imagesFolderEdit_, &DataAugmentationDialog::chooseImagesFolder);
    addFolder("增强标签目录：", &labelsFolderEdit_, &DataAugmentationDialog::chooseLabelsFolder);
    imagesFolderEdit_->setText(datasetRoot + "/augmented/images");
    labelsFolderEdit_->setText(datasetRoot + "/augmented/labels");
    seedSpin_ = new QSpinBox(output);
    seedSpin_->setRange(0, 999999999);
    seedSpin_->setValue(20260615);
    outputLayout->addRow("随机种子：", seedSpin_);
    imageFormatCombo_ = new QComboBox(output);
    imageFormatCombo_->addItems({"保持原格式", "JPEG", "PNG"});
    outputLayout->addRow("输出图片格式：", imageFormatCombo_);
    root->addWidget(output);

    imagesList_ = new QListWidget(this);
    imagesList_->setObjectName("augmentationSchemesList");
    root->addWidget(imagesList_, 1);
    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* add = new QPushButton("新增增强方案...", this);
    QPushButton* edit = new QPushButton("编辑所选方案...", this);
    QPushButton* remove = new QPushButton("删除所选方案", this);
    actions->addWidget(add);
    actions->addWidget(edit);
    actions->addWidget(remove);
    actions->addStretch();
    root->addLayout(actions);
    connect(add, &QPushButton::clicked, this, &DataAugmentationDialog::addScheme);
    connect(edit, &QPushButton::clicked, this, &DataAugmentationDialog::editCurrentScheme);
    connect(remove, &QPushButton::clicked, this, &DataAugmentationDialog::removeCurrentScheme);
    connect(imagesList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        editCurrentScheme();
    });

    QLabel* naming = new QLabel(
        "命名示例：车辆01__hflip_rot-8_rcrop85.jpg。增强详情和来源仍会完整记录在 "
        "augmentation_manifest.json 中。",
        this);
    naming->setWordWrap(true);
    root->addWidget(naming);
    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("开始生成");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons, &QDialogButtonBox::accepted, this, &DataAugmentationDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    refreshImageList();
}

void DataAugmentationDialog::addScheme()
{
    const QJsonObject config =
        editSchemeConfiguration(schemes_.size() + 1, QJsonObject(), this);
    if (!config.isEmpty()) {
        schemes_.append(config);
        refreshImageList();
        imagesList_->setCurrentRow(schemes_.size() - 1);
    }
}

void DataAugmentationDialog::editCurrentScheme()
{
    const int row = imagesList_->currentRow();
    if (row < 0 || row >= schemes_.size()) {
        return;
    }
    const QJsonObject config =
        editSchemeConfiguration(row + 1, schemes_[row].toObject(), this);
    if (!config.isEmpty()) {
        schemes_[row] = config;
        refreshImageList();
        imagesList_->setCurrentRow(row);
    }
}

void DataAugmentationDialog::removeCurrentScheme()
{
    const int row = imagesList_->currentRow();
    if (row < 0 || row >= schemes_.size()) {
        return;
    }
    schemes_.removeAt(row);
    refreshImageList();
    if (!schemes_.isEmpty()) {
        imagesList_->setCurrentRow(qMin(row, schemes_.size() - 1));
    }
}

void DataAugmentationDialog::refreshImageList()
{
    const int selected = imagesList_->currentRow();
    imagesList_->clear();
    for (int i = 0; i < schemes_.size(); ++i) {
        const QJsonObject scheme = schemes_[i].toObject();
        QListWidgetItem* item = new QListWidgetItem(
            QString("方案 %1  -  %2")
                .arg(i + 1)
                .arg(configurationSummary(scheme)),
            imagesList_);
    }
    if (!schemes_.isEmpty()) {
        imagesList_->setCurrentRow(qBound(0, selected, schemes_.size() - 1));
    }
}

QString DataAugmentationDialog::configurationSummary(const QJsonObject& config) const
{
    const QStringList names = config.value("operations").toObject().keys();
    return QString("生成 %1 张；%2")
        .arg(config.value("copies_per_image").toInt())
        .arg(names.join(", "));
}

void DataAugmentationDialog::validateAndAccept()
{
    if (schemes_.isEmpty()) {
        QMessageBox::information(this, "尚未配置方案", "请至少新增一个增强方案。");
        return;
    }
    if (imagesFolderEdit_->text().trimmed().isEmpty() ||
        labelsFolderEdit_->text().trimmed().isEmpty()) {
        QMessageBox::information(this, "请选择输出目录", "图片和标签输出目录都不能为空。");
        return;
    }
    if (QMessageBox::question(
            this,
            "确认开始增强",
            QString("已配置 %1 个增强方案，将应用到全部 %2 张原图。\n\n"
                    "确认当前标注已经检查完成并开始生成？")
                .arg(schemes_.size())
                .arg(imageCount_),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) == QMessageBox::Yes) {
        accept();
    }
}

void DataAugmentationDialog::chooseImagesFolder()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, "选择增强图片目录", imagesFolderEdit_->text());
    if (!path.isEmpty()) {
        imagesFolderEdit_->setText(path);
    }
}

void DataAugmentationDialog::chooseLabelsFolder()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, "选择增强标签目录", labelsFolderEdit_->text());
    if (!path.isEmpty()) {
        labelsFolderEdit_->setText(path);
    }
}

QJsonObject DataAugmentationDialog::configuration() const
{
    QString imageFormat = "keep";
    if (imageFormatCombo_->currentIndex() == 1) {
        imageFormat = "jpg";
    } else if (imageFormatCombo_->currentIndex() == 2) {
        imageFormat = "png";
    }
    return {{"seed", seedSpin_->value()}, {"image_format", imageFormat}};
}

QJsonArray DataAugmentationDialog::schemes() const
{
    return schemes_;
}

QString DataAugmentationDialog::imagesOutputFolder() const
{
    return imagesFolderEdit_->text().trimmed();
}

QString DataAugmentationDialog::labelsOutputFolder() const
{
    return labelsFolderEdit_->text().trimmed();
}
