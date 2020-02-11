#pragma once
#include "build.txt"

#define MAJOR 0
#define MINOR 1
#define PATCH 0
#ifndef BUILD
#define BUILD 42
#endif

#define QQ(x) #x
#define Q(x) QQ(x)
#define VERSION Q(MAJOR) "." Q(MINOR) "." Q(PATCH) "." Q(BUILD)

#ifndef TARGETNAME
#define TARGETNAME "gcheapstat"
#endif

#define PRODUCTNAME "GCHeapStat"
#define DESCRIPTION ".NET GC heap statistics generator"
#define COPYRIGHT "Copyright (C) 2020 Mikhail Alpinskiy"
