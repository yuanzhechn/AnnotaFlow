#pragma once

#include <QImage>
#include <QString>

class ImageLoader {
public:
    static bool loadImage(const QString& filePath, QImage* image, QString* errorMessage = nullptr);
};
