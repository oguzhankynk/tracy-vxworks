#!/usr/bin/env python3
"""Experiment: load the Tracy demo as a kernel module (DKM) in the SDK's QEMU
kernel and, if it loads, capture a trace from it.

With the VxWorks SDK 26.09 kernel this stops at `ld`:
    TLS variable alloc error : DKM_TLS_SIZE (0) too small
because the kernel reserves no thread-local storage for kernel modules and
Tracy uses thread_local variables. See README.md, "Kernel modules (DKM)".

Usage: tools/try_dkm.py --module build/dkm/tracy_demo.out --capture <tracy-capture>
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "vxrun"))
import vxrun  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--module", required=True)
    p.add_argument("--capture", required=True)
    p.add_argument("--output", default="build/trace/dkm.tracy")
    p.add_argument("--host-port", type=int, default=18089)
    p.add_argument("--wind-home", default=os.environ.get("WIND_HOME"))
    args = p.parse_args()

    kernel = vxrun.find_kernel(args.wind_home, None)
    with tempfile.TemporaryDirectory(prefix="tracy-vx-dkm-") as stage:
        shutil.copy2(args.module, stage)
        name = os.path.basename(args.module)
        cmd = ["qemu-system-x86_64", "-snapshot", "-no-reboot", "-m", "2048M", "-smp", "2",
               "-kernel", kernel, "-drive", f"file=fat:{stage},format=raw,if=ide",
               "-nic", f"user,hostfwd=tcp:127.0.0.1:{args.host_port}-:8086",
               "-display", "none", "-serial", "stdio", "-monitor", "none",
               "-append", vxrun.DEFAULT_BOOTLINE]
        target = vxrun.Target(cmd, False)
        print(f"try_dkm: QEMU pid {target.proc.pid}", flush=True)
        deadline = time.monotonic() + 240
        try:
            target.read_until(vxrun.C_SHELL_PROMPT, deadline)
            start = target.send(f"ld < /ata0a/{name}")
            end = target.read_until(vxrun.C_SHELL_PROMPT, deadline, start + 1)
            out = target.text(target.buffer[start:end])
            print(out)
            if "error" in out.lower():
                print("try_dkm: the module did not load")
                return 1
            start = target.send("sp tracyDemo, 50")
            target.read_until(b"waiting for a server", deadline, start)
            rc = subprocess.run([args.capture, "-f", "-a", "127.0.0.1", "-p", str(args.host_port),
                                 "-s", "20", "-o", args.output], timeout=120).returncode
            print(f"try_dkm: capture exited with {rc}")
            return rc
        except vxrun.Timeout:
            print(target.text(target.buffer[-3000:]))
            return 124
        finally:
            target.close()
            print("try_dkm: QEMU stopped")


if __name__ == "__main__":
    sys.exit(main())
