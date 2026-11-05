#include "Dialogs.h"
#include "providers/BooruProvider.h"
#include "sessions/SessionEngine.h"
#include "settings/Credentials.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

namespace bt {

std::optional<Instance> instanceDialog(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Add instance / account"));
    dialog.resize(510, 340);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *kind = new QComboBox(&dialog);
    kind->addItem("Danbooru", static_cast<int>(ProviderKind::Danbooru));
    kind->addItem("Shimmie2 2.12+", static_cast<int>(ProviderKind::Shimmie));
    auto *name = new QLineEdit("Danbooru", &dialog);
    auto *url = new QLineEdit("https://danbooru.donmai.us", &dialog);
    auto *user = new QLineEdit(&dialog);
    auto *http = new QCheckBox(QObject::tr("Allow insecure HTTP (trusted local servers only)"), &dialog);
    auto *test = new QPushButton(QObject::tr("Use Danbooru's test server"), &dialog);
    form->addRow(QObject::tr("Provider"), kind);
    form->addRow(QObject::tr("Profile name"), name);
    form->addRow(QObject::tr("Installation URL"), url);
    form->addRow(QObject::tr("Account name"), user);
    layout->addLayout(form);
    layout->addWidget(test);
    layout->addWidget(http);
    auto *note = new QLabel(QObject::tr("Use the installation base URL, not /graphql or /posts. Each account gets its "
                                        "own profile. Server and account are fixed once a session exists."),
                            &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, [kind, name, url, test] {
        const bool danbooru = kind->currentData().toInt() == static_cast<int>(ProviderKind::Danbooru);
        name->setText(danbooru ? "Danbooru" : "My Shimmie");
        url->setText(danbooru ? "https://danbooru.donmai.us" : "");
        url->setPlaceholderText("https://example.org/booru");
        test->setVisible(danbooru);
    });
    QObject::connect(test, &QPushButton::clicked, &dialog, [name, url] {
        name->setText("Testbooru");
        url->setText("https://testbooru.donmai.us");
    });
    QObject::connect(http, &QCheckBox::clicked, &dialog, [&dialog, http](bool checked) {
        if (checked &&
            QMessageBox::warning(&dialog, QObject::tr("Unencrypted connection"),
                                 QObject::tr("HTTP exposes your login, session cookies, and edits to the network. Only "
                                             "use it on a trusted local connection."),
                                 QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
            http->setChecked(false);
    });
    std::optional<Instance> result;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        Instance instance;
        instance.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        instance.name = name->text().trimmed();
        instance.kind = static_cast<ProviderKind>(kind->currentData().toInt());
        instance.baseUrl = QUrl(url->text().trimmed());
        instance.username = user->text().trimmed();
        instance.allowHttp = http->isChecked();
        const auto message = validateInstance(instance);
        if (!message.isEmpty()) {
            error->setText(message);
            return;
        }
        result = instance;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
    return result;
}

std::optional<Session> sessionDialog(const Instance &instance, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("New tagging session · %1").arg(instance.name));
    dialog.resize(520, 370);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(&dialog);
    name->setPlaceholderText(QObject::tr("Optional — generated from the source"));
    auto *source = new QComboBox(&dialog);
    source->addItem(QObject::tr("Tag search"), static_cast<int>(Source::Search));
    source->addItem(QObject::tr("Single post ID"), static_cast<int>(Source::Single));
    source->addItem(QObject::tr("Post ID range"), static_cast<int>(Source::Range));
    auto *query = new QLineEdit("tagme", &dialog);
    auto *first = new QLineEdit("1", &dialog);
    auto *last = new QLineEdit("100", &dialog);
    auto *limit = new QSpinBox(&dialog);
    limit->setRange(1, 100000);
    limit->setValue(120);
    auto *remove = new QCheckBox(QObject::tr("Remove tagme after successful tagging"), &dialog);
    remove->setChecked(true);
    auto *note =
        new QLabel(QObject::tr("IDs are stored before editing starts. Ranges follow the direction of the entered IDs. "
                               "Removing tagme also requires explicitly marking each post as sufficiently tagged."),
                   &dialog);
    note->setWordWrap(true);
    form->addRow(QObject::tr("Session name"), name);
    form->addRow(QObject::tr("Source"), source);
    form->addRow(QObject::tr("Search query"), query);
    form->addRow(QObject::tr("Post ID / first ID"), first);
    form->addRow(QObject::tr("Last ID"), last);
    form->addRow(QObject::tr("Search result limit"), limit);
    layout->addLayout(form);
    layout->addWidget(remove);
    layout->addWidget(note);
    auto *error = new QLabel(&dialog);
    layout->addWidget(error);
    const auto update = [=] {
        const auto selected = static_cast<Source>(source->currentData().toInt());
        query->setEnabled(selected == Source::Search);
        limit->setEnabled(selected == Source::Search);
        first->setEnabled(selected != Source::Search);
        last->setEnabled(selected == Source::Range);
        remove->setChecked(selected == Source::Search &&
                           query->text().split(' ', Qt::SkipEmptyParts).contains("tagme"));
    };
    QObject::connect(source, &QComboBox::currentIndexChanged, &dialog, update);
    QObject::connect(query, &QLineEdit::textChanged, &dialog, update);
    update();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Start session"));
    layout->addWidget(buttons);
    std::optional<Session> result;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        Session s;
        s.instanceId = instance.id;
        s.account = instance.username;
        s.source = static_cast<Source>(source->currentData().toInt());
        s.query = query->text().trimmed();
        s.firstId = s.source == Source::Search ? 1 : first->text().toLongLong();
        s.lastId = s.source == Source::Range ? last->text().toLongLong() : s.firstId;
        s.limit = limit->value();
        s.removeTagme = remove->isChecked();
        if (s.firstId < 1 || s.lastId < 1 || (s.source == Source::Range && idRange(s.firstId, s.lastId).isEmpty()) ||
            (s.source == Source::Search && s.query.isEmpty())) {
            error->setText(QObject::tr("Use positive IDs, a range of up to 100,000 IDs, or a nonempty search."));
            return;
        }
        s.name = name->text().trimmed();
        if (s.name.isEmpty()) {
            s.name = s.source == Source::Search   ? s.query
                     : s.source == Source::Single ? QStringLiteral("#%1").arg(s.firstId)
                                                  : QStringLiteral("#%1–%2").arg(s.firstId).arg(s.lastId);
            s.name += " · " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");
        }
        result = s;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
    return result;
}

LoginDialog::LoginDialog(BooruProvider *provider, Credentials *credentials, QWidget *parent)
    : QDialog(parent), m_provider(provider), m_credentials(credentials)
{
    setWindowTitle(tr("Sign in · %1").arg(provider->instance().name));
    resize(490, 260);
    auto *layout = new QVBoxLayout(this);
    auto *account = new QLabel(
        tr("%1\n%2").arg(provider->instance().username, provider->instance().baseUrl.toDisplayString()), this);
    account->setTextFormat(Qt::PlainText);
    layout->addWidget(account);
    layout->addWidget(
        new QLabel(provider->instance().kind == ProviderKind::Danbooru ? tr("API key") : tr("Password"), this));
    m_secret = new QLineEdit(this);
    m_secret->setEchoMode(QLineEdit::Password);
    m_secret->setObjectName("loginSecret");
    layout->addWidget(m_secret);
    m_remember = new QCheckBox(tr("Remember in the OS keychain"), this);
    m_remember->setEnabled(Credentials::secureStorageBuiltIn());
    m_remember->setChecked(Credentials::secureStorageBuiltIn() &&
                           QSettings().value("credentials/remember/" + provider->instance().id, false).toBool());
    layout->addWidget(m_remember);
    m_status = new QLabel(Credentials::secureStorageBuiltIn()
                              ? tr("If the OS keychain is unavailable, credentials stay in memory only.")
                              : tr("No keychain support in this build. Credentials will not be persisted."),
                          this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_login = buttons->addButton(tr("Sign in"), QDialogButtonBox::AcceptRole);
    m_login->setObjectName("signInButton");
    m_login->setDefault(true);
    layout->addWidget(buttons);
    connect(m_login, &QPushButton::clicked, this, &LoginDialog::signIn);
    connect(buttons, &QDialogButtonBox::rejected, this, &LoginDialog::reject);
    QPointer<LoginDialog> guard(this);
    credentials->load(provider->instance().id, [this, guard](Result<QString> result) {
        if (!guard || m_pending)
            return;
        if (result && !m_secret->isModified())
            m_secret->setText(*result.value);
        else if (!result)
            m_status->setText(result.error.message);
    });
    m_secret->setFocus();
}

void LoginDialog::signIn()
{
    if (m_pending || !m_provider || m_secret->text().isEmpty())
        return;
    m_pending = true;
    m_login->setEnabled(false);
    m_secret->setEnabled(false);
    m_remember->setEnabled(false);
    m_status->setText(tr("Authenticating…"));
    const auto secret = m_secret->text();
    const auto id = m_provider->instance().id;
    const bool persist = m_remember->isChecked();
    QPointer<LoginDialog> guard(this);
    m_provider->authenticate(secret, [this, guard, secret, id, persist](Result<AuthInfo> result) {
        if (!guard)
            return;
        if (!result) {
            m_pending = false;
            m_login->setEnabled(true);
            m_secret->setEnabled(true);
            m_remember->setEnabled(Credentials::secureStorageBuiltIn());
            m_status->setText(result.error.message);
            m_secret->setFocus();
            return;
        }
        m_status->setText(tr("Signed in. Updating credential storage…"));
        m_credentials->store(id, secret, persist, [this, guard, id, persist](Result<bool> stored) {
            if (!guard)
                return;
            QSettings().setValue("credentials/remember/" + id, persist && static_cast<bool>(stored));
            if (!stored)
                QMessageBox::warning(this, tr("Credential storage"), stored.error.message);
            m_secret->clear();
            accept();
        });
    });
}

void LoginDialog::reject()
{
    if (m_provider && m_pending)
        m_provider->cancel();
    QDialog::reject();
}

bool reviewMerge(const TagMerge &merge, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Concurrent tag changes"));
    dialog.resize(760, 500);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(
        QObject::tr("Someone removed tags you still had in your snapshot. Review the merged result before writing."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(QObject::tr("Server added:\n%1\n\nServer removed:\n%2\n\nProposed tags:\n%3")
                           .arg(sortedTags(merge.remoteAdded).join(' '), sortedTags(merge.remoteRemoved).join(' '),
                                sortedTags(merge.tags).join(' ')));
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *apply = buttons->addButton(QObject::tr("Apply merged tags"), QDialogButtonBox::AcceptRole);
    apply->setObjectName("applyMergedTags");
    apply->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted;
}

VerificationDecision reviewVerification(const QSet<QString> &expected, const Post &actual, bool uncertain,
                                        QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Review server result"));
    dialog.resize(800, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(
        uncertain ? QObject::tr("The write response was uncertain. No write will be repeated without your review.")
                  : QObject::tr("The server returned different tags. Aliases, implications, permissions, or another "
                                "edit may have changed the result."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    const auto serverTags = tagSet(actual.tags);
    text->setPlainText(
        QObject::tr("Expected:\n%1\n\nNow on the server:\n%2\n\nUnexpected tags:\n%3\n\nMissing tags:\n%4")
            .arg(sortedTags(expected).join(' '), sortedTags(serverTags).join(' '),
                 sortedTags(serverTags - expected).join(' '), sortedTags(expected - serverTags).join(' ')));
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *accept = buttons->addButton(QObject::tr("Accept server result as completed"), QDialogButtonBox::AcceptRole);
    auto *retry = buttons->addButton(QObject::tr("Keep draft; allow a new attempt"), QDialogButtonBox::ActionRole);
    auto *cancel = buttons->addButton(QObject::tr("Leave uncertain"), QDialogButtonBox::RejectRole);
    accept->setAutoDefault(false);
    retry->setAutoDefault(false);
    cancel->setDefault(true);
    layout->addWidget(buttons);
    auto decision = VerificationDecision::KeepUncertain;
    QObject::connect(accept, &QPushButton::clicked, &dialog, [&] {
        decision = VerificationDecision::AcceptServer;
        dialog.accept();
    });
    QObject::connect(retry, &QPushButton::clicked, &dialog, [&] {
        decision = VerificationDecision::AllowRetry;
        dialog.accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
    return decision;
}

} // namespace bt
