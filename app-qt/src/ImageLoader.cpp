#include "ImageLoader.h"

#include <QFile>
#include <QImageReader>

#ifdef ANNOTAFLOW_HAS_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

bool ImageLoader::loadImage(const QString& filePath, QImage* image, QString* errorMessage)
{
    if (!image) {
        if (errorMessage) {
            *errorMessage = "内部错误：图片目标为空。";
        }
        return false;
    }

#ifdef ANNOTAFLOW_HAS_OPENCV
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = "无法打开图片文件。";
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    std::vector<uchar> buffer(bytes.begin(), bytes.end());
    cv::Mat mat = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);

    if (mat.empty()) {
        if (errorMessage) {
            *errorMessage = "OpenCV 无法解码这张图片。";
        }
        return false;
    }

    cv::Mat converted;
    if (mat.channels() == 1) {
        *image = QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8).copy();
        return true;
    }

    if (mat.channels() == 3) {
        cv::cvtColor(mat, converted, cv::COLOR_BGR2RGB);
        *image = QImage(converted.data, converted.cols, converted.rows, static_cast<int>(converted.step), QImage::Format_RGB888).copy();
        return true;
    }

    if (mat.channels() == 4) {
        cv::cvtColor(mat, converted, cv::COLOR_BGRA2RGBA);
        *image = QImage(converted.data, converted.cols, converted.rows, static_cast<int>(converted.step), QImage::Format_RGBA8888).copy();
        return true;
    }

    if (errorMessage) {
        *errorMessage = "不支持的图片通道格式。";
    }
    return false;
#else
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage loaded = reader.read();
    if (loaded.isNull()) {
        if (errorMessage) {
            *errorMessage = reader.errorString();
        }
        return false;
    }

    *image = loaded.convertToFormat(QImage::Format_RGB888);
    return true;
#endif
}
