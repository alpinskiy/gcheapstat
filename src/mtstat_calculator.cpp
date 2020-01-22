#include "mtstat_calculator.h"

MtStatCalculator::MtStatCalculator()
    : hprocess_{nullptr},
      sos_dac_interface_{nullptr},
      info_{},
      useful_globals_{} {}

HRESULT MtStatCalculator::Initialize(HANDLE hprocess,
                                     ISOSDacInterface* sos_dac_interface) {
  hprocess_ = hprocess;
  sos_dac_interface_ = sos_dac_interface;
  auto hr = info_.Request(sos_dac_interface_.get());
  if (FAILED(hr)) LogError(L"Error getting GCHeapData, code 0x%08lx\n", hr);
  hr = sos_dac_interface_->GetUsefulGlobals(&useful_globals_);
  if (FAILED(hr)) LogError(L"Error getting UsefulGlobals, code 0x%08lx\n", hr);
  // Get heaps
  if (info_.bServerMode) {
    std::vector<CLRDATA_ADDRESS> heap_addr_list(info_.HeapCount);
    unsigned int needed = 0;
    hr = sos_dac_interface_->GetGCHeapList(info_.HeapCount, &heap_addr_list[0],
                                           &needed);
    if (FAILED(hr)) {
      LogError(L"Error getting GCHeapList, code 0x%08lx\n", hr);
      return hr;
    }
    HRESULT lasterror = S_OK;
    heaps_.reserve(info_.HeapCount);
    for (auto addr : heap_addr_list) {
      DacpGcHeapDetails heap{};
      hr = heap.Request(sos_dac_interface_.get(), addr);
      if (FAILED(hr)) {
        LogError(L"Error getting GcHeapDetails at 0x%016" PRIX64
                 ", code 0x%08lx\n",
                 addr, hr);
        lasterror = hr;
      } else
        heaps_.push_back(heap);
    }
    if (heaps_.empty()) return lasterror;
  } else {
    heaps_.resize(1);
    DacpGcHeapDetails heap{};
    hr = heap.Request(sos_dac_interface_.get());
    if (FAILED(hr)) {
      LogError(L"Error getting GcHeapDetails, code 0x%08lx\n", hr);
      return hr;
    } else
      heaps_.push_back(heap);
  }
  // Get segments & gen0 allocation contexts
  for (size_t i = 0; i < heaps_.size(); ++i) {
    auto heap = &heaps_[i];
    if (heap->generation_table[0].allocContextPtr) {
      allocation_contexts_.push_back(
          {static_cast<uintptr_t>(heap->generation_table[0].allocContextPtr),
           static_cast<uintptr_t>(
               heap->generation_table[0].allocContextLimit)});
    }
    // Small & ephemeral
    auto addr = heap->generation_table[info_.g_max_generation].start_segment;
    for (; addr;) {
      Segment segment{static_cast<uintptr_t>(addr), heap};
      hr = segment.data.Request(sos_dac_interface_.get(), addr, *heap);
      if (FAILED(hr)) {
        LogError(L"Error getting SegmentData at 0x%016" PRIX64 "\n", addr);
        break;
      }
      segments_[0].push_back(segment);
      addr = segment.data.next;
    }
    // Large
    addr = heap->generation_table[info_.g_max_generation + 1].start_segment;
    for (; addr;) {
      Segment segment{static_cast<uintptr_t>(addr), heap};
      hr = segment.data.Request(sos_dac_interface_.get(), addr, *heap);
      if (FAILED(hr)) {
        LogError(L"Error getting SegmentData at 0x%016" PRIX64 "\n", addr);
        break;
      }
      segments_[1].push_back(segment);
      addr = segment.data.next;
    }
  }
  // Get thread allocation contexts
  DacpThreadStoreData threadstore_data{};
  hr = threadstore_data.Request(sos_dac_interface_.get());
  if (SUCCEEDED(hr)) {
    DacpThreadData thread_data{};
    for (auto thread = threadstore_data.firstThread; thread;
         thread = thread_data.nextThread) {
      hr = thread_data.Request(sos_dac_interface_.get(), thread);
      if (FAILED(hr)) {
        LogError(L"Error getting ThreadData at 0x%016" PRIX64
                 ", code 0x%08lx\n",
                 thread, hr);
      } else if (thread_data.allocContextPtr) {
        auto i = 0u;
        for (; i < allocation_contexts_.size() &&
               allocation_contexts_[i].ptr != thread_data.allocContextPtr;
             ++i)
          ;
        if (i == allocation_contexts_.size())
          allocation_contexts_.push_back(
              {static_cast<uintptr_t>(thread_data.allocContextPtr),
               static_cast<uintptr_t>(thread_data.allocContextLimit)});
      }
    }
  }
  // Verify allocation contexts
  for (auto it = allocation_contexts_.begin();
       it != allocation_contexts_.end();) {
    if (it->limit < it->ptr) {
      LogError(L"Invalid allocation context encountered\n");
      it = allocation_contexts_.erase(it);
    } else
      ++it;
  }
  // Sort allocation contexts
  std::sort(allocation_contexts_.begin(), allocation_contexts_.end(),
            [](auto& a, auto& b) { return a.ptr < b.ptr; });
  return hr;
}

HRESULT MtStatCalculator::Calculate(std::vector<MtStat>& mtstat) {
  std::unordered_map<uintptr_t, MtAddrStat>{}.swap(dict_);
  for (auto& segment : segments_[0]) {
    auto gen = GetGeneration(segment.data.mem, segment.heap);
    if (segment.addr == segment.heap->ephemeral_heap_segment)
      WalkSegment<kAlignment>(segment.data.mem, segment.heap->alloc_allocated,
                              segment.heap, gen);
    else
      WalkSegment<kAlignment>(segment.data.mem, segment.data.allocated,
                              segment.heap, gen);
    if (IsCancelled()) return S_FALSE;
  }
  for (auto& segment : segments_[1]) {
    WalkSegment<kAlignmentLarge>(segment.data.mem, segment.data.allocated,
                                 segment.heap, DAC_NUMBERGENERATIONS - 1);
    if (IsCancelled()) return S_FALSE;
  }
  std::vector<MtStat> ret;
  ret.reserve(dict_.size());
  std::transform(
      dict_.cbegin(), dict_.cend(), std::back_inserter(ret), [](auto& p) {
        auto& gen = p.second.gen;
        auto count = gen[0].count + gen[1].count + gen[2].count + gen[3].count;
        auto size_total = gen[0].size_total + gen[1].size_total +
                          gen[2].size_total + gen[3].size_total;
        return MtStat{p.first, {count, size_total}, gen[0], gen[1], gen[2],
                      gen[3]};
      });
  std::swap(ret, mtstat);
  return S_OK;
}

size_t MtStatCalculator::GetGeneration(CLRDATA_ADDRESS addr,
                                       DacpGcHeapDetails* heap) {
  size_t gen = 0;
  for (; gen < DAC_NUMBERGENERATIONS - 1 &&
         addr < heap->generation_table[gen].allocation_start;
       ++gen)
    ;
  if (gen == DAC_NUMBERGENERATIONS - 1) {
    gen = 0;
    LogError(
        L"Segment start address is out of valid range, generational data "
        L"will be messed up!\n");
  } else {
    // OK
  }
  return gen;
}

HRESULT CalculateMtStat(HANDLE hprocess, ISOSDacInterface* sos_dac_interface,
                        std::vector<MtStat>& mtstat) {
  MtStatCalculator impl;
  auto hr = impl.Initialize(hprocess, sos_dac_interface);
  if (SUCCEEDED(hr)) hr = impl.Calculate(mtstat);
  return hr;
}