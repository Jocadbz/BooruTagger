#pragma once

#include "tagging/TagChanges.h"
#include <QAbstractListModel>
#include <QWidget>

class QCompleter;
class QLabel;
class QLineEdit;
class QListWidget;
class QUndoStack;

namespace bt {

class SuggestionModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit SuggestionModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    void setSuggestions(QList<TagSuggestion> suggestions, const QSet<QString> &excluded);
private:
    QList<TagSuggestion> m_tags;
};

class TagEditor : public QWidget {
    Q_OBJECT
public:
    explicit TagEditor(QWidget *parent = nullptr);
    void load(const TagChanges &changes, ProviderKind kind);
    const TagChanges &changes() const { return m_changes; }
    void setSuggestions(const QList<TagSuggestion> &suggestions);
    void focusInput();
    void undo();
    void redo();
    void commitInput();
    void addText(const QString &text);
    void setRemoveTagmePreview(bool enabled);
    void replaceAll();
signals:
    void changed();
    void prefixChanged(const QString &prefix);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void apply(const TagChanges &changes);
    void edit(const std::function<void(TagChanges &)> &operation);
    void removeSelected();
    TagChanges m_changes;
    ProviderKind m_kind = ProviderKind::Danbooru;
    QListWidget *m_tags;
    QListWidget *m_diff;
    QLineEdit *m_input;
    QLabel *m_count;
    QUndoStack *m_undo;
    QCompleter *m_completer;
    SuggestionModel *m_suggestions;
    bool m_removeTagmePreview = false;
};

}
