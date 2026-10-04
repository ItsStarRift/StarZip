#define _FILE_OFFSET_BITS 64
#include "Common/MyWindows.h"
#include "Common/MyInitGuid.h"
#include "fd_stream.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const UInt32 kSeekSet = 0;
const UInt32 kSeekCur = 1;
const UInt32 kSeekEnd = 2;

const HRESULT kNegativeSeek = static_cast<HRESULT>(0x80070083u);
const HRESULT kDiskFull = static_cast<HRESULT>(0x80070070u);

}

HRESULT HResultFromErrno(int err) {
    switch (err) {
        case ENOSPC:
        case EDQUOT:
            return kDiskFull;
        case ENOMEM:
            return E_OUTOFMEMORY;
        case EACCES:
        case EPERM:
        case EROFS:
            return E_ACCESSDENIED;
        case ESPIPE:
            return E_NOTIMPL;
        case EINVAL:
            return E_INVALIDARG;
        default:
            return static_cast<HRESULT>(0x80070000u | (static_cast<unsigned>(err) & 0xFFFFu));
    }
}

FdHolder::~FdHolder() {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

int FdHolder::init(int source, CancelTokenPtr token) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
    int copy = fcntl(source, F_DUPFD_CLOEXEC, 0);
    if (copy < 0) return errno;
    if (lseek64(copy, 0, SEEK_CUR) < 0) {
        int err = errno;
        ::close(copy);
        return err;
    }
    fd = copy;
    pos = 0;
    token_ = std::move(token);
    return 0;
}

bool FdHolder::cancelled() const {
    return token_ && token_->flag.load(std::memory_order_relaxed);
}

HRESULT FdHolder::seek(Int64 offset, UInt32 origin, UInt64 *newPosition) {
    if (fd < 0) return E_FAIL;
    int64_t base = 0;
    switch (origin) {
        case kSeekSet:
            base = 0;
            break;
        case kSeekCur:
            base = static_cast<int64_t>(pos);
            break;
        case kSeekEnd: {
            struct stat st;
            if (fstat(fd, &st) != 0) return HResultFromErrno(errno);
            base = static_cast<int64_t>(st.st_size);
            break;
        }
        default:
            return E_INVALIDARG;
    }
    int64_t target = 0;
    if (__builtin_add_overflow(base, static_cast<int64_t>(offset), &target) || target < 0)
        return kNegativeSeek;
    pos = static_cast<uint64_t>(target);
    if (newPosition) *newPosition = pos;
    return S_OK;
}

Z7_COM7F_IMF(CFdInStream::Read(void *data, UInt32 size, UInt32 *processedSize)) {
    if (processedSize) *processedSize = 0;
    if (holder_.fd < 0) return E_FAIL;
    if (holder_.cancelled()) return E_ABORT;
    if (size == 0) return S_OK;
    for (;;) {
        ssize_t n = pread64(holder_.fd, data, size, static_cast<off64_t>(holder_.pos));
        if (n >= 0) {
            holder_.pos += static_cast<uint64_t>(n);
            if (processedSize) *processedSize = static_cast<UInt32>(n);
            return S_OK;
        }
        if (errno == EINTR) {
            if (holder_.cancelled()) return E_ABORT;
            continue;
        }
        return HResultFromErrno(errno);
    }
}

Z7_COM7F_IMF(CFdInStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)) {
    return holder_.seek(offset, seekOrigin, newPosition);
}

Z7_COM7F_IMF(CFdOutStream::Write(const void *data, UInt32 size, UInt32 *processedSize)) {
    if (processedSize) *processedSize = 0;
    if (holder_.fd < 0) return E_FAIL;
    const Byte *bytes = static_cast<const Byte *>(data);
    UInt32 total = 0;
    HRESULT result = S_OK;
    while (total < size) {
        if (holder_.cancelled()) {
            result = E_ABORT;
            break;
        }
        ssize_t n = pwrite64(holder_.fd, bytes + total, size - total,
                             static_cast<off64_t>(holder_.pos));
        if (n < 0) {
            if (errno == EINTR) continue;
            result = HResultFromErrno(errno);
            break;
        }
        if (n == 0) {
            result = E_FAIL;
            break;
        }
        total += static_cast<UInt32>(n);
        holder_.pos += static_cast<uint64_t>(n);
    }
    if (processedSize) *processedSize = total;
    return result;
}

Z7_COM7F_IMF(CFdOutStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)) {
    return holder_.seek(offset, seekOrigin, newPosition);
}

Z7_COM7F_IMF(CFdOutStream::SetSize(UInt64 newSize)) {
    if (holder_.fd < 0) return E_FAIL;
    for (;;) {
        if (ftruncate64(holder_.fd, static_cast<off64_t>(newSize)) == 0) return S_OK;
        if (errno == EINTR) continue;
        return HResultFromErrno(errno);
    }
}
