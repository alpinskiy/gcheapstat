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

  HRESULT WalkSmallObjectHeapSegment(Segment& segment);
  HRESULT WalkEphemeralHeapSegment(Segment& segment);
  HRESULT WalkLargeObjectHeapSegment(Segment& segment);

  template <size_t Alignment>
  inline uintptr_t Align(uintptr_t v) {
    auto constexpr kAlign = Alignment - 1;
    return (v + kAlign) & ~kAlign;
  }

  inline HRESULT GetMtAddrStat(uintptr_t mt, MtAddrStat** ptr) {
    auto it = dict_.find(mt);
    if (it != dict_.end()) {
      *ptr = &it->second;
    } else {
      DacpMethodTableData mt_data{};
      auto hr = mt_data.Request(sos_dac_interface_.get(), mt);
      if (FAILED(hr)) return hr;
      MtAddrStat stat{mt_data.BaseSize, mt_data.ComponentSize};
      *ptr = &dict_.emplace(mt, stat).first->second;
    }
    return S_OK;
  }

  inline size_t UpdateMtAddrStat(uintptr_t mt, DWORD component_count,
                                 MtAddrStat* stat) {
    // Calculate object size
    if (mt == useful_globals_.StringMethodTable) {
      // The component size on a String does not contain the trailing NULL
      // character, so we must add that ourselves.
      ++component_count;
    }
    auto object_size =
        stat->sizeof_base + component_count * stat->sizeof_component;
#if _WIN64
    if (object_size < kMinObjectSize) object_size = kMinObjectSize;
#endif
    // Update statistics
    ++stat->count;
    stat->size_total += object_size;
    return object_size;
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