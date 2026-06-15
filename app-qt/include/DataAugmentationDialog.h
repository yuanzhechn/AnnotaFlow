#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListWidget;
class QSpinBox;

class DataAugmentationDialog : public QDialog {
    Q_OBJECT

public:
    DataAugmentationDialog(
        const QString& datasetRoot,
        const QStringList& imagePaths,
        QWidget* parent = nullptr);

    QJsonObject configuration() const;
    QJsonArray schemes() const;
    QString imagesOutputFolder() const;
    QString labelsOutputFolder() const;

private:
    void chooseImagesFolder();
    void chooseLabelsFolder();
    void addScheme();
    void editCurrentScheme();
    void removeCurrentScheme();
    void validateAndAccept();
    void refreshImageList();
    QString configurationSummary(const QJsonObject& config) const;

    int imageCount_ = 0;
    QJsonArray schemes_;
    QLineEdit* imagesFolderEdit_ = nullptr;
    QLineEdit* labelsFolderEdit_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;
    QComboBox* imageFormatCombo_ = nullptr;
    QListWidget* imagesList_ = nullptr;
};
