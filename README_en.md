# GcHeapStat [![Build status](https://ci.appveyor.com/api/projects/status/3pcm9r3rai06g891?svg=true)](https://ci.appveyor.com/project/alpinskiy/gcheapstat/build/artifacts) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/3b99c9352dc7495383808c7824c0b420)](https://www.codacy.com/manual/malpinskiy/gcheapstat?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=alpinskiy/gcheapstat&amp;utm_campaign=Badge_Grade)

Does the same as "!dumpheap -stat" WinDBG/SOS command but without connecting a debugger to the target process. Only ReadProcessMemory API is used to access target process internals. 
## Usage
```
GCHEAPSTAT [/VERSION] [/HELP] [/VERBOSE] [/SORT:{+|-}{SIZE|COUNT}[:gen]]
           [/LIMIT:count] [/GEN:gen] [/RUNAS:LOCALSYSTEM] /PID:pid

  HELP     Display usage information.
  VERSION  Display version.
  VERBOSE  Display warnings. Only errors are displayed by default.
  SORT     Sort output by either total SIZE or COUNT, ascending '+' or
           descending '-'. You can also specify generation to sort on (refer to
           GEN option description).
  LIMIT    Limit the number of rows to output.
  GEN      Count only objects of the generation specified. Valid values are
           0 to 2 (first, second the third generations respectevely) and 3 for
           Large Object Heap. The same is for gen parameter of SORT option.
  RUNAS    The only currently available value is LOCALSYSTEM (run under
           LocalSystem computer account). This is to allow inspection of managed
           services running under LocalSystem (administrator account is not
           powerful enough for that).
  PID      Target process ID.

Zero status code on success, non-zero otherwise.
```
GcHeapStat bitness should match the bitness of the target application. There is an AppVeyor artifact named gcheapstatN.zip (N is a build number). There you can find gcheapstat32.exe and gcheapstat64.exe for debugging x86 and x64 targets respectevily. Nothing bad will happen however if you try to inspect x64 target with x86 version of GcHeapStat (and vise versa). 
## Details
### Why not WinDBG/SOS?
Yes, you can use WinDBG (or any other debugger) for this purpose. It is just not well suited for inspection of applications working in production environment:
1. The debugger suspends execution of the target. The chances are slim you will be able to execute any of the debugger command so nobody will notice it. Even if you try to automate it.
1. If you close the debugger while debugging so will do the debuggee. There is a chance you can forget to detach debuggee. Or you might issue a command like "kill". All these will shutdown a target application. It is just danjerous to use debugger in production environment.
1. You can not accomplish a task with the debugger only if target application is running under LocalSystem account. Even an administrator account is not powefull enough to do that. This tool can do that out of the box by itself.
### How does it work?
It is possible to get GC heap details without suspending a target process due to the following:
1. New managed objects are always placed on the end of the heap (LOH might be an exception but not that many applications create huge memory traffic in the LOH heap segment).
1. Objects are moved around only during compacting phase of GC, which take relatevely small amount of time (Microsoft is actually strive to get garbage collection complete in a time interval needed to process an ordinary PageFault).

Yes, object values might change. Yes object header flags might change. But the following does not change:
1. Object's MethodTable (you can not change the type of object)
1. Objects's size (you can not change the size of object)

So, in terms of object types and sizes, it is safe to consider managed heap a read-only structure.

GcHeapStat inspects target process with the help of Data Access Layer (DAC) library, wich Microsoft ships with each version of CRL.
DAC provides an unified interface for accessing CLR details.
It resides in the same directory with CRL binary and is always available on the machine the managed application runs on.
DAC is used by debuggers. All you need to work with DAC is the ability to read target process memory. 
GcHeapStat opens target process with ```PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ``` therefor it can not hurt it anyway.
### Correctness
1. All inconsistesies are reported either as error or warnings (/VERBOSE options turns all them ON). I.e. it is verified that all heap segments reported by DAC contain valid objects. This in turn implies that all the object start addresses contain valid method table address. The method table address is used then to get an object details with the help of DAC. The chances are slim we can read a valid method table from a random address.
1. Comparing with WinDBG/SOS output. GcHeapStat outputs the same format as WinDBG/SOS do, so we can use any text diff application for comparition. GcHeapStat can run in parallel with the debugger - in this case the output should be identical.
1. I tryied hard.
