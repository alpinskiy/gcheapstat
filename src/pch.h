#pragma once

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "wil/com.h"
#include "wil/resource.h"
#else
#include <dlfcn.h>  // dlopen, dlclose

#include "pal.h"
#include "palrt.h"
#endif
#include <corhdr.h>     // required by xclrdata.h
#include <crosscomp.h>  // required by dacprivate.h
#include <dacprivate.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "log.h"
#include "options.h"
