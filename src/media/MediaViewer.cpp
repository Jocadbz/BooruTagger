#include "MediaViewer.h"
#include "ImageView.h"
#include "MpvWidget.h"
#include "network/HttpClient.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <utility>

namespace bt {
namespace {
bool isVideo(const QString &extension)
{
    const auto ext = extension.toLower().toLatin1();
    if (ext == "gif" || ext == "apng")
        return true;
    return !ext.isEmpty() && !QImageReader::supportedImageFormats().contains(ext);
}
QString clockText(double value)
{
    const auto seconds = qMax(0, static_cast<int>(value));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

MediaViewer::MediaViewer(QWidget *parent) : QWidget(parent)
{
    QImageReader::setAllocationLimit(256);
    m_decodePool.setMaxThreadCount(2);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);
    m_message = new QLabel(tr("Start or resume a session to load a post."), m_stack);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_stack->addWidget(m_message);
    m_image = new ImageView(m_stack);
    m_stack->addWidget(m_image);
    m_videoPage = new QWidget(m_stack);
    auto *videoLayout = new QVBoxLayout(m_videoPage);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    m_video = new MpvWidget(m_videoPage);
    videoLayout->addWidget(m_video, 1);
    auto *timeline = new QHBoxLayout;
    m_seek = new QSlider(Qt::Horizontal, m_videoPage);
    m_seek->setRange(0, 10000);
    m_seek->setSingleStep(100);
    m_seek->setPageStep(1000);
    m_seek->setObjectName("videoTimeline");
    m_time = new QLabel("0:00 / 0:00", m_videoPage);
    timeline->addWidget(m_seek, 1);
    timeline->addWidget(m_time);
    videoLayout->addLayout(timeline);
    auto *controls = new QHBoxLayout;
    auto *pause = new QPushButton(tr("Pause"), m_videoPage);
    auto *step = new QPushButton(tr("Frame +"), m_videoPage);
    auto *back = new QPushButton(tr("Frame −"), m_videoPage);
    auto *speed = new QComboBox(m_videoPage);
    for (const auto value : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
        speed->addItem(QString::number(value) + "×", value);
    speed->setCurrentIndex(3);
    auto *loop = new QCheckBox(tr("Loop"), m_videoPage);
    loop->setChecked(true);
    auto *mute = new QCheckBox(tr("Mute"), m_videoPage);
    mute->setChecked(true);
    auto *volume = new QSlider(Qt::Horizontal, m_videoPage);
    volume->setRange(0, 100);
    volume->setValue(100);
    volume->setMaximumWidth(90);
    volume->setToolTip(tr("Volume"));
    controls->addWidget(pause);
    controls->addWidget(back);
    controls->addWidget(step);
    controls->addWidget(speed);
    controls->addStretch();
    controls->addWidget(loop);
    controls->addWidget(mute);
    controls->addWidget(volume);
    videoLayout->addLayout(controls);
    m_stack->addWidget(m_videoPage);
    connect(pause, &QPushButton::clicked, m_video, &MpvWidget::togglePause);
    connect(back, &QPushButton::clicked, this, [this] { m_video->frameStep(true); });
    connect(step, &QPushButton::clicked, this, [this] { m_video->frameStep(); });
    connect(speed, &QComboBox::currentIndexChanged, this,
            [this, speed] { m_video->setSpeed(speed->currentData().toDouble()); });
    connect(loop, &QCheckBox::toggled, m_video, &MpvWidget::setLoop);
    connect(mute, &QCheckBox::toggled, m_video, &MpvWidget::setMuted);
    connect(volume, &QSlider::valueChanged, m_video, &MpvWidget::setVolume);
    connect(m_seek, &QSlider::sliderReleased, this,
            [this] { m_video->seek(m_duration * m_seek->value() / 10000.0, true); });
    connect(m_seek, &QSlider::actionTriggered, this, [this] {
        if (!m_seek->isSliderDown())
            m_video->seek(m_duration * m_seek->sliderPosition() / 10000.0, true);
    });
    connect(m_video, &MpvWidget::positionChanged, this, [this](double position) {
        if (!m_seek->isSliderDown())
            m_seek->setValue(m_duration > 0 ? qRound(position / m_duration * 10000) : 0);
        m_time->setText(clockText(position) + " / " + clockText(m_duration));
    });
    connect(m_video, &MpvWidget::durationChanged, this, [this](double duration) { m_duration = duration; });
    connect(m_video, &MpvWidget::pauseChanged, this,
            [pause](bool paused) { pause->setText(paused ? tr("Play") : tr("Pause")); });
    connect(m_video, &MpvWidget::playbackError, this, &MediaViewer::mediaError);
}

MediaViewer::~MediaViewer()
{
    ++m_generation;
    if (m_client)
        m_client->cancelMedia();
    if (m_download)
        m_download->abort();
}

void MediaViewer::clear(const QString &message)
{
    ++m_generation;
    m_pendingDecode.reset();
    if (m_client)
        m_client->cancelMedia();
    if (m_download)
        m_download->abort();
    m_download.clear();
    m_video->stop();
    m_image->clearImage();
    m_message->setText(message.isEmpty() ? tr("No media loaded.") : message);
    m_stack->setCurrentWidget(m_message);
}

void MediaViewer::load(const Post &post, HttpClient *client)
{
    clear(tr("Loading media…"));
    m_client = client;
    if (post.mediaUrl.isEmpty()) {
        m_message->setText(tr("No accessible media URL. You can still edit this post's tags."));
        return;
    }
    if (isVideo(post.extension)) {
        if (!post.mediaUrl.userInfo().isEmpty() ||
            (post.mediaUrl.scheme() != "https" &&
             !(client->instance().allowHttp && post.mediaUrl.scheme() == "http"))) {
            m_message->setText(tr("The media URL uses an unsafe protocol."));
            return;
        }
        m_stack->setCurrentWidget(m_videoPage);
        m_video->load(post.mediaUrl);
        return;
    }
    const QString key = client->instance().id + ':' + post.mediaUrl.toString();
    if (const auto *image = m_images.object(key)) {
        m_image->setImage(*image);
        m_stack->setCurrentWidget(m_image);
        return;
    }
    const auto generation = m_generation;
    QPointer<MediaViewer> guard(this);
    m_download = client->send(
        "GET", post.mediaUrl, {},
        [this, guard, generation, key](Result<HttpResponse> response) {
            if (!guard || generation != m_generation)
                return;
            m_download.clear();
            if (!response) {
                m_message->setText(response.error.message);
                emit mediaError(response.error.message);
                return;
            }
            decode(response.value->body, key, generation, false);
        },
        true);
}

void MediaViewer::decode(const QByteArray &bytes, const QString &key, quint64 generation, bool prefetch)
{
    if (m_decoding >= 2) {
        m_pendingDecode = DecodeRequest{bytes, key, generation, prefetch};
        return;
    }
    ++m_decoding;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key, generation, prefetch] {
        const auto image = watcher->result();
        watcher->deleteLater();
        --m_decoding;
        if (auto pending = std::exchange(m_pendingDecode, std::nullopt))
            decode(pending->bytes, pending->key, pending->generation, pending->prefetch);
        if (generation != m_generation)
            return;
        if (!image.isNull())
            m_images.insert(key, new QImage(image), qMax<qsizetype>(1, image.sizeInBytes() / 1024));
        if (prefetch)
            return;
        if (image.isNull()) {
            m_message->setText(tr("This image could not be decoded (or exceeds the image memory limit)."));
            emit mediaError(m_message->text());
        } else {
            m_image->setImage(image);
            m_stack->setCurrentWidget(m_image);
        }
    });
    watcher->setFuture(QtConcurrent::run(&m_decodePool, [bytes] {
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (size.width() > 8192 || size.height() > 8192)
            reader.setScaledSize(size.scaled(8192, 8192, Qt::KeepAspectRatio));
        return reader.read();
    }));
}

void MediaViewer::prefetch(const Post &post, HttpClient *client)
{
    if (isVideo(post.extension) || post.previewUrl.isEmpty())
        return;
    // Only small previews are prefetched; full videos are always streamed on demand.
    client->send("GET", post.previewUrl, {}, [](Result<HttpResponse>) {}, true);
}

void MediaViewer::fit()
{
    m_image->fit();
}
void MediaViewer::actualSize()
{
    m_image->actualSize();
}

} // namespace bt
