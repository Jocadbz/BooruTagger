#pragma once

#include <QOpenGLWidget>
#include <QUrl>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

namespace bt {

class MpvWidget : public QOpenGLWidget {
    Q_OBJECT
  public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;
    void load(const QUrl &url);
    void stop();
    void togglePause();
    void seek(double seconds, bool absolute = false);
    void frameStep(bool backwards = false);
    void setSpeed(double speed);
    void setLoop(bool loop);
    void setMuted(bool muted);
    void setVolume(int volume);
    bool available() const { return m_mpv != nullptr; }
  signals:
    void positionChanged(double position);
    void durationChanged(double duration);
    void pauseChanged(bool paused);
    void playbackError(const QString &message);
    void frameRendered();

  protected:
    void initializeGL() override;
    void paintGL() override;

  private:
    void processEvents();
    void cleanupGL();
    void command(const QList<QByteArray> &args);
    void loadPending();
    static void wakeup(void *context);
    static void redraw(void *context);
    static void *resolve(void *context, const char *name);
    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_render = nullptr;
    QUrl m_pendingUrl;
    bool m_stopping = false;
};

} // namespace bt
