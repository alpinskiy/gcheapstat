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
  info_.Request(sos_dac_interface_.get());
  sos_dac_interface_->GetUsefulGlobals(&useful_globals_);
  //  Allocation contexts
  DacpThreadStoreData threadstore_data{};
  auto hr = threadstore_data.Request(sos_dac_interface_.get());
  if (SUCCEEDED(hr)) {
    DacpThreadData thread_data{};
    allocation_contexts_.reserve(threadstore_data.threadCount);
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
  // Heaps
  if (info_.bServerMode) {
    std::vector<CLRDATA_ADDRESS> heap_addr_list(info_.HeapCount);
    unsigned int needed = 0;
    hr = sos_dac_interface_->GetGCHeapList(info_.HeapCount, &heap_addr_list[0],
                                           &needed);
    if (FAILED(hr)) {
      LogError(L"Error getting GCHeapList\n");
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
  // Segments
  for (size_t i = 0; i < heaps_.size(); ++i) {
    auto heap = &heaps_[i];
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
  return hr;
}

HRESULT MtStatCalculator::Calculate(std::vector<MtStat>& mtstat) {
  std::unordered_map<uintptr_t, MtAddrStat>{}.swap(dict_);
  for (auto& segment : segments_[0]) {
    if (segment.addr == segment.heap->ephemeral_heap_segment)
      WalkEphemeralHeapSegment(segment);
    else
      WalkSmallObjectHeapSegment(segment);
    if (IsCancelled()) return S_FALSE;
  }
  for (auto& segment : segments_[1]) {
    WalkLargeObjectHeapSegment(segment);
    if (IsCancelled()) return S_FALSE;
  }
  std::vector<MtStat> ret;
  ret.reserve(dict_.size());
  std::transform(dict_.cbegin(), dict_.cend(), std::back_inserter(ret),
                 [](auto& p) {
                   MtStat item;
                   item.addr = p.first;
                   item.count = p.second.count;
                   item.size_total = p.second.size_total;
                   return item;
                 });
  std::swap(ret, mtstat);
  return S_OK;
}

HRESULT MtStatCalculator::WalkSmallObjectHeapSegment(Segment& segment) {
  auto segment_first = static_cast<uintptr_t>(segment.data.mem);
  auto segment_last = static_cast<uintptr_t>(segment.data.allocated);
  if (segment_last < segment_first) {
    LogError(L"Invalid segment range encountered\n");
    return E_FAIL;
  }
  auto segment_size = static_cast<size_t>(segment_last - segment_first);
  if (!segment_size) {
    LogError(L"Empty segment encountered\n");
    return S_FALSE;
  }
  std::vector<BYTE> buffer(segment_size);
  SIZE_T read = 0;
  if (!ReadProcessMemory(hprocess_, reinterpret_cast<LPCVOID>(segment_first),
                         &buffer[0], segment_size, &read))
    return HRESULT_FROM_WIN32(GetLastError()); // todo
  if (read != segment_size) {
    LogError(L"Error reading segment memory (requested %zu, read %zu bytes)\n",
             segment_size, read);
    return E_FAIL;
  }
  auto buffer_first = &buffer[0];
  auto buffer_ptr = buffer_first;
  auto buffer_bytes_left = segment_size;
  for (; kMinObjectSize <= buffer_bytes_left;) {
    if (IsCancelled()) return S_FALSE;
    uintptr_t mt;
    if (!ReadMethodTableAddress(buffer_ptr, buffer_bytes_left, mt)) {
      LogError(
          L"Zero method table address encountered (small object heap, %zu "
          L"bytes left)\n",
          buffer_bytes_left);
      return E_FAIL;
    }
    auto hr = ProcessObject<kAlignment>(mt, buffer_ptr, buffer_bytes_left,
                                        L"small object heap");
    if (FAILED(hr)) return hr;
  }
  return buffer_bytes_left ? E_FAIL : S_OK;
}

HRESULT MtStatCalculator::WalkEphemeralHeapSegment(Segment& segment) {
  auto segment_first = static_cast<uintptr_t>(segment.data.mem);
  auto segment_last = static_cast<uintptr_t>(segment.heap->alloc_allocated);
  if (segment_last < segment_first) {
    LogError(L"Invalid segment range encountered\n");
    return E_FAIL;
  }
  auto segment_size = static_cast<size_t>(segment_last - segment_first);
  if (!segment_size) {
    LogError(L"Empty segment encountered\n");
    return S_FALSE;
  }
  std::vector<BYTE> buffer(segment_size);
  SIZE_T read = 0;
  if (!ReadProcessMemory(hprocess_, reinterpret_cast<LPCVOID>(segment_first),
                         &buffer[0], segment_size, &read))
    return HRESULT_FROM_WIN32(GetLastError()); // todo
  if (read != segment_size) {
    LogError(L"Error reading segment memory (requested %zu, read %zu bytes)\n",
             segment_size, read);
    return E_FAIL;
  }
  auto buffer_first = &buffer[0];
  auto buffer_last = buffer_first + segment_size;
  auto buffer_ptr = buffer_first;
  auto buffer_bytes_left = segment_size;
  for (; kMinObjectSize <= buffer_bytes_left;) {
    if (IsCancelled()) return S_FALSE;
    auto segment_ptr = static_cast<uintptr_t>(
        segment_first + std::distance(&buffer[0], buffer_ptr));
    uintptr_t mt;
    if (!ReadMethodTableAddress(buffer_ptr, buffer_bytes_left, mt)) {
      // Is this the beginning of an allocation context?
      PBYTE buffer_ptr_new;
      if (SkipAllocationContext<kAlignment>(segment.heap, segment_ptr) &&
          SegmentPtrToBufferPtr(segment_ptr, segment_first, segment_last,
                                buffer_first, buffer_ptr_new)) {
        _ASSERT(buffer_ptr < buffer_ptr_new);
        _ASSERT(buffer_ptr_new <= buffer_last);
        buffer_ptr = buffer_ptr_new;
        buffer_bytes_left = std::distance(buffer_ptr, buffer_last);
        continue;
      } else {
        LogError(
            L"Zero method table address encountered (ephemeral segment, %zu "
            L"bytes left)\n",
            buffer_bytes_left);
        return E_FAIL;
      }
    }
    auto hr = ProcessObject<kAlignment>(mt, buffer_ptr, buffer_bytes_left,
                                        L"ephemeral segment");
    if (FAILED(hr)) return hr;
  }
  return buffer_bytes_left ? E_FAIL : S_OK;
}

HRESULT MtStatCalculator::WalkLargeObjectHeapSegment(Segment& segment) {
  auto segment_first = static_cast<uintptr_t>(segment.data.mem);
  auto segment_last = static_cast<uintptr_t>(segment.data.allocated);
  if (segment_last < segment_first) {
    LogError(L"Invalid segment range encountered\n");
    return E_FAIL;
  }
  auto segment_size = static_cast<size_t>(segment_last - segment_first);
  if (!segment_size) {
    LogError(L"Empty segment encountered\n");
    return S_FALSE;
  }
  std::vector<BYTE> buffer(segment_size);
  SIZE_T read = 0;
  if (!ReadProcessMemory(hprocess_, reinterpret_cast<LPCVOID>(segment_first),
                         &buffer[0], segment_size, &read)) // todo
    return HRESULT_FROM_WIN32(GetLastError());
  if (read != segment_size) {
    LogError(L"Error reading segment memory (requested %zu, read %zu bytes)\n",
             segment_size, read);
    return E_FAIL;
  }
  auto buffer_first = &buffer[0];
  auto buffer_ptr = buffer_first;
  auto buffer_bytes_left = segment_size;
  for (; kMinObjectSize <= buffer_bytes_left;) {
    if (IsCancelled()) return S_FALSE;
    uintptr_t mt;
    if (!ReadMethodTableAddress(buffer_ptr, buffer_bytes_left, mt)) {
      LogError(
          L"Zero method table address encountered (large object heap, %zu "
          L"bytes left)\n",
          buffer_bytes_left);
      return E_FAIL;
    }
    auto hr = ProcessObject<kAlignmentLarge>(mt, buffer_ptr, buffer_bytes_left,
                                             L"large object heap");
    if (FAILED(hr)) return hr;
  }
  return buffer_bytes_left ? E_FAIL : S_OK;
}

HRESULT CalculateMtStat(HANDLE hprocess, ISOSDacInterface* sos_dac_interface,
                        std::vector<MtStat>& mtstat) {
  MtStatCalculator impl;
  auto hr = impl.Initialize(hprocess, sos_dac_interface);
  if (SUCCEEDED(hr)) hr = impl.Calculate(mtstat);
  return hr;
}
