#pragma once
#include "common.h"
#include "process_context.h"
#include "rpc_h.h"
#include "rpc_helpers.h"

class RpcServer final {
 public:
  template <uint32_t BufferSize>
  RpcServer(wchar_t (&buffer)[BufferSize])
      : buffer_{buffer}, buffer_size_{BufferSize} {}
  HRESULT Run(PWSTR pipename);

 private:
  HRESULT CalculateMtStat(DWORD pid, size_t *size);
  HRESULT GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  HRESULT GetMtName(uintptr_t addr, LPBSTR name);

  template <typename... Args>
  void LogError(PCWSTR format, Args &&... args) {
    auto hr = StringCchPrintfW(buffer_, buffer_size_, format,
                               std::forward<Args>(args)...);
    _ASSERT(SUCCEEDED(hr));
    TryExceptRpc(&RpcProxyLogError, application_binding_.get(),
                 _bstr_t{buffer_}.GetBSTR());
  }

  ProcessContext process_context_;
  std::vector<MtStat> mtstat_;
  wchar_t *buffer_;
  uint32_t buffer_size_;
  wil::unique_rpc_binding application_binding_;
  friend HRESULT RpcStubCalculateMtStat(handle_t handle, DWORD pid,
                                        PSIZE_T size);
  friend HRESULT RpcStubGetMtStat(handle_t handle, SIZE_T offset, UINT size,
                                  MtStat mtstat[]);
  friend HRESULT RpcStubGetMtName(handle_t handle, UINT_PTR addr, LPBSTR name);
  friend class Log;
};