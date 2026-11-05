#include "TagEditor.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QUndoStack>
#include <QVBoxLayout>

namespace bt {
namespace {
class TagCommand : public QUndoCommand {
public:
    TagCommand(std::function<void(const TagChanges &)> apply, TagChanges before, TagChanges after)
        : m_apply(std::move(apply)), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { m_apply(m_before); }
    void redo() override { m_apply(m_after); }
private:
    std::function<void(const TagChanges &)> m_apply;
    TagChanges m_before;
    TagChanges m_after;
};

QString categoryName(int category)
{
    switch (category) {
    case 0: return QStringLiteral("general");
    case 1: return QStringLiteral("artist");
    case 3: return QStringLiteral("copyright");
    case 4: return QStringLiteral("character");
    case 5: return QStringLiteral("meta");
    default: return QString::number(category);
    }
}
}

SuggestionModel::SuggestionModel(QObject *parent) : QAbstractListModel(parent) {}

int SuggestionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tags.size();
}

QVariant SuggestionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tags.size())
        return {};
    const auto &tag = m_tags.at(index.row());
    if (role == Qt::EditRole || role == Qt::UserRole)
        return tag.name;
    if (role == Qt::DisplayRole) {
        QString label = tag.name;
        if (!tag.alias.isEmpty())
            label += QStringLiteral("  ← %1").arg(tag.alias);
        if (tag.category)
            label += QStringLiteral("  · %1").arg(categoryName(*tag.category));
        if (tag.postCount)
            label += QStringLiteral("  (%1)").arg(*tag.postCount);
        if (tag.deprecated)
            label += QStringLiteral("  [deprecated]");
        return label;
    }
    return {};
}

void SuggestionModel::setSuggestions(QList<TagSuggestion> suggestions, const QSet<QString> &excluded)
{
    beginResetModel();
    m_tags.clear();
    QSet<QString> seen;
    for (const auto &tag : suggestions) {
        if (!excluded.contains(tag.name) && !seen.contains(tag.name)) {
            seen.insert(tag.name);
            m_tags.append(tag);
        }
    }
    endResetModel();
}

TagEditor::TagEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    m_count = new QLabel(tr("Current tags"), this);
    layout->addWidget(m_count);
    m_tags = new QListWidget(this);
    m_tags->setObjectName("currentTags");
    m_tags->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tags->setUniformItemSizes(true);
    m_tags->setToolTip(tr("Select tags and press Delete to remove them. Ctrl+Z restores removals."));
    layout->addWidget(m_tags, 3);
    auto *buttons = new QHBoxLayout;
    auto *remove = new QPushButton(tr("&Remove selected"), this);
    auto *full = new QPushButton(tr("Edit &full list…"), this);
    buttons->addWidget(remove);
    buttons->addWidget(full);
    layout->addLayout(buttons);
    layout->addWidget(new QLabel(tr("Add tags"), this));
    m_input = new QLineEdit(this);
    m_input->setObjectName("tagInput");
    m_input->setPlaceholderText(tr("Type a tag or paste space-separated tags"));
    m_input->setClearButtonEnabled(true);
    layout->addWidget(m_input);
    layout->addWidget(new QLabel(tr("Pending changes (double-click to reverse)"), this));
    m_diff = new QListWidget(this);
    m_diff->setObjectName("tagChanges");
    m_diff->setUniformItemSizes(true);
    layout->addWidget(m_diff, 1);
    m_undo = new QUndoStack(this);
    m_suggestions = new SuggestionModel(this);
    m_completer = new QCompleter(m_suggestions, this);
    m_completer->setCompletionRole(Qt::EditRole);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setMaxVisibleItems(12);
    m_completer->setCaseSensitivity(Qt::CaseSensitive);
    m_input->setCompleter(m_completer);
    m_input->installEventFilter(this);
    m_completer->popup()->installEventFilter(this);
    connect(m_input, &QLineEdit::textEdited, this, [this](const QString &text) {
        emit prefixChanged(text.trimmed());
    });
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this, [this](const QString &tag) {
        addText(tag);
        m_input->clear();
        m_input->setFocus();
    });
    connect(m_input, &QLineEdit::returnPressed, this, &TagEditor::commitInput);
    connect(remove, &QPushButton::clicked, this, &TagEditor::removeSelected);
    connect(full, &QPushButton::clicked, this, &TagEditor::replaceAll);
    auto *deleteKey = new QShortcut(QKeySequence::Delete, m_tags);
    deleteKey->setContext(Qt::WidgetShortcut);
    connect(deleteKey, &QShortcut::activated, this, &TagEditor::removeSelected);
    connect(m_diff, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item->data(Qt::UserRole + 1).toBool())
            return;
        const auto name = item->data(Qt::UserRole).toString();
        const bool wasRemoval = m_changes.removed.contains(name);
        edit([&](TagChanges &c) { if (wasRemoval) c.add(name); else c.remove(name); });
    });
}

void TagEditor::load(const TagChanges &changes, ProviderKind kind)
{
    m_kind = kind;
    m_removeTagmePreview = false;
    m_undo->clear();
    m_input->clear();
    m_completer->popup()->hide();
    m_suggestions->setSuggestions({}, {});
    apply(changes);
}

void TagEditor::apply(const TagChanges &changes)
{
    m_changes = changes;
    m_tags->clear();
    const auto result = changes.result();
    for (const auto &tag : sortedTags(result)) {
        auto *item = new QListWidgetItem(tag, m_tags);
        if (changes.added.contains(tag))
            item->setForeground(QColor(92, 190, 130));
    }
    m_count->setText(tr("Current tags · %1").arg(result.size()));
    m_diff->clear();
    for (const auto &tag : sortedTags(changes.added)) {
        auto *item = new QListWidgetItem("+ " + tag, m_diff);
        item->setData(Qt::UserRole, tag);
        item->setForeground(QColor(92, 190, 130));
    }
    for (const auto &tag : sortedTags(changes.removed)) {
        auto *item = new QListWidgetItem("− " + tag, m_diff);
        item->setData(Qt::UserRole, tag);
        item->setForeground(QColor(225, 130, 120));
    }
    setRemoveTagmePreview(m_removeTagmePreview);
    emit changed();
}

void TagEditor::setRemoveTagmePreview(bool enabled)
{
    m_removeTagmePreview = enabled;
    for (int i = m_diff->count() - 1; i >= 0; --i) {
        if (m_diff->item(i)->data(Qt::UserRole + 1).toBool())
            delete m_diff->takeItem(i);
    }
    if (enabled && m_changes.result().contains("tagme")) {
        auto *item = new QListWidgetItem(tr("− tagme (on save, confirmed finished)"), m_diff);
        item->setData(Qt::UserRole + 1, true);
        item->setForeground(QColor(225, 130, 120));
    }
}

bool TagEditor::eventFilter(QObject *object, QEvent *event)
{
    if ((object == m_input || object == m_completer->popup()) && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && m_completer->popup()->isVisible()) {
            m_completer->popup()->hide();
            return true;
        }
        const bool accept = key->key() == Qt::Key_Tab || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (accept && key->modifiers() == Qt::NoModifier && m_completer->popup()->isVisible()) {
            auto index = m_completer->popup()->currentIndex();
            if (!index.isValid())
                index = m_completer->completionModel()->index(0, 0);
            addText(index.data(Qt::EditRole).toString());
            m_input->clear();
            m_completer->popup()->hide();
            m_input->setFocus();
            emit prefixChanged({});
            return true;
        }
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier && !m_input->text().isEmpty()) {
            commitInput();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void TagEditor::edit(const std::function<void(TagChanges &)> &operation)
{
    auto next = m_changes;
    operation(next);
    if (next != m_changes)
        m_undo->push(new TagCommand([this](const TagChanges &c) { apply(c); }, m_changes, next));
}

void TagEditor::addText(const QString &text)
{
    const auto tags = splitTags(text, m_kind);
    edit([&](TagChanges &c) { for (const auto &tag : tags) c.add(tag); });
}

void TagEditor::commitInput()
{
    addText(m_input->text());
    m_input->clear();
    m_completer->popup()->hide();
    emit prefixChanged({});
}

void TagEditor::removeSelected()
{
    QStringList selected;
    for (auto *item : m_tags->selectedItems())
        selected.append(item->text());
    edit([&](TagChanges &c) { for (const auto &tag : selected) c.remove(tag); });
}

void TagEditor::replaceAll()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit all tags"));
    dialog.resize(580, 340);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(sortedTags(m_changes.result()).join(' '), &dialog);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto desired = tagSet(splitTags(text->toPlainText(), m_kind));
    edit([&](TagChanges &c) {
        c.added = desired - c.original;
        c.removed = c.original - desired;
    });
    focusInput();
}

void TagEditor::setSuggestions(const QList<TagSuggestion> &suggestions)
{
    m_suggestions->setSuggestions(suggestions, m_changes.result());
    if (!m_input->text().isEmpty() && m_input->hasFocus() && m_suggestions->rowCount() > 0) {
        m_completer->setCompletionPrefix(m_input->text());
        m_completer->complete();
    } else {
        m_completer->popup()->hide();
    }
}

void TagEditor::focusInput() { m_input->setFocus(); }
void TagEditor::undo() { m_undo->undo(); }
void TagEditor::redo() { m_undo->redo(); }

}
