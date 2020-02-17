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
GcHeapStat возвращает код 0, если удалось выполнить задачу без ошибок и отличный от нуля код в противном случае.
## Технические детали

Целевой процесс открывается с правами "только чтение" (точнее ```PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ```), поэтому возможность ему навредить (намерено или по ошибке) исключена.

В реализации используются следующие моменты:
- Microsoft для каждой версии CLR поставляет Data Access Layer (DAC) библиотеку. Она предоставляет унифицированый интерфейс доступа к деталям рантайма управляемого процесса. DAC библиотека лежит в одной директории с рантаймом, поставляет вместе с ним, поэтому всегда доступна на машине, где выполняется .NET процесс. DAC используется в том числе отладчиком. Для получения DAC интерфейса практически достаточно только предоставить возможность читать память целевого процесса.

### Почему не WinDBG (или другой отладчик)?
Да, можно для этих целей использовать WinDBG (или любой другой отладчик). Но он не очень хорошо подходит для инспекции работающих на продакшене приложений:
1. Отладчик приостанавливает выполнение процесса. Подключиться отладчиком быстро, чтобы никто не заметил, скорее всего не выйдет - это занимает время даже если автоматизировать процесс.
1. Отладчик тянет за собой (убивает) процесс, если закрыть его в момент отладки (без отключения от отлаживаемого процесса). Если например забыть отсоединить отладчик перед выходом, или выполнить команду вроде ".kill", или ошибка в отладчике приведет к его краху, то отлаживаемый процесс также завершится. Это опасно.
1. Только одним отладчиком снять статистику с процесса, который работает под LocalSystem аккаунтом, нельзя. Даже у учетки администратора недостаточно прав для этого. Хочется же иметь возможность и в этом случае решить задачу быстро, без дополнительных инструментов (ОК, единственным инструментом).

### Как возможно получить информацию об внутреннем устройстве процесса без его остановки (контроля)?
Это возможно благодаря следующим особенностям управляемой кучи .NET:
1. Новые объекты добавлются всегда только в конец управляемой кучи (исключение составляет LOH, но большинство приложений не создают большой memory-traffic в LOH сегменте).
2. Расположение объектов в памяти меняется только в процессе Compact этапа работы GC, который занимает относительно немного (Microsoft стремится к тому, чтобы сборка мусора занимала не больше времени чем требует PageFault).

Соответсвенно большую часть времени жизни процесса кучу можно считать readonly структурой (кроме LOH и эфемерного поколения). 

### Какие доказательства корректности программы?
