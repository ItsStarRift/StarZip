#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "Common/MyWindows.h"
#include "Common/MyCom.h"
#include "7zip/IStream.h"

struct CancelToken {
    std::atomic<bool> flag{false};
};

using CancelTokenPtr = std::shared_ptr<CancelToken>;

HRESULT HResultFromErrno(int err);

class FdHolder {
public:
    FdHolder() = default;
    ~FdHolder();
    FdHolder(const FdHolder &) = delete;
    FdHolder &operator=(const FdHolder &) = delete;

    int init(int source, CancelTokenPtr token);
    bool cancelled() const;
    HRESULT seek(Int64 offset, UInt32 origin, UInt64 *newPosition);

    int fd = -1;
    uint64_t pos = 0;

private:
    CancelTokenPtr token_;
};

Z7_class_final(CFdInStream) :
  public IInStream,
  public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
  Z7_IFACE_COM7_IMP(ISequentialInStream)
  Z7_IFACE_COM7_IMP(IInStream)
public:
  int Init(int source, CancelTokenPtr token) { return holder_.init(source, std::move(token)); }
private:
  FdHolder holder_;
};

Z7_class_final(CFdOutStream) :
  public IOutStream,
  public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_2(IOutStream, ISequentialOutStream)
  Z7_IFACE_COM7_IMP(ISequentialOutStream)
  Z7_IFACE_COM7_IMP(IOutStream)
public:
  int Init(int source, CancelTokenPtr token) { return holder_.init(source, std::move(token)); }
private:
  FdHolder holder_;
};
