#pragma once

#include "Annotation.h"

#include <QImage>
#include <QPointF>
#include <QVector>
#include <QWidget>

class AnnotationCanvas : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        Navigate,
        DrawBox
    };

    explicit AnnotationCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setAnnotations(const QVector<Annotation>& annotations);
    void setSelectedIndex(int index);
    void setMode(Mode mode);

    Mode mode() const;
    int selectedIndex() const;
    QSize imageSize() const;
    double scale() const;

public slots:
    void fitToWindow();
    void zoomIn();
    void zoomOut();
    void cancelInteraction();

signals:
    void rectangleCreated(const QRectF& rect);
    void selectionChanged(int index);
    void cursorImagePositionChanged(const QPointF& imagePosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QPointF imageToWidget(const QPointF& point) const;
    QPointF widgetToImage(const QPointF& point) const;
    QRectF imageRectToWidget(const QRectF& rect) const;
    QRectF clampToImage(const QRectF& rect) const;
    bool isPointInsideImage(const QPointF& imagePoint) const;
    int hitTest(const QPointF& imagePoint) const;
    void zoomAt(const QPointF& widgetPoint, double factor);
    void setSelectedIndexInternal(int index, bool emitSignal);

    QImage image_;
    QVector<Annotation> annotations_;
    Mode mode_ = Mode::Navigate;
    int selectedIndex_ = -1;

    double scale_ = 1.0;
    QPointF origin_;
    bool fitMode_ = true;

    bool drawing_ = false;
    bool panning_ = false;
    QPointF drawStartImage_;
    QPointF drawCurrentImage_;
    QPointF lastMouseWidget_;
};
