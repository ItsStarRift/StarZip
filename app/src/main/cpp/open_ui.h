#pragma once
#include <string>

#include "Common/MyWindows.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "7zip/UI/Common/ArchiveOpenCallback.h"
#include "fd_stream.h"

class OpenUi : public IOpenCallbackUI {
public:
    OpenUi(CancelTokenPtr token, const std::u16string *password)
        : token_(std::move(token)), password_(password) {}
    virtual ~OpenUi() {}

    bool passwordRequested = false;

    Z7_IFACE_IMP(IOpenCallbackUI)

private:
    bool cancelled() const { return token_ && token_->flag.load(std::memory_order_relaxed); }
    CancelTokenPtr token_;
    const std::u16string *password_;
};
