#pragma once

class AsLocalSystem {
 public:
  AsLocalSystem();
  ~AsLocalSystem();
  AsLocalSystem(const AsLocalSystem&) = delete;
  AsLocalSystem(AsLocalSystem&&) = delete;
  AsLocalSystem& operator=(const AsLocalSystem&) = delete;
  AsLocalSystem& operator=(AsLocalSystem&&) = delete;

  HRESULT Initialize();
  HRESULT Run(PCWSTR cmdline);
  void Terminate();

 private:
  HRESULT ExtractServiceToDisk();
  void DeleteServiceFromDisk();
  HRESULT InstallService();
  void UninstallService();

  wil::unique_schandle service_control_manager_;
  wil::unique_schandle service_;
  bool service_on_disk_;
  wchar_t service_name_[MAX_PATH];
  wchar_t service_path_[MAX_PATH];
};

HRESULT RunAsLocalSystem(PCWSTR cmdline);