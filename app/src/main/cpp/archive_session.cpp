#include "archive_session.h"

namespace {

UString ToUString(const std::u16string &s) {
    std::wstring wide;
    wide.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() &&
            s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00);
            ++i;
        }
        wide.push_back(static_cast<wchar_t>(cp));
    }
    return UString(wide.c_str());
}

}

ArchiveSession::ArchiveSession()
    : token_(std::make_shared<CancelToken>()), ui_(token_) {}

ArchiveSession::~ArchiveSession() {
    try {
        shutdown();
    } catch (...) {
    }
}

void ArchiveSession::shutdown() {
    link_.Close();
    link_.Release();
    stream_.Release();
    codecsRef_.Release();
    codecs_ = nullptr;
    opened_ = false;
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
        opened_ = true;
        return kResultOk;
    }
    if (result == E_ABORT) return ui_.passwordRequested ? kResultPassword : kResultCancelled;
    if (result == S_FALSE || result == S_OK) return kResultNotArchive;
    return kResultError;
}

HRESULT ArchiveSession::itemCount(uint32_t &count) {
    count = 0;
    if (!opened_ || link_.Arcs.IsEmpty()) return E_FAIL;
    UInt32 total = 0;
    HRESULT result = link_.GetArchive()->GetNumberOfItems(&total);
    count = total;
    return result;
}
