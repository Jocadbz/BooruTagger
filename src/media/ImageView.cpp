#include "ImageView.h"

#include <QGraphicsPixmapItem>
#include <QResizeEvent>
#include <QShortcut>
#include <QWheelEvent>
#include <cmath>

namespace bt {

ImageView::ImageView(QWidget *parent) : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor(28, 29, 32));
    setFrameShape(QFrame::NoFrame);
    auto *fitKey = new QShortcut(QKeySequence("F"), this);
    fitKey->setContext(Qt::WidgetShortcut);
    connect(fitKey, &QShortcut::activated, this, &ImageView::fit);
}

void ImageView::setImage(const QImage &image)
{
    m_scene->clear();
    m_scene->addPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(QRectF(QPointF(0, 0), image.size()));
    fit();
}

void ImageView::clearImage()
{
    m_scene->clear();
    resetTransform();
}

void ImageView::fit()
{
    m_fit = true;
    resetTransform();
    if (!m_scene->sceneRect().isEmpty())
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void ImageView::actualSize()
{
    m_fit = false;
    resetTransform();
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    if (m_scene->items().isEmpty())
        return;
    const auto delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
    if (delta == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    const double factor = std::pow(1.15, delta / 120.0);
    const double next = transform().m11() * factor;
    if (next > 0.005 && next < 64) {
        m_fit = false;
        scale(factor, factor);
    }
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fit)
        fit();
}

} // namespace bt
