#pragma once
#include "../build_number.txt"

#define MAJOR 0
#ifndef BUILD
#define BUILD 0
#endif

#define QQ(x) #x
#define Q(x) QQ(x)

#define PRODUCTNAME "gcheapstat"
#define DESCRIPTION ".NET GC heap statistics generator"
#define VERSION MAJOR,BUILD
#define VERSION_STR Q(MAJOR) "." Q(BUILD)
