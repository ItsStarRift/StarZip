#include "open_ui.h"

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
    if (password) *password = NULL;
    passwordRequested = true;
    return E_ABORT;
}
