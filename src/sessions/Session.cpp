#include "Session.h"

namespace bt {

QString stateName(SubmissionState state)
{
    switch (state) {
    case SubmissionState::Editing: return "editing";
    case SubmissionState::Pending: return "pending";
    case SubmissionState::Submitting: return "submitting";
    case SubmissionState::Confirming: return "confirming";
    case SubmissionState::Reviewing: return "reviewing";
    case SubmissionState::Completed: return "completed";
    case SubmissionState::Failed: return "failed";
    case SubmissionState::Uncertain: return "uncertain";
    }
    return "uncertain";
}

SubmissionState stateFromName(const QString &name)
{
    for (auto state : {SubmissionState::Editing, SubmissionState::Pending, SubmissionState::Submitting,
                       SubmissionState::Confirming, SubmissionState::Reviewing, SubmissionState::Completed,
                       SubmissionState::Failed, SubmissionState::Uncertain}) {
        if (name == stateName(state))
            return state;
    }
    return SubmissionState::Uncertain;
}

bool submissionBusy(SubmissionState state)
{
    return state == SubmissionState::Pending || state == SubmissionState::Submitting
        || state == SubmissionState::Confirming || state == SubmissionState::Reviewing;
}

}
