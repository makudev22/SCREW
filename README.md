# SCREW

SCREW you, Microsoft Executable!

Screw runs Windows x86-64 `.exe` files on Linux natively. There is no emulator,
no interpreter, no background server and no fake Windows prefix: the program's
machine code runs directly on your CPU inside a single Linux process, and every
Windows API call lands straight in a native Linux implementation.

```
./hello.exe          # after `screw register`, just like a Linux binary
screw hello.exe      # or explicitly
```

## How it works

```
 hello.exe ──► loader ──► maps PE image at its preferred base, applies relocations
                 │
                 ├──► imports ──► bits (built-in DLLs written for Linux)
                 │          └──► native .dll files shipped next to the .exe
                 │
                 ├──► runtime ──► TEB/PEB on %gs, handles, paths, UTF-16
                 │
                 └──► jump to entry point — the CPU runs the program at full speed
```

| Piece      | Role                                                                    |
|------------|-------------------------------------------------------------------------|
| `loader`   | Parses PE32+, maps sections, relocations, imports, exports, TLS          |
| `runtime`  | Thread environment block via `%gs`, handle table, path and text mapping  |
| `bits`     | Windows DLLs re-implemented as thin `ms_abi` shims over Linux and libc   |
| `cli`      | `screw` command, binfmt registration for double-click / `./app.exe`      |

### What makes it different

* **One process, zero daemons.** No wineserver; Windows handles are plain
  Linux objects (fds, pthreads, mutexes) behind a small handle table.
* **No prefix.** No registry hives, no `drive_c` copy of Windows. Paths map
  directly: `C:\` is `~/.screw/c`, `Z:\` is `/`, and relative paths stay relative.
* **Bits, not DLL emulation.** Each Windows DLL is a *bit*: a table of
  functions compiled with the Windows calling convention that call Linux directly.
* **Native DLLs welcome.** If an app ships its own `.dll`, Screw loads the real
  PE file and links it like the main executable.
* **Honest failures.** Calling an API that is not implemented yet stops with
  `screw: missing KERNEL32.dll!Foo` instead of mysterious misbehaviour.
  Set `SCREW_LENIENT=1` to make missing calls return 0 instead.

## Build

Requirements: `gcc`, `make`. For tests: `x86_64-w64-mingw32-gcc`.

```
make
sudo make install
make test
```

## Use

```
screw program.exe [args...]
screw info program.exe        # show imports and which ones are supported
sudo screw register           # run .exe files directly via binfmt_misc
sudo screw unregister
```

Environment:

| Variable          | Meaning                                    |
|-------------------|--------------------------------------------|
| `SCREW_TRACE=1`   | log loading and resolved imports           |
| `SCREW_LENIENT=1` | missing APIs return 0 instead of aborting  |
| `SCREW_C`         | host directory used as `C:\`               |

## Status

Works today: console programs built with MinGW or MSVC that use the C runtime
(`msvcrt`) and core `kernel32` (files, memory, threads, sync, time, environment),
plus `MessageBox` through the desktop notifier. Full GUI (`CreateWindow`) and
DirectX are not implemented yet.

## Layout

```
include/screw/   public headers: Windows types, PE format, modules, runtime
src/core/        logging and helpers
src/loader/      PE mapping, relocations, imports, exports, TLS
src/runtime/     TEB/PEB, handles, paths, unicode, process state
src/bits/        built-in DLLs: kernel32, msvcrt, ntdll, user32, advapi32, shell32
src/cli/         the screw command and binfmt integration
tests/samples/   Windows test programs compiled with MinGW
```
