#pragma once

#include "core/Types.h"
#include <QCache>
#include <QPointer>
#include <QWidget>

class QLabel;
class QNetworkReply;
class QSlider;
class QStackedWidget;

namespace bt {
class HttpClient;
class ImageView;
class MpvWidget;

class MediaViewer : public QWidget {
    Q_OBJECT
public:
    explicit MediaViewer(QWidget *parent = nullptr);
    ~MediaViewer() override;
    void load(const Post &post, HttpClient *client);
    void clear(const QString &message = {});
    void fit();
    void actualSize();
    void prefetch(const Post &post, HttpClient *client);
signals:
    void mediaError(const QString &message);
private:
    void decode(const QByteArray &bytes, const QString &key, quint64 generation, bool prefetch);
    QStackedWidget *m_stack;
    ImageView *m_image;
    QWidget *m_videoPage;
    MpvWidget *m_video;
    QLabel *m_message;
    QLabel *m_time;
    QSlider *m_seek;
    double m_duration = 0;
    quint64 m_generation = 0;
    QPointer<QNetworkReply> m_download;
    QCache<QString, QImage> m_images{96 * 1024};
};

}
