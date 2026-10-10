#include "open_ui.h"

#include "text_util.h"

HRESULT OpenUi::Open_CheckBreak() {
    return cancelled() ? E_ABORT : S_OK;
}

HRESULT OpenUi::Open_SetTotal(const UInt64 *, const UInt64 *) {
    return cancelled() ? E_ABORT : S_OK;
}

HRESULT OpenUi::Open_SetCompleted(const UInt64 *, const UInt64 *) {
    return cancelled() ? E_ABORT : S_OK;
}

HRESULT OpenUi::Open_Finished() {
    return S_OK;
}

HRESULT OpenUi::Open_CryptoGetTextPassword(BSTR *password) {
    if (!password) return E_INVALIDARG;
    *password = NULL;
    if (cancelled()) return E_ABORT;
    if (!password_ || password_->empty()) {
        passwordRequested = true;
        return E_ABORT;
    }
    return StringToBstr(ToUString(*password_), password);
}
