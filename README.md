# tracy-vxworks

A port of the [Tracy profiler](https://github.com/wolfpld/tracy) client to
**VxWorks 7**, tested with the free VxWorks SDK (26.09,
`wrsdk-vxworks7-qemu-1.18.0`) and its QEMU kernel.

Status:

| | |
|---|---|
| Tracy client in an **RTP** (user-space process) | works: zones, frames, plots, messages, locks, thread names, host info |
| Trace capture from the Linux host (`tracy-capture` through QEMU port forwarding) | works |
| Tracy client in a **DKM** (kernel module) | compiles and links; does **not load** in the SDK kernel (no TLS for kernel modules, see below) |
| Call stacks, sampling, context switches, system tracing, crash handler | not available on VxWorks (not compiled in) |

The patch to Tracy is small: 75 added lines in 3 files, all inside
`#if defined __VXWORKS__` blocks ([patches/tracy-vxworks.patch](patches/tracy-vxworks.patch),
against the `v0.15.0` tag).

## Quick start

Requirements: the VxWorks SDK, CMake ≥ 3.25, Ninja, Python 3,
`qemu-system-x86_64`, git, and a host C++20 compiler for Tracy's tools
(GCC 11 works).

```sh
export WIND_HOME=/path/to/wrsdk-vxworks7-qemu-1.18.0   # no need to source sdkenv.sh
scripts/run_demo.sh
```

`run_demo.sh` does everything:

1. `scripts/fetch_tracy.sh`: clones Tracy `v0.15.0` into `./tracy` and applies
   `patches/tracy-vxworks.patch` (skipped if `./tracy` exists);
2. builds the Tracy client and the demo as a static VxWorks RTP
   (`build/vx/tracy_demo.vxe`);
3. builds Tracy's `capture` and `csvexport` and the small `tracy-trace-info`
   checker (`tools/trace_info/`) for the host;
4. `tools/run_tracy_demo.py`: boots the SDK kernel in QEMU with the demo on a
   FAT disk (`/ata0a`) and host port 18086 forwarded to the guest's Tracy
   port 8086, runs the demo with `rtp exec`, connects `tracy-capture` and
   saves `build/trace/tracy_demo.tracy`, then stops QEMU;
5. prints the zone statistics with `tracy-csvexport` and checks the trace with
   `tracy-trace-info`.

Options (environment variables): `HOST_PORT` (default 18086), `FRAMES`
(default 200), `KVM=1` (boot with `-enable-kvm -cpu host,+invtsc`; default is
TCG, which needs no KVM).

The end of a successful run:

```
-- trace-info
Program:   tracy_demo.vxe
Host info:
OS: VxWorks (SMP 64-bit, Core Kernel version: 3.2.4.4)
Compiler: clang 21.1.8
User: @vxTarget
Arch: x64
CPU: QEMU Virtual CPU version 2.5+
CPU cores: 2
RAM: unknown

Threads:
  tid 65536    Main thread          zones 400
  tid 65600    worker 0             zones 1005
  tid 65606    worker 1             zones 1000
  tid 65613    worker 2             zones 1000
Frames:    202
Plots:
  result               200 points
  frame                200 points
Messages:
  tracy_demo starting
  frame 0
  ...
Checks:
  [ok] trace has zones
  [ok] main thread has zones
  [ok] 3 named worker threads have zones
  [ok] frame count >= expected
  [ok] frame timestamps increase
  [ok] user plots 'result' and 'frame' present
  [ok] plot timestamps increase
  [ok] messages present
  [ok] message timestamps increase
  [ok] program name is tracy_demo.vxe
  [ok] host info reports VxWorks
TRACE CHECK PASSED
```

The `.tracy` file opens in the normal Tracy GUI (`profiler/`) on any host.

## Doing it by hand

```sh
# VxWorks RTP build (toolchain from vxworks-cpp-template)
cmake -G Ninja -B build/vx -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/vxworks.cmake
cmake --build build/vx

# Boot the SDK kernel with the demo on /ata0a and port 8086 forwarded
mkdir -p /tmp/vxdisk && cp build/vx/tracy_demo.vxe /tmp/vxdisk/
qemu-system-x86_64 -m 2048M -smp 2 -snapshot \
  -kernel $WIND_HOME/vxsdk/bsps/itl_generic_3_1_0_0/vxWorks \
  -drive file=fat:/tmp/vxdisk,format=raw,if=ide \
  -nic user,hostfwd=tcp:127.0.0.1:18086-:8086 \
  -display none -serial stdio -monitor none \
  -append "bootline:fs(0,0)host:vxWorks h=10.0.2.2 e=10.0.2.15 u=target pw=vxTarget o=gei0"

# in the VxWorks shell
-> cmd
[vxWorks *]# rtp exec -e TRACY_NO_INVARIANT_CHECK=1 /ata0a/tracy_demo.vxe 200

# on the host
build/host-capture/tracy-capture -a 127.0.0.1 -p 18086 -o demo.tracy
```

`TRACY_NO_INVARIANT_CHECK=1` is only needed under TCG, see [Timer](#timer).

## Using it in your own project

Build Tracy's own `CMakeLists.txt` with the VxWorks toolchain and link
`Tracy::TracyClient`, as [CMakeLists.txt](CMakeLists.txt) does. Only
`TRACY_ENABLE` is required. No `TRACY_NO_*` option is needed: the features
VxWorks lacks are not compiled in for it anyway. `TRACY_NO_EXIT` is useful
for short programs: the process then waits after `main()` until a server has
received all data. Tracy's network defaults (IPv6 dual-stack listen socket,
UDP discovery broadcast) can stay on: a run with them captures fine.

## Changes to Tracy

All changes are in `#if defined __VXWORKS__` blocks (`__VXWORKS__` is
predefined by `wr-cc`/`wr-c++`; `__RTP__` is predefined for RTPs and
`_WRS_KERNEL` for kernel code). Other platforms compile exactly as before.

### `public/common/TracySystem.cpp`

| Change | Why |
|---|---|
| `GetThreadHandleImpl()`: return `taskIdSelf()` | The `#if` chain ended in `#error "Unsupported platform!"`. In an RTP, `TASK_ID` is a 32-bit object handle that identifies the task, which is what Tracy's 32-bit thread id needs. In the kernel it is a 64-bit TCB address and is truncated (see DKM). |
| `SetThreadNameWithHint()`: call `pthread_setname_np()` (RTP) | VxWorks has it, and it renames the VxWorks task, so Tracy's thread names also show up in the shell (`i`, `ps`). The `_GNU_SOURCE` branch is not taken because VxWorks does not define `_GNU_SOURCE`. |
| `GetThreadName()`: fall back to `taskName(id)` (RTP) | Threads that never called `SetThreadName()` still get their VxWorks task name (for example `iTracy_demo`) instead of a number. Not in the kernel, where the id is a truncated pointer. |
| `GetUserFullName()`: return `nullptr` | VxWorks' `struct passwd` has no `pw_gecos`; the generic branch did not compile. |
| `#include <taskLib.h>` | For `taskIdSelf()` and `taskName()`. |

### `public/client/TracyProfiler.cpp`

| Change | Why |
|---|---|
| `GetProcessName()`: basename of `RTP_DESC.pathName` from `rtpInfoGet(getpid())`; `"VxWorks kernel"` in a DKM | Otherwise the trace says `unknown`. There is no `getprogname()` / `program_invocation_short_name`. |
| `GetProcessExecutablePath()`: `RTP_DESC.pathName` | Same source, full path (`/ata0a/tracy_demo.vxe`). |
| `GetHostInfo()`: `OS: VxWorks (<release>, <kernel version>)` from `uname()` | Otherwise `OS: unknown`. The kernel's `struct utsname` has no `kernelversion`, hence the RTP/kernel split. |
| Safe-copy pipe: take the BSD/macOS branch (no `F_SETPIPE_SZ`) | VxWorks has `pipe()` but no `F_SETPIPE_SZ`/`F_GETPIPE_SZ`. Checked on the target: `pipe()` works, and `write()` from an unreadable address fails with `EINVAL` instead of crashing, so Tracy's pipe-based safe memory read works as designed. |
| `#include <sys/utsname.h>`, `<rtpLib.h>` (RTP) | For the above. |

### `public/client/tracy_rpmalloc.cpp`

| Change | Why |
|---|---|
| `#define atomic32_t rpmalloc_atomic32_t` (and `atomic64_t`), `#undef` at the end of the file | VxWorks' `<base/b_atomic_t.h>` (pulled in by the POSIX headers) already declares `atomic32_t`/`atomic64_t` as plain integers in the global namespace; rpmalloc's `std::atomic` typedefs of the same name do not compile. |
| `_rpmalloc_unmap_os()`: no `madvise()` on VxWorks | VxWorks has neither `madvise()` nor `posix_madvise()`. The decommit is only a hint: the pages stay committed until the span is released with `munmap()`, which VxWorks has (with `MAP_ANONYMOUS`). |
| Kernel only: page size from `vmPageSizeGet()` | No `sysconf(_SC_PAGESIZE)` in the kernel. |

## What needed no change

Checked on the target with `tools/probe/vx_probe.cpp` (a small RTP that
prints what each facility returns):

- **Sockets**: Tracy's BSD-socket code (`TracySocket.cpp`) compiles and works
  unchanged, with the default IPv6 dual-stack listen socket and with
  `TRACY_ONLY_IPV4`.
- **Threads**: `std::thread`, `pthread_*`, `thread_local` work in RTPs.
- **CPU count**: `std::thread::hardware_concurrency()` returns the number of
  CPUs (checked with `-smp 1/2/4`). `_SC_NPROCESSORS_ONLN` does not exist,
  but Tracy does not use it.
- **`/proc` and `/sys`**: all reads are inside `__linux__` blocks; nothing to
  disable.
- **CPU model**: `cpuid` works in an RTP.
- **Allocator**: rpmalloc on top of `mmap(MAP_ANONYMOUS)` works.
- **Call stacks, sampling, context switches, system tracing, crash handler,
  system CPU usage (`TracySysTime`), power (`TracySysPower`)**: their `#if`
  chains list the supported platforms explicitly, so on VxWorks they are
  simply not compiled in. No `TRACY_NO_*` define is needed.

### Timer

Tracy uses `rdtsc` on x86-64. `rdtsc` works in a VxWorks RTP (and in kernel
code). At start-up Tracy refuses to run unless `cpuid` reports an
*invariant* TSC:

- under QEMU/TCG (the default), the virtual CPU does not report it: run with
  `TRACY_NO_INVARIANT_CHECK=1` in the RTP's environment
  (`rtp exec -e TRACY_NO_INVARIANT_CHECK=1 ...`), as `run_demo.sh` does;
- under KVM with `-cpu host,+invtsc`, or on real hardware with a modern x86
  CPU, the check passes and nothing is needed (`KVM=1 scripts/run_demo.sh`).

`TRACY_TIMER_FALLBACK` is not a useful alternative on VxWorks: the fallback
uses `std::chrono::high_resolution_clock`, and in the SDK kernel
`clock_gettime(CLOCK_MONOTONIC)` advances only with the system clock tick
(`clock_getres()` reports the tick), which is far too coarse for profiling.
The C++ clocks sit on top of `clock_gettime()`. A better
fallback would need the BSP's high-resolution timestamp driver
(`sysTimestamp`), which is only callable from the kernel.

The trace checker verifies that frame, plot and message timestamps increase.

## What does not work / open points

- **Call stacks** (`TracyCallstack`) are not ported. VxWorks would need a
  `TRACY_HAS_CALLSTACK` variant: the unwinder (`_Unwind_Backtrace` from the
  LLVM runtime) and symbol resolution. Tracy's libbacktrace path needs
  `dl_iterate_phdr` and reading the executable's ELF file; for an RTP the
  executable path is known (`rtpInfoGet`), so this looks possible but is
  unverified. Static RTPs make it simpler (one image).
- **Sampling, context switches, system tracing**: Tracy implements these with
  Linux `perf`/ftrace and Windows ETW. The VxWorks equivalents (System Viewer
  / `wvLib`, the ED&R and `taskSwitchHookAdd`) are kernel facilities and are
  not exposed to RTPs. Out of scope.
- **Crash handler**: Tracy's crash handler is Linux/Windows only. A VxWorks
  RTP version with `sigaction()` is possible (VxWorks RTPs have POSIX
  signals), but it also needs to stop the other threads (Linux uses `tgkill`
  and `/proc/self/task`), which would need `taskSuspend`/`taskListGet`.
- **Host info**: `RAM: unknown` (no `sysinfo()`; `memInfoGet()` is
  kernel-only) and `User: @vxTarget` (`getlogin_r()` fails in the SDK
  kernel's RTPs). Cosmetic.
- **Startup handshake through QEMU's user networking**: QEMU accepts a
  forwarded connection even when nothing listens in the guest yet and then
  drops it, which `tracy-capture` reports as "client disconnected during the
  initial handshake". The demo therefore prints a line once Tracy listens,
  and `run_tracy_demo.py` connects only after it (and retries). This is a
  QEMU artefact, not a Tracy/VxWorks problem.
- Once, right after boot, `rtp exec` reported "Executable file not found" for
  a file that is on `/ata0a`; `run_tracy_demo.py` retries the launch once.
  Not reproduced since.

## Kernel modules (DKM)

The client also **compiles and links as a DKM** with the patch
(`cmake -B build/dkm -DVX_TARGET_TYPE=DKM ...`; the demo then has a
`tracyDemo(frames)` entry point). Differences from the RTP case, already
handled in the patch:

- `TASK_ID` is a 64-bit TCB pointer, not a 32-bit handle. The patch truncates
  it to 32 bits. This is not guaranteed unique; a proper port should map
  task ids to small numbers (for example, a counter in a task-create hook or
  in thread-local storage).
- `taskName()` is not called on the truncated id, `struct utsname` has no
  `kernelversion`, `sysconf(_SC_PAGESIZE)` is replaced by `vmPageSizeGet()`,
  and the process name is `"VxWorks kernel"`.
- `find_package(Threads)` (used by Tracy's CMake) falls back to `-lpthreads`
  for DKMs; [CMakeLists.txt](CMakeLists.txt) provides an empty
  `Threads::Threads` target instead.

**It does not load in the SDK kernel.** `tools/try_dkm.py` shows:

```
-> ld < /ata0a/tracy_demo.out
(tShell0): TLS variable alloc error : DKM_TLS_SIZE (0) too small
ld(): error loading file (errno = 0x610020).
```

`0x610020` is `S_loadElfLib_DKM_TLS_SIZE_TOO_SMALL`. The SDK kernel reserves
no thread-local storage for kernel modules, and Tracy needs some
(`thread_local` producer tokens and thread handle, rpmalloc's per-thread heap;
the module's `.tbss` is only a few dozen bytes). `DKM_TLS_SIZE` is a kernel
configuration parameter: the SDK cannot change it, a VxWorks installation can
(VIP parameter). This was not worked around. Other things a kernel port
would have to look at:

- the Profiler is a static object, so it starts when the module is loaded
  (C++ constructors run at `ld`) and stops only at unload; `TRACY_NO_EXIT`
  has no meaning there. `TRACY_DELAYED_INIT` + `TRACY_MANUAL_LIFETIME` would
  give explicit `StartupProfiler()`/`ShutdownProfiler()` calls instead;
- the kernel has `sysTimestamp()`-style high-resolution timers that could
  back a timer fallback, and kernel hooks (`taskSwitchHookAdd`) that could
  feed context-switch events, which RTPs cannot do;
- rpmalloc needs kernel `mmap()`; with a custom allocator
  (`TRACY_HAS_CUSTOM_ALLOCATOR`) it could use `malloc()` instead.

## Upstreaming

Two possible routes:

1. **Native support** (this patch): `#if defined __VXWORKS__` branches next
   to the existing QNX/BSD ones. Small, and it makes VxWorks work out of the
   box. Before a PR: build-test the non-VxWorks platforms in Tracy's CI
   (the patch only adds `#elif` branches), split the rpmalloc change (rpmalloc
   is vendored from upstream), and probably limit the first PR to RTPs.
2. **`TRACY_PLATFORM_HEADER`** (new in 0.15, see the manual, "Porting to
   unsupported platforms"): a user-supplied header can provide the thread id,
   user info, safe copy and allocator. That would cover only part of this
   port: the rpmalloc compile errors, `pw_gecos`, `F_SETPIPE_SZ`, the process
   name and the OS name are outside the hooks, so some patching is needed
   either way.

## Layout

```
CMakeLists.txt              builds Tracy's client (Tracy's own CMake) + demo + probe for VxWorks
cmake/toolchains/           VxWorks toolchain file and platform module (from vxworks-cpp-template)
cmake/VxWorksSupport.cmake  static RTPs, sysroot headers as system headers (trimmed from the template)
demo/tracy_demo.cpp         demo: 3 worker threads, nested zones, a lock, frames, plots, messages
patches/tracy-vxworks.patch the Tracy change (git diff against v0.15.0)
scripts/fetch_tracy.sh      clone Tracy v0.15.0 into ./tracy and apply the patch
scripts/make_patch.sh       regenerate the patch from ./tracy
scripts/run_demo.sh         build + QEMU run + capture + check, end to end
tools/run_tracy_demo.py     boots QEMU, runs the demo, runs tracy-capture
tools/try_dkm.py            the DKM load experiment
tools/trace_info/           host tool: prints and checks a .tracy file (uses Tracy's server library)
tools/probe/vx_probe.cpp    RTP that reports which OS facilities are available
tools/vxrun/                QEMU runner and RTP launcher (from vxworks-cpp-template)
```

`./tracy` (the patched upstream tree) and `./build` are not part of this
repository; `fetch_tracy.sh` recreates the former.

## Licensing

The code in this repository is MIT-licensed (see [LICENSE](LICENSE)), except
`patches/tracy-vxworks.patch`: it modifies Tracy, which is BSD-3-Clause, and is
offered under Tracy's licence. Tracy itself is not included; `fetch_tracy.sh`
downloads it. The toolchain files and `tools/vxrun/` come from
[vxworks-cpp-template](https://github.com/oguzhankynk/vxworks-cpp-template). This
repository contains no Wind River files; you need your own copy of the
VxWorks SDK under its licence. VxWorks is a trademark of Wind River Systems,
Inc.
