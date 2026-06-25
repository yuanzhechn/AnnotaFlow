#pragma once

#include "Annotation.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QVector>
#include <QWidget>

class AnnotationCanvas : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        Navigate,
        DrawBox,
        AiPoint
    };

    explicit AnnotationCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setAnnotations(const QVector<Annotation>& annotations);
    void setLabelColors(const QHash<QString, QColor>& labelColors);
    void setSelectedIndex(int index);
    void setMode(Mode mode);
    void setPromptPoint(const QPointF& point);
    void setPromptPoints(const QVector<QPointF>& points, const QVector<int>& labels);
    void clearPromptPoint();
    void setProposalRect(const QRectF& rect);
    void setProposalContours(const QVector<QVector<QPointF>>& contours);
    void clearProposalRect();

    Mode mode() const;
    int selectedIndex() const;
    QSize imageSize() const;
    double scale() const;
    bool hasProposalRect() const;

public slots:
    void fitToWindow();
    void zoomIn();
    void zoomOut();
    void cancelInteraction();

signals:
    void rectangleCreated(const QRectF& rect);
    void pointPromptCreated(const QPointF& point, int pointLabel);
    void selectionChanged(int index);
    void annotationContextMenuRequested(int index, const QPoint& globalPosition);
    void cursorImagePositionChanged(const QPointF& imagePosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QPointF imageToWidget(const QPointF& point) const;
    QPointF widgetToImage(const QPointF& point) const;
    QRectF imageRectToWidget(const QRectF& rect) const;
    QRectF clampToImage(const QRectF& rect) const;
    bool isPointInsideImage(const QPointF& imagePoint) const;
    int hitTest(const QPointF& imagePoint) const;
    QColor colorForLabel(const QString& label) const;
    void zoomAt(const QPointF& widgetPoint, double factor);
    void setSelectedIndexInternal(int index, bool emitSignal);
    void updateCursorForMode();

    QImage image_;
    QVector<Annotation> annotations_;
    QHash<QString, QColor> labelColors_;
    Mode mode_ = Mode::Navigate;
    int selectedIndex_ = -1;

    double scale_ = 1.0;
    QPointF origin_;
    bool fitMode_ = true;

    bool drawing_ = false;
    bool panning_ = false;
    bool hasProposalRect_ = false;
    bool hasCursorImagePoint_ = false;
    QPointF drawStartImage_;
    QPointF drawCurrentImage_;
    QPointF cursorImagePoint_;
    QRectF proposalRect_;
    QVector<QPointF> promptPoints_;
    QVector<int> promptPointLabels_;
    QVector<QVector<QPointF>> proposalContours_;
    QPointF lastMouseWidget_;
};
