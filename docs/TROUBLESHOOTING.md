# Диагностика и настройка Screw

## 1. Начни с проверки системы

```
screw doctor
```

Проверяет процессор и ядро, binfmt, диск `C:`, дисплей X11, видеокарту, Vulkan-драйверы
и вспомогательные утилиты. Для каждой проблемы печатается строка `fix:` с готовой командой.
Код выхода `1` значит, что найдена ошибка (`FAIL`), `0` — всё работает.

## 2. Логи

| Что нужно                       | Команда                                     |
|---------------------------------|---------------------------------------------|
| Подробный журнал в терминал     | `SCREW_TRACE=1 screw app.exe`               |
| Журнал в файл                   | `SCREW_LOG=screw.log screw app.exe`         |
| Какие функции программа требует | `screw info app.exe`                        |

В журнале видно: где загружен каждый модуль, какие DLL подключены, чего не хватает
и какие `OutputDebugString` выводит сама программа. Каждая строка содержит время от старта
и номер потока.

## 3. Частые сообщения

**`screw: missing KERNEL32.dll!SomeFunction`**
Программа вызвала функцию, которой в Screw ещё нет. Она остановлена в момент вызова,
а не раньше. `screw info app.exe` покажет все такие функции заранее.
`SCREW_LENIENT=1` заставит недостающие функции возвращать 0. Иногда этого хватает,
но поведение программы может стать неверным.

**`app.exe needs foo.dll, which is neither built in nor next to the program`**
Положи `foo.dll` рядом с `.exe` или укажи папку: `SCREW_DLL_PATH=/путь/к/dll screw app.exe`.
Несколько папок разделяются `:`.

**`only 64-bit x86 programs are supported`**
Это 32-битная программа. Пока Screw запускает только 64-битные `.exe`.

**`cannot install thread block in %gs`**
Ядро запретило `arch_prctl`. Обычно это контейнер с жёстким seccomp: запусти вне его.

## 4. Если программа упала

Screw печатает отчёт:

```
screw: the program crashed: access violation (SIGSEGV)
  at     000000014000149c  crash.exe+0x149c
  memory 0000000000000000  (null pointer)
  likely callers:
  from   00000001400014e1  crash.exe+0x14e1
```

`crash.exe+0x149c` — смещение (RVA) внутри файла. Его можно найти в дизассемблере:

```
x86_64-w64-mingw32-objdump -d --start-address=0x14000149c app.exe | head
```

Если падение `in built-in kernel32.dll`, ошибка в Screw: пришли отчёт и журнал `SCREW_LOG`.

## 5. Видеодрайверы и Vulkan

Для игр через DXVK нужен Vulkan 1.3 или новее на настоящей видеокарте.

| Видеокарта     | Что поставить                                                     |
|----------------|-------------------------------------------------------------------|
| Intel, AMD     | `sudo apt install mesa-vulkan-drivers libvulkan1 vulkan-tools`    |
| NVIDIA         | `sudo apt install nvidia-driver nvidia-vulkan-icd libvulkan1`, затем перезагрузка |
| Виртуалка      | включи 3D-ускорение в настройках VM; иначе будет только llvmpipe  |

Проверка: `screw doctor` (раздел Vulkan) или `vulkaninfo --summary`.
Если видна только `llvmpipe` — работает программный рендер, игры будут очень медленными.
Мост `vulkan-1.dll` пишет в журнал (`SCREW_TRACE=1`) включённые расширения и результат
`vkCreateInstance`/`vkCreateDevice`. Другой загрузчик Vulkan можно задать через
`SCREW_VULKAN=/путь/libvulkan.so.1`. Слои проверки Linux работают как обычно:
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation screw app.exe`.

На ноутбуке с двумя видеокартами NVIDIA можно выбрать так:
`__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia screw game.exe`.

## 6. Запуск двойным кликом и `./app.exe`

```
sudo screw register
```

Регистрирует Screw в `binfmt_misc` и сохраняет правило в `/etc/binfmt.d/screw.conf`,
поэтому оно переживает перезагрузку. Отменить: `sudo screw unregister`.
Если `binfmt_misc` не смонтирован:
`sudo mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc`.

## 7. Диски и файлы

| Windows  | Linux                                   |
|----------|-----------------------------------------|
| `C:\`    | `~/.screw/c` (или папка из `SCREW_C`)   |
| `Z:\`    | `/`                                     |
| `D:\`…   | папка из `SCREW_DRIVE_D` и т. д.        |

Регистр букв в путях не важен: `Data\Config.INI` найдёт `data/config.ini`.
Реестр Windows хранится обычными папками и файлами внутри `C:`, его можно править руками.

## 8. Окна и ввод

Окна создаются через X11. На Wayland нужен XWayland (он включён почти во всех окружениях).
Обработка клавиатуры и мыши внутри окон пока не реализована: окно открывается, двигается,
закрывается и получает фокус, но нажатия клавиш и кнопок мыши программа ещё не получает.

## 9. Как сообщить об ошибке

Приложи:

1. вывод `screw doctor`;
2. файл из `SCREW_LOG=screw.log screw app.exe`;
3. вывод `screw info app.exe`;
4. отчёт о падении, если он был.
