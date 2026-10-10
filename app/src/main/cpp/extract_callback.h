#pragma once
#include <cstdint>
#include <string>

#include "Common/MyWindows.h"
#include "Common/MyCom.h"
#include "7zip/IPassword.h"
#include "7zip/Archive/IArchive.h"
#include "fd_stream.h"
#include "session.h"

class ExtractCallback Z7_final :
  public IArchiveExtractCallback,
  public ICryptoGetTextPassword,
  public CMyUnknownImp
{
  Z7_IFACES_IMP_UNK_2(IArchiveExtractCallback, ICryptoGetTextPassword)
  Z7_IFACE_COM7_IMP(IProgress)

  int fd_;
  CancelTokenPtr token_;
  ProgressSink *sink_;
  const std::u16string *password_;
  std::u16string name_;
  uint64_t total_ = 0;

public:
  ExtractCallback(int fd, CancelTokenPtr token, ProgressSink *sink,
                  const std::u16string *password, std::u16string name);

  bool passwordRequested = false;
  bool resultSeen = false;
  Int32 operationResult = 0;
  HRESULT streamError = S_OK;
};
