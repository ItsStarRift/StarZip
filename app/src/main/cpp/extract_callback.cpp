#include "extract_callback.h"

#include "text_util.h"

ExtractCallback::ExtractCallback(int fd, CancelTokenPtr token, ProgressSink *sink,
                                 const std::u16string *password, std::u16string name)
    : fd_(fd), token_(std::move(token)), sink_(sink), password_(password), name_(std::move(name)) {}

Z7_COM7F_IMF(ExtractCallback::SetTotal(UInt64 size)) {
    total_ = size;
    return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::SetCompleted(const UInt64 *completeValue)) {
    if (token_ && token_->flag.load(std::memory_order_relaxed)) return E_ABORT;
    if (completeValue && sink_) sink_->report(*completeValue, total_, name_, false);
    return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::GetStream(UInt32, ISequentialOutStream **outStream, Int32 askExtractMode)) {
    if (!outStream) return E_INVALIDARG;
    *outStream = NULL;
    if (askExtractMode != NArchive::NExtract::NAskMode::kExtract) return S_OK;
    CFdOutStream *spec = new CFdOutStream;
    CMyComPtr<ISequentialOutStream> holder(spec);
    int err = spec->Init(fd_, token_);
    if (err != 0) {
        streamError = HResultFromErrno(err);
        return streamError;
    }
    *outStream = holder.Detach();
    return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::PrepareOperation(Int32)) {
    return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::SetOperationResult(Int32 opRes)) {
    operationResult = opRes;
    resultSeen = true;
    return S_OK;
}

Z7_COM7F_IMF(ExtractCallback::CryptoGetTextPassword(BSTR *password)) {
    if (!password) return E_INVALIDARG;
    *password = NULL;
    if (token_ && token_->flag.load(std::memory_order_relaxed)) return E_ABORT;
    if (!password_ || password_->empty()) {
        passwordRequested = true;
        return E_ABORT;
    }
    return StringToBstr(ToUString(*password_), password);
}
