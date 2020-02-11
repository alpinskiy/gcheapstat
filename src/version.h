#pragma once
#include "build.txt"

#define MAJOR 0
#define MINOR 1
#ifndef BUILD
#define BUILD 0
#endif

#define QQ(x) #x
#define Q(x) QQ(x)

#define PRODUCTNAME "GCHeapStat"
#define DESCRIPTION ".NET GC heap statistics generator"
#define COPYRIGHT "Copyright (C) 2020 Mikhail Alpinskiy"
#define VERSION Q(MAJOR) "." Q(MINOR) "." Q(BUILD)
#define FILENAME Q(TARGETNAME) ".exe"
