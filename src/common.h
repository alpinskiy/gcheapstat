#pragma once

auto constexpr kPipeNameFormat = L"\\pipe\\gcheapstat%" PRIu32;
#ifdef _WIN64
#define PLHxPTR "0x016" PRIxPTR
#else
#define PLHxPTR "0x08" PRIxPTR
#endif
