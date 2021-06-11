#include "statistics.h"

#include <iterator>

int DacpGcHeapDetailsEx::Generation(CLRDATA_ADDRESS address) const {
  auto gen = 0;
  for (; gen < DAC_NUMBERGENERATIONS - 1 &&
         address < generation_table[gen].allocation_start;
       ++gen)
    ;
  if (gen == DAC_NUMBERGENERATIONS - 1) {
    gen = 0;
    Error() << "Segment start address is out of valid range, generational data will be messed up!";
  } else {
    // OK
  }
  return gen;
}

bool HeapSnapshot::Initialize(ISOSDacInterface* dac) {
  auto hr = data.Request(dac);
  if (FAILED(hr)) {
    Error() << "Error getting GCHeapData, code " << hr;
    return false;
  }
  hr = dac->GetUsefulGlobals(&globals);
  if (FAILED(hr)) {
    Error() << "Error getting UsefulGlobals, code " << hr;
    return false;
  }
  // Get heaps
  if (data.bServerMode) {
    std::vector<CLRDATA_ADDRESS> heap_addr_list(data.HeapCount);
    unsigned int needed = 0;
    hr = dac->GetGCHeapList(data.HeapCount, &heap_addr_list[0], &needed);
    if (FAILED(hr)) {
      Error() << "Error 0x" << std::hex << hr << " getting GCHeapList";
      return false;
    }
    auto lasterror = S_OK;
    details.reserve(data.HeapCount);
    for (auto addr : heap_addr_list) {
      DacpGcHeapDetailsEx heap{};
      hr = heap.Request(dac, addr);
      if (FAILED(hr)) {
        Error() << "Error getting GcHeapDetails at " << addr << ", code " << hr;
        lasterror = hr;
      } else {
        details.push_back(heap);
      }
    }
    if (details.empty()) {
      return SUCCEEDED(lasterror);
    }
  } else {
    details.resize(1);
    DacpGcHeapDetailsEx heap{};
    hr = heap.Request(dac);
    if (FAILED(hr)) {
      Error() << "Error getting GcHeapDetails, code " << hr;
      return false;
    }
    details.push_back(heap);
  }
  // Get segments & gen0 allocation contexts
  for (size_t i = 0; i < details.size(); ++i) {
    auto heap = &details[i];
    if (heap->generation_table[0].allocContextPtr) {
      allocation_contexts.push_back(
          {static_cast<uintptr_t>(heap->generation_table[0].allocContextPtr),
           static_cast<uintptr_t>(
               heap->generation_table[0].allocContextLimit)});
    }
    // Small & ephemeral
    auto addr = heap->generation_table[data.g_max_generation].start_segment;
    for (; addr;) {
      Segment segment{static_cast<uintptr_t>(addr), heap};
      hr = segment.data.Request(dac, addr, *heap);
      if (FAILED(hr)) {
        Error() << "Error getting SegmentData at " << addr << ", code " << hr;
        break;
      }
      segments[0].push_back(segment);
      addr = segment.data.next;
    }
    // Large
    addr = heap->generation_table[data.g_max_generation + 1].start_segment;
    for (; addr;) {
      Segment segment{static_cast<uintptr_t>(addr), heap};
      hr = segment.data.Request(dac, addr, *heap);
      if (FAILED(hr)) {
        Error() << "Error getting SegmentData at " << addr << ", code " << hr;
        break;
      }
      segments[1].push_back(segment);
      addr = segment.data.next;
    }
  }
  // Get thread allocation contexts
  DacpThreadStoreData threadstore_data{};
  hr = threadstore_data.Request(dac);
  if (SUCCEEDED(hr)) {
    DacpThreadData thread_data{};
    for (auto thread = threadstore_data.firstThread; thread;
         thread = thread_data.nextThread) {
      hr = thread_data.Request(dac, thread);
      if (FAILED(hr)) {
        Error() << "Error getting ThreadData at " << thread << ", code " << hr;
      } else if (thread_data.allocContextPtr) {
        auto i = 0u;
        for (; i < allocation_contexts.size() &&
               allocation_contexts[i].ptr != thread_data.allocContextPtr;
             ++i)
          ;
        if (i == allocation_contexts.size())
          allocation_contexts.push_back(
              {static_cast<uintptr_t>(thread_data.allocContextPtr),
               static_cast<uintptr_t>(thread_data.allocContextLimit)});
      }
    }
  }
  // Verify allocation contexts
  for (auto it = allocation_contexts.begin();
       it != allocation_contexts.end();) {
    if (it->limit < it->ptr) {
      Error() << "Invalid allocation context encountered";
      it = allocation_contexts.erase(it);
    } else
      ++it;
  }
  // Sort allocation contexts
  std::sort(allocation_contexts.begin(), allocation_contexts.end(),
            [](auto& a, auto& b) { return a.ptr < b.ptr; });
  return SUCCEEDED(hr);
}

bool HeapStatisticsGenerator::Run(HeapStatistics& statistics) {
  if (!dac_) {
    return false;
  }
  // Generate statistics
  auto hr = heap_.Initialize(dac_->GetSOSDacInterface());
  if (FAILED(hr)) {
    return false;
  }
  for (auto& segment : heap_.segments[0]) {
    auto gen = segment.heap->Generation(segment.data.mem);
    if (segment.addr == segment.heap->ephemeral_heap_segment)
      WalkSegment<kAlignment>(segment.data.mem, segment.heap->alloc_allocated,
                              segment.heap, gen);
    else
      WalkSegment<kAlignment>(segment.data.mem, segment.data.allocated,
                              segment.heap, gen);
  }
  for (auto& segment : heap_.segments[1]) {
    WalkSegment<kAlignmentLarge>(segment.data.mem, segment.data.allocated,
                                 segment.heap, DAC_NUMBERGENERATIONS - 1);
  }
  // To array
  statistics.details.reserve(statistics_.size());
  std::transform(statistics_.cbegin(), statistics_.cend(),
                 std::back_inserter(statistics.details), [this](auto& p) {
                   return TypeInformation{p.first, p.second};
                 });
  // Sort and limit
  if (options_.order == Order::Asc) {
    std::sort(statistics.details.begin(), statistics.details.end(),
              TypeInformationComparer<std::less>{options_});
  } else {
    std::sort(statistics.details.begin(), statistics.details.end(),
              TypeInformationComparer<std::greater>{options_});
  }
  statistics.details.resize(
      (std::min)(statistics.details.size(), options_.limit));
  // Get names, count totals
  TypeNameProvider nameof{dac_.get()};
  for (auto& item : statistics.details) {
    item.name = nameof(item.method_table_address);
    statistics.count[DAC_NUMBERGENERATIONS] +=
        item.statistics.count[DAC_NUMBERGENERATIONS];
    statistics.size_total[DAC_NUMBERGENERATIONS] +=
        item.statistics.size_total[DAC_NUMBERGENERATIONS];
  }
  return S_OK;
}
