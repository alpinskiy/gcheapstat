#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

#include "dac.h"

struct DacpGcHeapDetailsEx : DacpGcHeapDetails {
  int Generation(CLRDATA_ADDRESS address) const;
};

struct HeapSnapshot {
  bool Initialize(ISOSDacInterface* dac);

  struct AllocationContext {
    uintptr_t ptr;
    uintptr_t limit;
  };

  struct Segment {
    uintptr_t addr;
    DacpGcHeapDetailsEx* heap;
    DacpHeapSegmentData data;
  };

  DacpGcHeapData data{};
  DacpUsefulGlobalsData globals{};
  std::vector<DacpGcHeapDetailsEx> details{};
  std::array<std::vector<Segment>, 2> segments{};  // [0] - small & ephemeral
                                                   // [1] - large
  std::vector<AllocationContext> allocation_contexts{};
};

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

template <size_t Alignment>
uintptr_t Align(uintptr_t value) {
  auto constexpr align = Alignment - 1;
  return (value + align) & ~align;
}

static_assert(DAC_NUMBERGENERATIONS == 4, "4 generations expected!");

struct TypeStatistics {
  size_t base_size;
  size_t component_size;
  std::array<SIZE_T, DAC_NUMBERGENERATIONS + 1> count;
  std::array<SIZE_T, DAC_NUMBERGENERATIONS + 1> size_total;
};

struct TypeInformation {
  TypeInformation() = default;
  TypeInformation(UINT_PTR method_table_address,
                  const TypeStatistics& statistics)
      : method_table_address{method_table_address}, statistics{statistics} {}

  UINT_PTR method_table_address;
  std::string name;
  TypeStatistics statistics;
};

struct HeapStatistics {
  std::array<SIZE_T, DAC_NUMBERGENERATIONS + 1> count;
  std::array<SIZE_T, DAC_NUMBERGENERATIONS + 1> size_total;
  std::vector<TypeInformation> details;
};

class HeapStatisticsGenerator final {
 public:
  static bool Run(const Options& options, HeapStatistics& statistics) {
    return HeapStatisticsGenerator{options}.Run(statistics);
  }

 private:
  explicit HeapStatisticsGenerator(const Options& options)
      : options_{options}, dac_{CreateDac(options.pid)} {}
  bool Run(HeapStatistics& statistics);

#ifndef _WIN64
  template <size_t Alignment>
  void WalkSegment(CLRDATA_ADDRESS mem, CLRDATA_ADDRESS allocated,
                   DacpGcHeapDetailsEx* heap, int gen) {
    WalkSegment<Alignment>(static_cast<uintptr_t>(mem),
                           static_cast<uintptr_t>(allocated), heap, gen);
  }
#endif

  template <size_t Alignment>
  void WalkSegment(uintptr_t mem, uintptr_t allocated,
                   DacpGcHeapDetailsEx* heap, int gen) {
    if (allocated < mem) {
      Error() << "Invalid segment range encountered";
      return;
    }
    auto size = static_cast<size_t>(allocated - mem);
    if (!size) {
      Error() << "Empty segment encountered";
      return;
    }
    std::vector<BYTE> buffer(size);
    ULONG32 read = 0;
    auto hr = dac_->GetXCLRDataTarget3()->ReadVirtual(
        static_cast<CLRDATA_ADDRESS>(mem), &buffer[0], static_cast<ULONG>(size),
        &read);
    if (FAILED(hr)) {
      Error() << "Error reading segment memory"
              << ", code " << hr;
      return;
    }
    if (read != size) {
      Error() << "Incomplete segment memory read, bytes requested " << size
              << ", read " << read;
      return;
    }
    auto ptr = &buffer[0], end = &buffer[0] + size;
    auto allocation_context = std::find_if(
        heap_.allocation_contexts.cbegin(), heap_.allocation_contexts.cend(),
        [mem, allocated](auto& a) {
          return mem <= a.ptr && a.ptr < allocated;
        });
    for (; allocation_context != heap_.allocation_contexts.cend();
         ++allocation_context) {
      WalkMemory<Alignment>(
          mem, ptr, (std::min)(allocation_context->ptr, allocated) - mem, heap,
          gen);
      if (allocated < allocation_context->limit) {
        Debug() << "Allocation context limit " << allocation_context->limit
                << " goes " << allocation_context->limit - allocated
                << " bytes beyond segment boundary";
        return;
      }
      auto limit =
          allocation_context->limit + Align<kAlignment>(kMinObjectSize);
      if (allocated < limit) {
        Debug() << "Aligned allocation context limit " << limit << " goes "
                << limit - allocated << " bytes beyond segment boundary";
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
                  DacpGcHeapDetails* heap, int& gen) {
    for (size_t object_size; kMinObjectSize <= size;
         addr += object_size, ptr += object_size, size -= object_size) {
      if (gen && addr == heap->generation_table[gen - 1].allocation_start)
        --gen;
      // Get method table address
      auto mt = *reinterpret_cast<uintptr_t*>(ptr) & ~3;
      if (!mt) {
        auto end = ptr + size;
        for (; ptr < end && *ptr == 0; ++ptr)
          ;
        if (ptr == end) {
          if (!gen)
            Debug() << size
                    << "-byte tail of a gen#0 segment is all filled with zeros";
          else
            Error() << size << "-byte tail of a gen#" << gen
                    << " segment is all filled with zeros";
        } else
          Error() << "Zero method table address encountered, skip " << size
                  << " bytes of gen#" << gen;
        return;
      }
      // Get method table data
      auto it = statistics_.find(mt);
      if (it == statistics_.end()) {
        DacpMethodTableData mt_data{};
        auto hr = mt_data.Request(dac_->GetSOSDacInterface(), mt);
        if (FAILED(hr)) {
          Error() << "Error getting method table data, code " << hr << ", skip "
                  << size << " bytes of gen#" << gen;
          return;
        }
        TypeStatistics stat{mt_data.BaseSize, mt_data.ComponentSize};
        it = statistics_.emplace(mt, stat).first;
      }
      // Calculate object size
      auto& stat = it->second;
      auto component_count = *reinterpret_cast<PDWORD>(ptr + sizeof(uintptr_t));
      if (mt == heap_.globals.StringMethodTable) {
        // The component size on a String does not contain the trailing NULL
        // character, so we must add that ourselves.
        ++component_count;
      }
      object_size = stat.base_size + component_count * stat.component_size;
#if _WIN64
      if (object_size < kMinObjectSize) object_size = kMinObjectSize;
#endif
      // Validate object size
      if (!object_size || size < object_size) {
        Error() << "Object size " << object_size
                << " is out of valid range, skip " << size << " bytes of gen#"
                << gen;
        return;
      }
      // Update statistics
      ++stat.count[gen];
      ++stat.count[DAC_NUMBERGENERATIONS];
      stat.size_total[gen] += object_size;
      stat.size_total[DAC_NUMBERGENERATIONS] += object_size;
      // Align object size
      object_size = Align<Alignment>(object_size);
      if (!object_size || size < object_size) {
        Error() << "Aligned object size " << object_size
                << " is out of valid range, skip " << size << "bytes of gen#"
                << gen;
        return;
      }
    }
    if (size) Error() << "Skip " << size << " bytes of gen#" << gen;
  }

  template <template <class> class C>
  struct TypeInformationComparer final {
    explicit TypeInformationComparer(const Options& options)
        : ptr{options.orderby == OrderBy::Count ? &TypeStatistics::count
                                                : &TypeStatistics::size_total},
          gen{options.orderby_gen} {}

    bool operator()(TypeInformation& a, TypeInformation& b) {
      return cmp((a.statistics.*ptr)[gen], (b.statistics.*ptr)[gen]);
    }

    std::array<SIZE_T, DAC_NUMBERGENERATIONS + 1> TypeStatistics::*ptr;
    int gen;
    C<SIZE_T> cmp;
  };

  const Options& options_;
  std::unique_ptr<IDac> dac_;
  HeapSnapshot heap_;
  std::unordered_map<uintptr_t, TypeStatistics> statistics_;
};
