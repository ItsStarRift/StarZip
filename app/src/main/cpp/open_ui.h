#pragma once
#include "Common/MyWindows.h"
#include "Common/MyString.h"
#include "7zip/UI/Common/ArchiveOpenCallback.h"
#include "fd_stream.h"

class OpenUi : public IOpenCallbackUI {
public:
    explicit OpenUi(CancelTokenPtr token) : token_(std::move(token)) {}
    virtual ~OpenUi() {}

    bool passwordRequested = false;

    Z7_IFACE_IMP(IOpenCallbackUI)

private:
    bool cancelled() const { return token_ && token_->flag.load(std::memory_order_relaxed); }
    CancelTokenPtr token_;
};
