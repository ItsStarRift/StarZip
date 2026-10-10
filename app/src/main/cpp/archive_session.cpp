#include "archive_session.h"

#include <algorithm>

#include "extract_callback.h"
#include "text_util.h"

ArchiveSession::ArchiveSession()
    : token_(std::make_shared<CancelToken>()), ui_(token_, &password_) {}

ArchiveSession::~ArchiveSession() {
    try {
        shutdown();
    } catch (...) {
    }
}

void ArchiveSession::wipePassword() {
    std::fill(password_.begin(), password_.end(), u'\0');
    password_.clear();
}

void ArchiveSession::setPassword(std::u16string password) {
    wipePassword();
    password_ = std::move(password);
}

void ArchiveSession::shutdown() {
    link_.Close();
    link_.Release();
    stream_.Release();
    codecsRef_.Release();
    codecs_ = nullptr;
    opened_ = false;
    wipePassword();
}

void ArchiveSession::cancel() {
    token_->flag.store(true);
}

jint ArchiveSession::open(int fd, const std::u16string &name) {
    CCodecs *codecs = new CCodecs;
    codecsRef_ = codecs;
    codecs_ = codecs;
    if (codecs_->Load() != S_OK) return kResultError;

    CFdInStream *spec = new CFdInStream;
    stream_ = spec;
    if (spec->Init(fd, token_) != 0) return kResultError;

    COpenOptions op;
    op.props = &props_;
    op.codecs = codecs_;
    op.types = &types_;
    op.excludedFormats = &excluded_;
    op.stdInMode = false;
    op.stream = stream_;
    op.filePath = ToUString(name);

    HRESULT result = link_.Open_Strict(op, &ui_);
    if (result == S_OK && !link_.Arcs.IsEmpty()) {
        HRESULT built = tree_.build(link_.GetArchive(), token_);
        if (built == E_ABORT) return kResultCancelled;
        if (built != S_OK) return kResultError;
        opened_ = true;
        return kResultOk;
    }
    if (result == E_ABORT) return ui_.passwordRequested ? kResultPassword : kResultCancelled;
    if (result == S_FALSE || result == S_OK) return kResultNotArchive;
    return kResultError;
}

jint ArchiveSession::extract(uint32_t node, int fd, ProgressSink *sink) {
    if (!opened_ || link_.Arcs.IsEmpty()) return kResultError;
    if (node == 0 || node >= tree_.nodeCount()) return kResultError;
    const TreeNode &entry = tree_.at(node);
    if (entry.isDir || entry.itemIndex < 0) return kResultError;

    token_->flag.store(false);
    ExtractCallback *spec = new ExtractCallback(fd, token_, sink, &password_, tree_.name(entry));
    CMyComPtr<IArchiveExtractCallback> callback(spec);
    const UInt32 index = static_cast<UInt32>(entry.itemIndex);
    HRESULT result = link_.GetArchive()->Extract(&index, 1, 0, callback);
    if (result == E_ABORT) return spec->passwordRequested ? kResultPassword : kResultCancelled;
    if (result != S_OK || spec->streamError != S_OK) return kResultError;
    if (!spec->resultSeen || spec->operationResult != NArchive::NExtract::NOperationResult::kOK)
        return kResultDataError;
    if (sink) sink->report(1, 1, tree_.name(entry), true);
    return kResultOk;
}

HRESULT ArchiveSession::itemCount(uint32_t &count) {
    count = 0;
    if (!opened_ || link_.Arcs.IsEmpty()) return E_FAIL;
    UInt32 total = 0;
    HRESULT result = link_.GetArchive()->GetNumberOfItems(&total);
    count = total;
    return result;
}
