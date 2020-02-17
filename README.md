# GcHeapStat [![Build status](https://ci.appveyor.com/api/projects/status/3pcm9r3rai06g891?svg=true)](https://ci.appveyor.com/project/alpinskiy/gcheapstat/build/artifacts) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/3b99c9352dc7495383808c7824c0b420)](https://www.codacy.com/manual/malpinskiy/gcheapstat?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=alpinskiy/gcheapstat&amp;utm_campaign=Badge_Grade)

GcHeapStat с помощью только чтения адресного пространства целевого процесса, перебирает его управляемую кучу и собирает статистику в формате WinDBG/SOS команды "!dumpheap -stat".

## Использование
```
GCHEAPSTAT [/VERSION] [/HELP] [/VERBOSE] [/SORT:{+|-}{SIZE|COUNT}[:gen]]
           [/LIMIT:count] [/GEN:gen] [/RUNAS:LOCALSYSTEM] /PID:pid

  HELP     Display usage information.
  VERSION  Display version.
  VERBOSE  Display warnings. Only errors are displayed by default.
  SORT     Sort output by either total SIZE or COUNT, ascending '+' or
           descending '-'. You can also specify generation to sort on.
  LIMIT    Limit the number of rows to output.
  GEN      Count only objects of the generation specified.
  RUNAS    The only currently available value is LOCALSYSTEM (run under
           LocalSystem computer account). This is to allow inspection of managed
           services running under LocalSystem (administrator account is not powerful enough for that).
  PID      Target process ID.
```
## Технические детали

Целевой процесс открывается с правами "только чтение", поэтому возможность ему навредить (намерено или по ошибке) исключена.

В реализации используются следующие моменты:
- Microsoft для каждой версии CLR поставляет Data Access Layer (DAC) библиотеку. Она предоставляет унифицированый интерфейс доступа к деталям рантайма управляемого процесса. DAC библиотека лежит в одной директории с рантаймом, поставляет вместе с ним, поэтому всегда доступна на машине, где выполняется .NET процесс. DAC используется в том числе отладчиком. Для получения DAC интерфейса практически достаточно только предоставить возможность читать память целевого процесса.
- Новые объекты добавлются всегда только в конец управляемой кучи (за исключением LOH)
- Расположение объектов в памяти меняется только при работе GC, который занимает относительно немного (Microsoft стремится к тому, чтобы сборка мусора занимала не больше чем требует PageFault)

Соответсвенно большую часть времени жизни процесса кучу можно считать readonly структурой (кроме LOH и эфемерного поколения). Это позволяет считать статистику по объектам без остановки .NET процесса.

Да, вроде то же самое можно сделать с помощью NonInvasive режима отладчика, но
1. Даже NonInvasive отладчик тянет за собой (убивает) процесс, если закрыть (или убить отладчик) отладчик в момент, когда он подключен к процессу. Поэтому подключать даже NonInvasive отладчик к важному процессу потенциально опасно (кто-то забудет отключить отладчик или ошибка в отладчике приведет к его краху).
2. Только одним отладчиком снять статистику с процесса, которые работает под LocalSystem аккаунтом, нельзя (даже у учетки администратора недостаточно прав для этого). Хочется иметь возможность и в этом случае решить задачу быстро, без дополнительных инструментов (ОК, единственным инструментом).

GcHeapStat возвращает код 0, если удалось выполнить задачу без ошибок и отличный от нуля код в противном случае.
