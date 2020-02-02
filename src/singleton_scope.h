#pragma once

template <typename T>
class SingletonScope final {
 public:
  explicit SingletonScope(T *ptr) {
    _ASSERT(ptr);
    auto lock = Mutex.lock_exclusive();
    _ASSERT(!Ptr);
    Ptr = ptr;
  }
  ~SingletonScope() {
    auto lock = Mutex.lock_exclusive();
    Ptr = nullptr;
  }
  SingletonScope(const SingletonScope &) = delete;
  SingletonScope(SingletonScope &&) = delete;
  SingletonScope &operator=(const SingletonScope &) = delete;
  SingletonScope &operator=(SingletonScope &&) = delete;

  template <typename... Params, typename... Args>
  static void Invoke(void (T::*mf)(Params...), Args &&... args) {
    auto lock = Mutex.lock_shared();
    _ASSERT(Ptr);
    if (Ptr) (Ptr->*mf)(std::forward<Args>(args)...);
  }

  template <typename... Params, typename... Args>
  static HRESULT Invoke(HRESULT (T::*mf)(Params...), Args &&... args) {
    auto lock = Mutex.lock_shared();
    _ASSERT(Ptr);
    return Ptr ? (Ptr->*mf)(std::forward<Args>(args)...) : E_POINTER;
  }

 private:
  static wil::srwlock Mutex;
  static T *Ptr;
};

template <typename T>
wil::srwlock SingletonScope<T>::Mutex;
template <typename T>
T *SingletonScope<T>::Ptr;