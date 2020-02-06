#include "runas_localsystem.h"

AsLocalSystem::AsLocalSystem()
    : service_on_disk_{false}, service_name_{}, service_path_{} {}

AsLocalSystem::~AsLocalSystem() { Terminate(); }

HRESULT AsLocalSystem::Initialize() {
  auto len =
      GetModuleFileNameW(nullptr, service_path_, ARRAYSIZE(service_path_));
  if (len == 0 || len == ARRAYSIZE(service_path_)) {
    service_path_[0] = 0;
    return ERROR_INSUFFICIENT_BUFFER;
  }
  HRESULT hr;
  auto fail =
      FAILED(hr = StringCchPrintfW(service_name_, ARRAYSIZE(service_name_),
                                   L"gcheapstatsvc%" PRIu32,
                                   GetCurrentProcessId())) ||
      FAILED(hr = PathCchRemoveFileSpec(service_path_,
                                        ARRAYSIZE(service_path_))) ||
      FAILED(hr = PathCchCombine(service_path_, ARRAYSIZE(service_path_),
                                 service_path_, service_name_)) ||
      FAILED(hr = PathCchRenameExtension(service_path_,
                                         ARRAYSIZE(service_path_), L"exe"));
  if (fail) {
    service_name_[0] = 0;
    service_path_[0] = 0;
    return hr;
  }
  hr = ExtractServiceToDisk();
  if (FAILED(hr)) return hr;
  hr = InstallService();
  return hr;
}

HRESULT AsLocalSystem::Run(PCWSTR cmdline) {
  if (!service_) return E_FAIL;
  // Wait service stopped
  for (;;) {
    DWORD unused;
    SERVICE_STATUS_PROCESS status{};
    auto ok = QueryServiceStatusEx(service_.get(), SC_STATUS_PROCESS_INFO,
                                   reinterpret_cast<LPBYTE>(&status),
                                   sizeof(SERVICE_STATUS_PROCESS), &unused);
    if (!ok) {
      auto error = GetLastError();
      LogError(L"Could not query service status!\n");
      return HRESULT_FROM_WIN32(error);
    }
    if (status.dwCurrentState == SERVICE_STOPPED) break;
    if (IsCancelled()) return S_FALSE;
    Sleep(1);
  }
  // Start process
  auto argc = 0;
  auto argv = CommandLineToArgvW(cmdline, &argc);
  auto ok =
      (StartServiceW(service_.get(), argc, const_cast<PCWSTR*>(argv)) != 0);
  auto error = GetLastError();
  LocalFree(argv);
  return ok ? S_OK : HRESULT_FROM_WIN32(error);
}

void AsLocalSystem::Terminate() {
  UninstallService();
  DeleteServiceFromDisk();
}

HRESULT AsLocalSystem::ExtractServiceToDisk() {
  if (service_on_disk_) return true;
  // Load resource
  auto current_module = GetModuleHandleW(nullptr);
  auto rc = FindResourceW(current_module, L"GCHEAPSTATSVC", L"BINARY");
  if (!rc) return HRESULT_FROM_WIN32(GetLastError());
  auto rch = LoadResource(NULL, rc);
  if (!rch) return HRESULT_FROM_WIN32(GetLastError());
  // Create file
  wil::unique_hfile file{CreateFileW(service_path_, GENERIC_WRITE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_HIDDEN,
                                     nullptr)};
  if (!file) return HRESULT_FROM_WIN32(GetLastError());
  // Write file
  DWORD to_write = SizeofResource(NULL, rc);
  DWORD written = 0;
  auto ok =
      WriteFile(file.get(), LockResource(rch), to_write, &written, nullptr);
  auto error = GetLastError();
  file.reset();
  if (!ok || to_write != written) {
    DeleteFile(service_path_);
    return HRESULT_FROM_WIN32(error);
  }
  service_on_disk_ = true;
  return S_OK;
}

void AsLocalSystem::DeleteServiceFromDisk() {
  if (!service_on_disk_) return;
  for (; !DeleteFile(service_path_); Sleep(1)) {
    if (IsCancelled()) return;
  }
  service_on_disk_ = false;
}

HRESULT AsLocalSystem::InstallService() {
  if (service_) return true;
  if (!service_control_manager_) {
    service_control_manager_.reset(
        OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS));
    if (!service_control_manager_) return HRESULT_FROM_WIN32(GetLastError());
  }
  service_.reset(CreateServiceW(
      service_control_manager_.get(), service_name_, service_name_,
      SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
      SERVICE_ERROR_NORMAL, service_path_, NULL, NULL, NULL, NULL, NULL));
  return service_ ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

void AsLocalSystem::UninstallService() {
  if (!service_) return;
  if (!DeleteService(service_.get())) {
    auto error = HRESULT_FROM_WIN32(GetLastError());
    LogError(L"Could not delete service! Error 0x%08lx\n", error);
    _ASSERT_EXPR(false, L"Could not delete service!");
  }
  service_.release();
}

HRESULT RunAsLocalSystem(PCWSTR cmdline) {
  AsLocalSystem aslocalsystem;
  auto hr = aslocalsystem.Initialize();
  if (hr == S_OK) hr = aslocalsystem.Run(cmdline);
  return hr;
}