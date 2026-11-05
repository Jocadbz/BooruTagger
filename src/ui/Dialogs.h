#pragma once

#include "sessions/Session.h"
#include <QDialog>
#include <QPointer>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace bt {
class BooruProvider;
class Credentials;
enum class VerificationDecision;

std::optional<Instance> instanceDialog(QWidget *parent);
std::optional<Session> sessionDialog(const Instance &instance, QWidget *parent);
bool reviewMerge(const TagMerge &merge, QWidget *parent);
VerificationDecision reviewVerification(const QSet<QString> &expected, const Post &actual, bool uncertain,
                                        QWidget *parent);

class LoginDialog : public QDialog {
  public:
    LoginDialog(BooruProvider *provider, Credentials *credentials, QWidget *parent);
    void reject() override;

  private:
    void signIn();
    QPointer<BooruProvider> m_provider;
    Credentials *m_credentials;
    QLineEdit *m_secret;
    QCheckBox *m_remember;
    QLabel *m_status;
    QPushButton *m_login;
    bool m_pending = false;
};

} // namespace bt
