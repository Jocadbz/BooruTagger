#include "MpvWidget.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QShortcut>
#include <QUrl>
#include <clocale>

namespace bt {

MpvWidget::MpvWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    std::setlocale(LC_NUMERIC, "C");
    m_mpv = mpv_create();
    if (!m_mpv)
        return;
    for (const auto &option :
         {qMakePair("vo", "libmpv"), qMakePair("config", "no"), qMakePair("terminal", "no"),
          qMakePair("msg-level", "all=no"), qMakePair("hwdec", "auto-safe"), qMakePair("ytdl", "no"),
          qMakePair("load-unsafe-playlists", "no"), qMakePair("access-references", "no"),
          qMakePair("input-default-bindings", "no"), qMakePair("input-vo-keyboard", "no"),
          qMakePair("demuxer-max-bytes", "32MiB"), qMakePair("demuxer-max-back-bytes", "8MiB"),
          qMakePair("cache-secs", "20"), qMakePair("keep-open", "yes"), qMakePair("mute", "yes"),
          qMakePair("loop-file", "inf"), qMakePair("user-agent", "BooruTagger/" BOORUTAGGER_VERSION)})
        mpv_set_option_string(m_mpv, option.first, option.second);
    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    mpv_observe_property(m_mpv, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 3, "pause", MPV_FORMAT_FLAG);
    mpv_set_wakeup_callback(m_mpv, &MpvWidget::wakeup, this);
    const auto shortcut = [this](const QString &key, auto action) {
        auto *s = new QShortcut(QKeySequence(key), this);
        s->setContext(Qt::WidgetShortcut);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut("Space", [this] { togglePause(); });
    shortcut("Left", [this] { seek(-5); });
    shortcut("Right", [this] { seek(5); });
    shortcut(".", [this] { frameStep(); });
    shortcut(",", [this] { frameStep(true); });
}

MpvWidget::~MpvWidget()
{
    if (context())
        disconnect(context(), nullptr, this, nullptr);
    if (m_mpv)
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    cleanupGL();
    if (m_mpv)
        mpv_terminate_destroy(m_mpv);
}

void *MpvWidget::resolve(void *, const char *name)
{
    return reinterpret_cast<void *>(QOpenGLContext::currentContext()->getProcAddress(name));
}

void MpvWidget::wakeup(void *context)
{
    auto *self = static_cast<MpvWidget *>(context);
    QMetaObject::invokeMethod(self, &MpvWidget::processEvents, Qt::QueuedConnection);
}

void MpvWidget::redraw(void *context)
{
    auto *self = static_cast<MpvWidget *>(context);
    QMetaObject::invokeMethod(self, [self] { self->update(); }, Qt::QueuedConnection);
}

void MpvWidget::initializeGL()
{
    if (!m_mpv) {
        emit playbackError(tr("libmpv could not be initialized."));
        return;
    }
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MpvWidget::cleanupGL, Qt::DirectConnection);
    mpv_opengl_init_params init{&MpvWidget::resolve, nullptr};
    mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                                 {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    const int error = mpv_render_context_create(&m_render, m_mpv, params);
    if (error < 0) {
        m_render = nullptr;
        emit playbackError(
            tr("Could not create the mpv OpenGL renderer: %1").arg(QString::fromUtf8(mpv_error_string(error))));
        return;
    }
    mpv_render_context_set_update_callback(m_render, &MpvWidget::redraw, this);
    loadPending();
}

void MpvWidget::cleanupGL()
{
    if (!m_render)
        return;
    makeCurrent();
    mpv_render_context_set_update_callback(m_render, nullptr, nullptr);
    mpv_render_context_free(m_render);
    m_render = nullptr;
    doneCurrent();
}

void MpvWidget::paintGL()
{
    if (!m_render) {
        auto *gl = context()->functions();
        gl->glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    mpv_render_context_update(m_render);
    const auto ratio = devicePixelRatioF();
    mpv_opengl_fbo fbo{static_cast<int>(defaultFramebufferObject()), qRound(width() * ratio), qRound(height() * ratio),
                       0};
    int flip = 1;
    int block = 0;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                 {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(m_render, params);
    emit frameRendered();
}

void MpvWidget::command(const QList<QByteArray> &args)
{
    if (!m_mpv)
        return;
    QList<const char *> pointers;
    for (const auto &arg : args)
        pointers.append(arg.constData());
    pointers.append(nullptr);
    if (mpv_command_async(m_mpv, 0, pointers.data()) < 0)
        emit playbackError(tr("The player could not accept this command."));
}

void MpvWidget::load(const QUrl &url)
{
    stop();
    m_stopping = false;
    m_pendingUrl = url;
    if (m_render)
        loadPending();
    else if (!m_mpv)
        emit playbackError(tr("libmpv is unavailable."));
}

void MpvWidget::loadPending()
{
    if (!m_pendingUrl.isEmpty()) {
        command({"loadfile",
                 m_pendingUrl.isLocalFile() ? m_pendingUrl.toLocalFile().toUtf8() : m_pendingUrl.toEncoded(),
                 "replace"});
        m_pendingUrl.clear();
        command({"set", "pause", "no"});
    }
}

void MpvWidget::stop()
{
    m_stopping = true;
    m_pendingUrl.clear();
    command({"stop"});
    emit positionChanged(0);
    emit durationChanged(0);
}

void MpvWidget::togglePause()
{
    command({"cycle", "pause"});
}
void MpvWidget::seek(double seconds, bool absolute)
{
    command({"seek", QByteArray::number(seconds), absolute ? "absolute+exact" : "relative+exact"});
}
void MpvWidget::frameStep(bool backwards)
{
    command({backwards ? "frame-back-step" : "frame-step"});
}
void MpvWidget::setSpeed(double speed)
{
    command({"set", "speed", QByteArray::number(speed)});
}
void MpvWidget::setLoop(bool loop)
{
    command({"set", "loop-file", loop ? "inf" : "no"});
}
void MpvWidget::setMuted(bool muted)
{
    command({"set", "mute", muted ? "yes" : "no"});
}
void MpvWidget::setVolume(int volume)
{
    command({"set", "volume", QByteArray::number(volume)});
}

void MpvWidget::processEvents()
{
    if (!m_mpv)
        return;
    for (;;) {
        const auto *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            const auto *property = static_cast<mpv_event_property *>(event->data);
            if (!property->data)
                continue;
            if (event->reply_userdata == 1)
                emit positionChanged(*static_cast<double *>(property->data));
            else if (event->reply_userdata == 2)
                emit durationChanged(*static_cast<double *>(property->data));
            else if (event->reply_userdata == 3)
                emit pauseChanged(*static_cast<int *>(property->data) != 0);
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            if (!m_stopping && end->reason == MPV_END_FILE_REASON_ERROR)
                emit playbackError(
                    tr("Video could not be played: %1").arg(QString::fromUtf8(mpv_error_string(end->error))));
        }
    }
}

} // namespace bt
