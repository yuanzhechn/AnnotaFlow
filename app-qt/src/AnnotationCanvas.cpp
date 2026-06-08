#include "AnnotationCanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>

namespace {

constexpr double kMinimumScale = 0.02;
constexpr double kMaximumScale = 64.0;
constexpr double kMinimumBoxSize = 3.0;

QColor labelBackgroundColor()
{
    return QColor(25, 31, 38, 210);
}

} // namespace

AnnotationCanvas::AnnotationCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
}

void AnnotationCanvas::setImage(const QImage& image)
{
    image_ = image;
    selectedIndex_ = -1;
    drawing_ = false;
    panning_ = false;
    hasProposalRect_ = false;
    promptPoints_.clear();
    promptPointLabels_.clear();
    proposalContours_.clear();
    fitToWindow();
}

void AnnotationCanvas::setAnnotations(const QVector<Annotation>& annotations)
{
    annotations_ = annotations;
    if (selectedIndex_ >= annotations_.size()) {
        setSelectedIndexInternal(-1, true);
    }
    update();
}

void AnnotationCanvas::setLabelColors(const QHash<QString, QColor>& labelColors)
{
    labelColors_ = labelColors;
    update();
}

void AnnotationCanvas::setSelectedIndex(int index)
{
    setSelectedIndexInternal(index, false);
}

void AnnotationCanvas::setMode(Mode mode)
{
    mode_ = mode;
    drawing_ = false;
    updateCursorForMode();
    update();
}

void AnnotationCanvas::setPromptPoint(const QPointF& point)
{
    promptPoints_ = {point};
    promptPointLabels_ = {1};
    update();
}

void AnnotationCanvas::setPromptPoints(const QVector<QPointF>& points, const QVector<int>& labels)
{
    promptPoints_ = points;
    promptPointLabels_ = labels;
    update();
}

void AnnotationCanvas::clearPromptPoint()
{
    if (promptPoints_.isEmpty()) {
        return;
    }
    promptPoints_.clear();
    promptPointLabels_.clear();
    update();
}

void AnnotationCanvas::setProposalRect(const QRectF& rect)
{
    proposalRect_ = clampToImage(rect.normalized());
    hasProposalRect_ = !proposalRect_.isEmpty();
    update();
}

void AnnotationCanvas::setProposalContours(const QVector<QVector<QPointF>>& contours)
{
    proposalContours_ = contours;
    update();
}

void AnnotationCanvas::clearProposalRect()
{
    if (!hasProposalRect_ && proposalContours_.isEmpty()) {
        return;
    }
    hasProposalRect_ = false;
    proposalContours_.clear();
    update();
}

AnnotationCanvas::Mode AnnotationCanvas::mode() const
{
    return mode_;
}

int AnnotationCanvas::selectedIndex() const
{
    return selectedIndex_;
}

QSize AnnotationCanvas::imageSize() const
{
    return image_.size();
}

double AnnotationCanvas::scale() const
{
    return scale_;
}

bool AnnotationCanvas::hasProposalRect() const
{
    return hasProposalRect_;
}

void AnnotationCanvas::fitToWindow()
{
    if (image_.isNull() || width() <= 0 || height() <= 0) {
        scale_ = 1.0;
        origin_ = QPointF(0.0, 0.0);
        update();
        return;
    }

    const double sx = static_cast<double>(width() - 24) / image_.width();
    const double sy = static_cast<double>(height() - 24) / image_.height();
    scale_ = std::clamp(std::min(sx, sy), kMinimumScale, kMaximumScale);
    origin_ = QPointF((width() - image_.width() * scale_) / 2.0,
                      (height() - image_.height() * scale_) / 2.0);
    fitMode_ = true;
    update();
}

void AnnotationCanvas::zoomIn()
{
    zoomAt(rect().center(), 1.2);
}

void AnnotationCanvas::zoomOut()
{
    zoomAt(rect().center(), 1.0 / 1.2);
}

void AnnotationCanvas::cancelInteraction()
{
    if (drawing_) {
        drawing_ = false;
        update();
    }
    setMode(Mode::Navigate);
}

void AnnotationCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(17, 20, 24));

    if (image_.isNull()) {
        painter.setPen(QColor(150, 158, 166));
        painter.drawText(rect(), Qt::AlignCenter, "打开图片文件夹开始标注");
        return;
    }

    const QRectF target(origin_, QSizeF(image_.width() * scale_, image_.height() * scale_));
    painter.fillRect(target.adjusted(-1, -1, 1, 1), QColor(10, 12, 15));
    painter.drawImage(target, image_);

    QFont labelFont = font();
    labelFont.setPointSize(10);
    painter.setFont(labelFont);

    for (int i = 0; i < annotations_.size(); ++i) {
        const Annotation& annotation = annotations_[i];
        const QRectF widgetRect = imageRectToWidget(annotation.rect);
        const bool selected = i == selectedIndex_;
        const QColor baseColor = colorForLabel(annotation.label);

        QPen pen(selected ? QColor(255, 214, 92) : baseColor);
        pen.setWidth(selected ? 3 : 2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(widgetRect);

        const QString label = annotation.label.trimmed();
        if (!label.isEmpty()) {
            const QRect textBounds = painter.fontMetrics().boundingRect(label).adjusted(-6, -3, 6, 3);
            QRectF labelRect(widgetRect.topLeft() + QPointF(0, -textBounds.height() - 2),
                             QSizeF(textBounds.width(), textBounds.height()));
            if (labelRect.top() < 0) {
                labelRect.moveTop(widgetRect.top() + 2);
            }

            painter.setPen(Qt::NoPen);
            QColor labelFill = baseColor;
            labelFill.setAlpha(selected ? 245 : 220);
            painter.setBrush(labelFill.isValid() ? labelFill : labelBackgroundColor());
            painter.drawRoundedRect(labelRect, 3, 3);
            const int luminance = (baseColor.red() * 299 + baseColor.green() * 587 + baseColor.blue() * 114) / 1000;
            painter.setPen(luminance > 150 ? QColor(20, 24, 28) : QColor(246, 248, 250));
            painter.drawText(labelRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
        }
    }

    if (drawing_) {
        const QRectF preview = imageRectToWidget(QRectF(drawStartImage_, drawCurrentImage_).normalized());
        QPen pen(QColor(255, 98, 98));
        pen.setWidth(2);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 98, 98, 35));
        painter.drawRect(preview);
    }

    if (hasProposalRect_) {
        const QRectF proposal = imageRectToWidget(proposalRect_);
        QPen pen(QColor(0, 220, 80));
        pen.setWidth(3);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(0, 220, 80, 24));
        painter.drawRect(proposal);
    }

    if (!proposalContours_.isEmpty()) {
        QPen pen(QColor(0, 255, 70));
        pen.setWidth(2);
        pen.setStyle(Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const QVector<QPointF>& contour : proposalContours_) {
            if (contour.size() < 2) {
                continue;
            }
            QPolygonF polygon;
            polygon.reserve(contour.size());
            for (const QPointF& point : contour) {
                polygon.append(imageToWidget(point));
            }
            painter.drawPolygon(polygon);
        }
    }

    for (int i = 0; i < promptPoints_.size(); ++i) {
        const QPointF center = imageToWidget(promptPoints_[i]);
        const bool foreground = i >= promptPointLabels_.size() || promptPointLabels_[i] != 0;
        const QColor color = foreground ? QColor(70, 160, 255) : QColor(255, 82, 82);
        QPen pen(color);
        pen.setWidth(2);
        painter.setPen(pen);
        QColor fill = color;
        fill.setAlpha(100);
        painter.setBrush(fill);
        painter.drawEllipse(center, 6, 6);
        if (!foreground) {
            painter.drawLine(center + QPointF(-4, -4), center + QPointF(4, 4));
            painter.drawLine(center + QPointF(-4, 4), center + QPointF(4, -4));
        }
    }
}

void AnnotationCanvas::resizeEvent(QResizeEvent*)
{
    if (fitMode_) {
        fitToWindow();
    }
}

void AnnotationCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    lastMouseWidget_ = event->pos();

    if (image_.isNull()) {
        return;
    }

    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier))) {
        panning_ = true;
        fitMode_ = false;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    const QPointF imagePoint = widgetToImage(event->pos());
    if ((event->button() == Qt::LeftButton || event->button() == Qt::RightButton) &&
        mode_ == Mode::AiPoint &&
        isPointInsideImage(imagePoint)) {
        emit pointPromptCreated(imagePoint, event->button() == Qt::RightButton ? 0 : 1);
        return;
    }

    if (event->button() == Qt::LeftButton && mode_ == Mode::DrawBox && isPointInsideImage(imagePoint)) {
        drawing_ = true;
        drawStartImage_ = imagePoint;
        drawCurrentImage_ = imagePoint;
        update();
        return;
    }

    if (event->button() == Qt::LeftButton && mode_ == Mode::Navigate) {
        setSelectedIndexInternal(hitTest(imagePoint), true);
    }
}

void AnnotationCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (image_.isNull()) {
        return;
    }

    const QPointF imagePoint = widgetToImage(event->pos());
    emit cursorImagePositionChanged(imagePoint);

    if (panning_) {
        const QPointF delta = event->pos() - lastMouseWidget_;
        origin_ += delta;
        lastMouseWidget_ = event->pos();
        update();
        return;
    }

    if (drawing_) {
        drawCurrentImage_ = imagePoint;
        update();
    }
}

void AnnotationCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && panning_)) {
        panning_ = false;
        updateCursorForMode();
        return;
    }

    if (event->button() == Qt::LeftButton && drawing_) {
        drawing_ = false;
        drawCurrentImage_ = widgetToImage(event->pos());
        const QRectF rect = clampToImage(QRectF(drawStartImage_, drawCurrentImage_).normalized());
        if (rect.width() >= kMinimumBoxSize && rect.height() >= kMinimumBoxSize) {
            emit rectangleCreated(rect);
        }
        update();
    }
}

void AnnotationCanvas::wheelEvent(QWheelEvent* event)
{
    if (image_.isNull()) {
        return;
    }

    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    zoomAt(event->position(), factor);
    event->accept();
}

QPointF AnnotationCanvas::imageToWidget(const QPointF& point) const
{
    return origin_ + point * scale_;
}

QPointF AnnotationCanvas::widgetToImage(const QPointF& point) const
{
    return (point - origin_) / scale_;
}

QRectF AnnotationCanvas::imageRectToWidget(const QRectF& rect) const
{
    const QRectF normalized = normalizedAnnotationRect(rect);
    return QRectF(imageToWidget(normalized.topLeft()), QSizeF(normalized.width() * scale_, normalized.height() * scale_));
}

QRectF AnnotationCanvas::clampToImage(const QRectF& rect) const
{
    if (image_.isNull()) {
        return QRectF();
    }
    return normalizedAnnotationRect(rect).intersected(QRectF(0, 0, image_.width(), image_.height()));
}

bool AnnotationCanvas::isPointInsideImage(const QPointF& imagePoint) const
{
    return QRectF(0, 0, image_.width(), image_.height()).contains(imagePoint);
}

int AnnotationCanvas::hitTest(const QPointF& imagePoint) const
{
    for (int i = annotations_.size() - 1; i >= 0; --i) {
        if (normalizedAnnotationRect(annotations_[i].rect).contains(imagePoint)) {
            return i;
        }
    }
    return -1;
}

QColor AnnotationCanvas::colorForLabel(const QString& label) const
{
    const QColor color = labelColors_.value(label.trimmed());
    return color.isValid() ? color : QColor(64, 201, 255);
}

void AnnotationCanvas::zoomAt(const QPointF& widgetPoint, double factor)
{
    if (image_.isNull()) {
        return;
    }

    const double newScale = std::clamp(scale_ * factor, kMinimumScale, kMaximumScale);
    if (qFuzzyCompare(newScale, scale_)) {
        return;
    }

    const QPointF imagePoint = widgetToImage(widgetPoint);
    scale_ = newScale;
    origin_ = widgetPoint - imagePoint * scale_;
    fitMode_ = false;
    update();
}

void AnnotationCanvas::setSelectedIndexInternal(int index, bool emitSignal)
{
    const int normalizedIndex = (index >= 0 && index < annotations_.size()) ? index : -1;
    if (selectedIndex_ == normalizedIndex) {
        return;
    }

    selectedIndex_ = normalizedIndex;
    if (emitSignal) {
        emit selectionChanged(selectedIndex_);
    }
    update();
}

void AnnotationCanvas::updateCursorForMode()
{
    if (mode_ == Mode::DrawBox) {
        setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}
