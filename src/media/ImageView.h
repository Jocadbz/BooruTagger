#pragma once

#include <QGraphicsView>

namespace bt {

class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void clearImage();
    void fit();
    void actualSize();
protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    QGraphicsScene *m_scene;
    bool m_fit = true;
};

}
