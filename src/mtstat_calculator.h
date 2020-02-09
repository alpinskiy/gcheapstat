#pragma once
#include "dacprivate.h"
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

static_assert(DAC_NUMBERGENERATIONS == 4, "4 generations expected!");

// printf format string PLaceHolder
#ifdef _WIN64
#define PLHxPTR "0x%016"##PRIxPTR
#else
#define PLHxPTR "0x%08"##PRIxPTR
#endif

class MtStatCalculator final {
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
    std::array<Stat, DAC_NUMBERGENERATIONS> gen;
  };

  size_t GetGeneration(CLRDATA_ADDRESS addr, DacpGcHeapDetails* heap);

#ifndef _WIN64
  template <size_t Alignment>
  inline void WalkSegment(CLRDATA_ADDRESS mem, CLRDATA_ADDRESS allocated,
                          DacpGcHeapDetails* heap, size_t gen) {
    WalkSegment<Alignment>(static_cast<uintptr_t>(mem),
                           static_cast<uintptr_t>(allocated), heap, gen);
  }
#endif

  template <size_t Alignment>
  void WalkSegment(uintptr_t mem, uintptr_t allocated, DacpGcHeapDetails* heap,
                   size_t gen) {
    if (allocated < mem) {
      LogError(L"Invalid segment range encountered\n");
      return;
    }
    auto size = static_cast<size_t>(allocated - mem);
    if (!size) {
      LogError(L"Empty segment encountered\n");
      return;
    }
    std::vector<BYTE> buffer(size);
    SIZE_T read = 0;
    if (!ReadProcessMemory(hprocess_, reinterpret_cast<LPCVOID>(mem),
                           &buffer[0], size, &read)) {
      auto hr = HRESULT_FROM_WIN32(GetLastError());
      LogError(L"Error 0x%08lx reading segment memory\n", hr);
      return;
    }
    if (read != size) {
      LogError(
          L"Incomplete segment memory read, bytes requested %zu, read %zu\n",
          size, read);
      return;
    }
    PBYTE ptr = &buffer[0], end = &buffer[0] + size;
    auto allocation_context =
        std::find_if(allocation_contexts_.cbegin(), allocation_contexts_.cend(),
                     [mem, allocated](auto& a) {
                       return mem <= a.ptr && a.ptr < allocated;
                     });
    for (; allocation_context != allocation_contexts_.cend();
         ++allocation_context) {
      WalkMemory<Alignment>(
          mem, ptr, (std::min)(allocation_context->ptr, allocated) - mem, heap,
          gen);
      if (allocated < allocation_context->limit) {
        LogDebug(L"Allocation context limit " PLHxPTR " goes %" PRIuPTR
                 " bytes beyond segment boundary\n",
                 allocation_context->limit,
                 allocation_context->limit - allocated);
        return;
      }
      auto limit =
          allocation_context->limit + Align<kAlignment>(kMinObjectSize);
      if (allocated < limit) {
        LogDebug(L"Aligned allocation context limit " PLHxPTR " goes %" PRIuPTR
                 " bytes beyond segment boundary\n",
                 limit, limit - allocated);
        return;
      }
      size = allocated - limit;
      ptr = end - size;
      mem = limit;
    }
    WalkMemory<Alignment>(mem, ptr, size, heap, gen);
  }

  template <size_t Alignment>
  void WalkMemory(uintptr_t addr, PBYTE ptr, size_t size,
                  DacpGcHeapDetails* heap, size_t& gen) {
    for (size_t object_size; kMinObjectSize <= size;
         addr += object_size, ptr += object_size, size -= object_size) {
      if (IsCancelled()) return;
      if (gen && addr == heap->generation_table[gen - 1].allocation_start)
        --gen;
      // Get method table address
      auto mt = *reinterpret_cast<uintptr_t*>(ptr) & ~3;
      if (!mt) {
        LogError(
            L"Zero method table address encountered, skip %zu bytes of "
            L"gen#%zu\n",
            size, gen);
        return;
      }
      // Get method table data
      auto it = dict_.find(mt);
      if (it == dict_.end()) {
        DacpMethodTableData mt_data{};
        auto hr = mt_data.Request(sos_dac_interface_.get(), mt);
        if (FAILED(hr)) {
          LogError(
              L"Error 0x%08lx getting method table data, skip %zu bytes of "
              L"gen#%zu\n",
              hr, size, gen);
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
            L"Object size %zu is out of valid range, skip %zu bytes of "
            L"gen#%zu\n",
            object_size, size, gen);
        return;
      }
      // Update statistics
      ++stat.gen[gen].count;
      stat.gen[gen].size_total += object_size;
      // Align object size
      object_size = Align<Alignment>(object_size);
      if (!object_size || size < object_size) {
        LogError(
            L"Aligned object size %zu is out of valid range, skip %zu "
            L"bytes of gen#%zu\n",
            object_size, size, gen);
        return;
      }
    }
    if (size) LogError(L"Skip %zu bytes of gen#%zu", size, gen);
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