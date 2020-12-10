#pragma once
#include <winsdkver.h>
//
#include <comdef.h>
#include <corhdr.h>
#include <pathcch.h>
#include <shlwapi.h>
#include <strsafe.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>  // PRIX32
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <iterator>

#include "wil/com.h"
#include "wil/resource.h"
//
#include "cancellation.h"
#include "crosscomp.h"
#include "log.h"