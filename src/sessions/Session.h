#pragma once

#include "tagging/TagChanges.h"

namespace bt {

enum class Source { Single, Range, Search };
enum class SubmissionState { Editing, Pending, Submitting, Confirming, Reviewing, Completed, Failed, Uncertain };

QString stateName(SubmissionState state);
SubmissionState stateFromName(const QString &name);
bool submissionBusy(SubmissionState state);

struct Session {
    QString id;
    QString name;
    QString instanceId;
    QString account;
    Source source = Source::Search;
    QString query = "tagme";
    qint64 firstId = 1;
    qint64 lastId = 1;
    int limit = 120;
    bool removeTagme = true;
    int current = 0;
    QString status = "building";
    QString cursor;
    int offset = 0;
};

struct QueueEntry {
    qint64 postId = 0;
    int position = 0;
    QString status;
    QString error;
};

struct Draft {
    TagChanges changes;
    SubmissionState state = SubmissionState::Editing;
    QSet<QString> intended;
    QSet<QString> baseline;
    bool hasIntended = false;
    bool sufficient = false;
    QString error;
};

struct HistoryEntry {
    qint64 postId = 0;
    QString sessionId;
    QSet<QString> original;
    QSet<QString> result;
    QString outcome;
    QString error;
    QDateTime time;
};

}

Q_DECLARE_METATYPE(bt::Session)
Q_DECLARE_METATYPE(bt::SubmissionState)
