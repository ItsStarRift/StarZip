#pragma once
#include <jni.h>

#include <cstdint>
#include <mutex>
#include <string>

#include "Common/MyWindows.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "Common/MyVector.h"
#include "7zip/UI/Common/LoadCodecs.h"
#include "7zip/UI/Common/OpenArchive.h"
#include "7zip/UI/Common/Property.h"
#include "archive_tree.h"
#include "fd_stream.h"
#include "open_ui.h"
#include "session.h"

constexpr jint kResultPassword = 3;
constexpr jint kResultNotArchive = 4;
constexpr jint kResultDataError = 5;

class ArchiveSession {
public:
    ArchiveSession();
    ~ArchiveSession();
    ArchiveSession(const ArchiveSession &) = delete;
    ArchiveSession &operator=(const ArchiveSession &) = delete;

    void setPassword(std::u16string password);
    jint open(int fd, const std::u16string &name);
    jint extract(uint32_t node, int fd, ProgressSink *sink);
    HRESULT itemCount(uint32_t &count);
    const ArchiveTree &tree() const { return tree_; }
    void cancel();

    std::mutex mutex;

private:
    void shutdown();
    void wipePassword();

    CancelTokenPtr token_;
    std::u16string password_;
    CMyComPtr<IUnknown> codecsRef_;
    CCodecs *codecs_ = nullptr;
    CMyComPtr<IInStream> stream_;
    OpenUi ui_;
    CObjectVector<CProperty> props_;
    CObjectVector<COpenType> types_;
    CIntVector excluded_;
    CArchiveLink link_;
    ArchiveTree tree_;
    bool opened_ = false;
};
