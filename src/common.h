#pragma once

auto constexpr kPipeNameFormat = L"\\pipe\\gcheapstat_pid%" PRIu32;

template <typename T>
class Proxy {
 protected:
  explicit Proxy(T *ptr) {
    _ASSERT(ptr);
    auto lock = Mutex.lock_exclusive();
    _ASSERT(!Instance);
    Instance = ptr;
  }
  ~Proxy() {
    // Not virtual intentionally
    auto lock = Mutex.lock_exclusive();
    Instance = nullptr;
  }
  Proxy(const Proxy &) = delete;
  Proxy(Proxy &&) = delete;
  Proxy &operator=(const Proxy &) = delete;
  Proxy &operator=(Proxy &&) = delete;

  static wil::srwlock Mutex;
  static T *Instance;
};

template <typename T>
wil::srwlock Proxy<T>::Mutex;
template <typename T>
T *Proxy<T>::Instance;