#pragma once
#include "dacprivate.h"
#include "logger.h"
#include "rpc_h.h"  // MtStat

#ifdef _WIN64
auto constexpr kObjectHeaderSize = 8;
auto constexpr kAlignment = 8;
#else
auto constexpr kObjectHeaderSize = 4;
auto constexpr kAlignment = 4;
#endif
auto constexpr kAlignmentLarge = 8;
auto constexpr kMinObjectSize = sizeof(uintptr_t) +  // Method table address
                                kObjectHeaderSize + sizeof(size_t);

class MtStatCalculator {
 public:
  MtStatCalculator();
  HRESULT Initialize(HANDLE hprocess, ISOSDacInterface* sos_dac_interface);
  HRESULT Calculate(std::vector<MtStat>& mtstat);

 private:
  struct AllocationContext {
    uintptr_t ptr;
    uintptr_t limit;
  };
  struct Segment {
    uintptr_t addr;
    DacpGcHeapDetails* heap;
    DacpHeapSegmentData data;
  };
  struct MtAddrStat {
    size_t sizeof_base;
    size_t sizeof_component;
    size_t count;
    size_t size_total;
  };

#ifndef _WIN64
  template <size_t Alignment>
  inline void WalkSegment(CLRDATA_ADDRESS mem, CLRDATA_ADDRESS allocated,
                          PCWSTR name) {
    WalkSegment<Alignment>(static_cast<uintptr_t>(mem),
                           static_cast<uintptr_t>(allocated), name);
  }
#endif

  template <size_t Alignment>
  void WalkSegment(uintptr_t mem, uintptr_t allocated, PCWSTR name) {
    if (allocated < mem) {
      LogError(L"Invalid segment range encountered, %s segment\n", name);
      return;
    }
    auto size = static_cast<size_t>(allocated - mem);
    if (!size) {
      LogError(L"Empty segment encountered, %s segment\n", name);
      return;
    }
    std::vector<BYTE> buffer(size);
    SIZE_T read = 0;
    if (!ReadProcessMemory(hprocess_, reinterpret_cast<LPCVOID>(mem),
                           &buffer[0], size, &read)) {
      auto hr = HRESULT_FROM_WIN32(GetLastError());
      LogError(L"Error reading segment memory, code 0x%08lx, %s segment\n", hr,
               name);
      return;
    }
    if (read != size) {
      LogError(
          L"Incomplete segment memory read, bytes requested %zu, read %zu, "
          L"%s segment\n",
          size, read, name);
      return;
    }
    PBYTE ptr = &buffer[0], end = &buffer[0] + size;
    for (auto it = std::find_if(allocation_contexts_.cbegin(),
                                allocation_contexts_.cend(),
                                [mem, allocated](auto& a) {
                                  return mem <= a.ptr && a.ptr < allocated;
                                });
         it != allocation_contexts_.cend(); ++it) {
      WalkMemory<Alignment>(ptr, (std::min)(it->ptr, allocated) - mem, name);
      if (allocated < it->limit) {
        LogError(
            L"Allocation context limit goes beyond %s segment boundaries\n",
            name);
        return;
      }
      auto limit = it->limit + Align<kAlignment>(kMinObjectSize);
      if (allocated < limit) {
        LogError(
            L"Aligned allocation context limit goes beyond %s segment "
            L"boundaries\n",
            name);
        return;
      }
      size = allocated - limit;
      ptr = end - size;
    }
    WalkMemory<Alignment>(ptr, size, name);
  }

  template <size_t Alignment>
  void WalkMemory(PBYTE ptr, size_t size, PCWSTR name) {
    for (size_t object_size; kMinObjectSize <= size;
         ptr += object_size, size -= object_size) {
      if (IsCancelled()) return;
      // Get method table address
      auto mt = *reinterpret_cast<uintptr_t*>(ptr) & ~3;
      if (!mt) {
        LogError(
            L"Zero method table address encountered, skip %zu bytes, %s "
            L"segment\n",
            size, name);
        return;
      }
      // Get method table data
      auto it = dict_.find(mt);
      if (it == dict_.end()) {
        DacpMethodTableData mt_data{};
        auto hr = mt_data.Request(sos_dac_interface_.get(), mt);
        if (FAILED(hr)) {
          LogError(
              L"Error getting method table data, code 0x%08lx, skip %zu "
              L"bytes, %s segment\n",
              hr, name);
          return;
        }
        MtAddrStat stat{mt_data.BaseSize, mt_data.ComponentSize};
        it = dict_.emplace(mt, stat).first;
      }
      // Calculate object size
      auto& stat = it->second;
      auto component_count = *reinterpret_cast<PDWORD>(ptr + sizeof(uintptr_t));
      if (mt == useful_globals_.StringMethodTable) {
        // The component size on a String does not contain the trailing NULL
        // character, so we must add that ourselves.
        ++component_count;
      }
      object_size = stat.sizeof_base + component_count * stat.sizeof_component;
#if _WIN64
      if (object_size < kMinObjectSize) object_size = kMinObjectSize;
#endif
      // Validate object size
      if (!object_size || size < object_size) {
        LogError(
            L"Object size %zu is out of valid range, skip %zu bytes, %s "
            L"segment\n",
            object_size, size, name);
        return;
      }
      // Update statistics
      ++stat.count;
      stat.size_total += object_size;
      // Align object size
      object_size = Align<Alignment>(object_size);
      if (!object_size || size < object_size) {
        LogError(
            L"Aligned object size is out of valid range, value %zu, skip %zu "
            L"bytes, %s segment\n",
            object_size, size, name);
        return;
      }
    }
    if (size) LogError(L"Skip %zu bytes of %s segment", size, name);
  }

  template <size_t Alignment>
  inline uintptr_t Align(uintptr_t v) {
    auto constexpr kAlign = Alignment - 1;
    return (v + kAlign) & ~kAlign;
  }

  HANDLE hprocess_;
  wil::com_ptr<ISOSDacInterface> sos_dac_interface_;
  DacpGcHeapData info_;
  DacpUsefulGlobalsData useful_globals_;
  std::vector<AllocationContext> allocation_contexts_;
  std::vector<DacpGcHeapDetails> heaps_;
  std::array<std::vector<Segment>, 2> segments_;  // [0] - small & ephemeral
                                                  // [1] - large
  std::unordered_map<uintptr_t, MtAddrStat> dict_;
};

HRESULT CalculateMtStat(HANDLE hprocess, ISOSDacInterface* sos_dac_interface,
                        std::vector<MtStat>& mtstat);