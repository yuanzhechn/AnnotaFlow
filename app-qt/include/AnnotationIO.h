#pragma once

#include "Annotation.h"

#include <QSize>
#include <QString>
#include <QVector>

namespace AnnotationIO {

enum class SaveFormat {
    VocXml,
    Yolo
};

QString formatDisplayName(SaveFormat format);
bool save(SaveFormat format,
          const QString& imagePath,
          const QString& outputRoot,
          const QSize& imageSize,
          const QVector<Annotation>& annotations,
          QString* errorMessage = nullptr);

bool load(SaveFormat format,
          const QString& imagePath,
          const QString& outputRoot,
          const QSize& imageSize,
          QVector<Annotation>* annotations,
          QString* errorMessage = nullptr);

} // namespace AnnotationIO
