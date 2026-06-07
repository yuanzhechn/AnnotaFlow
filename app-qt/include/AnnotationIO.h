#pragma once

#include "Annotation.h"

#include <QSize>
#include <QStringList>
#include <QString>
#include <QHash>
#include <QVector>

namespace AnnotationIO {

enum class SaveFormat {
    VocXml,
    Yolo,
    CocoJson,
    LabelMeJson,
    KittiTxt,
    Csv
};

QVector<SaveFormat> supportedFormats();
QString formatDisplayName(SaveFormat format);
QString formatDirectoryName(SaveFormat format);
QString formatDescription(SaveFormat format);
bool isDatasetLevelFormat(SaveFormat format);

bool save(SaveFormat format,
          const QString& imagePath,
          const QString& outputRoot,
          const QSize& imageSize,
          const QVector<Annotation>& annotations,
          QString* errorMessage = nullptr);

bool saveImage(SaveFormat format,
               const QString& imagePath,
               const QString& outputRoot,
               const QSize& imageSize,
               const QVector<Annotation>& annotations,
               const QStringList& classNames,
               QString* errorMessage = nullptr);

bool saveDataset(SaveFormat format,
                 const QStringList& imagePaths,
                 const QString& outputRoot,
                 const QHash<QString, QVector<Annotation>>& annotationsByImage,
                 const QStringList& classNames,
                 QString* errorMessage = nullptr);

bool convertDataset(SaveFormat sourceFormat,
                    SaveFormat targetFormat,
                    const QStringList& imagePaths,
                    const QString& sourceRoot,
                    const QString& targetRoot,
                    int* annotationCount = nullptr,
                    QStringList* classNames = nullptr,
                    QString* errorMessage = nullptr);

bool load(SaveFormat format,
          const QString& imagePath,
          const QString& outputRoot,
          const QSize& imageSize,
          QVector<Annotation>* annotations,
          QString* errorMessage = nullptr);

bool exists(SaveFormat format,
            const QString& imagePath,
            const QString& outputRoot);

QStringList readClassNames(SaveFormat format,
                           const QString& outputRoot,
                           QString* errorMessage = nullptr);

} // namespace AnnotationIO
