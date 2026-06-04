#pragma once

#include <QRectF>
#include <QString>

struct Annotation {
    QRectF rect;
    QString label;
};

inline QRectF normalizedAnnotationRect(const QRectF& rect)
{
    return rect.normalized();
}
