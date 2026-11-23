#!/usr/bin/env python3
"""Boot the VxWorks SDK kernel in QEMU, run the Tracy demo RTP in it and
capture a trace from the host with Tracy's `capture` tool.

Steps:
  1. start QEMU with the demo on a FAT disk (/ata0a) and a host port forwarded
     to the guest's Tracy port 8086;
  2. in the cmd shell: rtp exec vxrun_launcher.vxe tracy_demo.vxe <frames>;
  3. on the host: tracy-capture -a 127.0.0.1 -p <host port> -o <file>;
  4. wait for capture to finish and for the RTP to exit (VXRUN-EXIT line);
  5. stop QEMU (only the QEMU process this script started).

Uses tools/vxrun/vxrun.py (copied from vxworks-cpp-template) for the serial
console handling. Exit status: 0 if the capture succeeded and the demo
exited with 0.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "vxrun"))
import vxrun  # noqa: E402


def log(msg: str) -> None:
    print(f"run_tracy_demo: {msg}", file=sys.stderr, flush=True)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--build-dir", required=True, help="VxWorks build dir (tracy_demo.vxe, vxrun_launcher.vxe)")
    p.add_argument("--capture", required=True, help="host tracy-capture binary")
    p.add_argument("--output", required=True, help=".tracy file to write")
    p.add_argument("--wind-home", default=os.environ.get("WIND_HOME"))
    p.add_argument("--host-port", type=int, default=18086)
    p.add_argument("--frames", type=int, default=200)
    p.add_argument("--smp", type=int, default=2)
    p.add_argument("--kvm", action="store_true",
                   help="use KVM and -cpu host,+invtsc (else TCG with TRACY_NO_INVARIANT_CHECK=1)")
    p.add_argument("--timeout", type=float, default=300)
    p.add_argument("--pid-file", help="write the QEMU PID here")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()

    kernel = vxrun.find_kernel(args.wind_home, None)
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        log("qemu-system-x86_64 not found")
        return 125

    demo = os.path.join(args.build_dir, "tracy_demo.vxe")
    launcher = os.path.join(args.build_dir, "vxrun_launcher.vxe")
    with tempfile.TemporaryDirectory(prefix="tracy-vx-") as stage:
        for f in (demo, launcher):
            shutil.copy2(f, stage)
        cmd = [qemu, "-snapshot", "-no-reboot", "-m", "2048M", "-smp", str(args.smp)]
        if args.kvm:
            cmd += ["-enable-kvm", "-cpu", "host,+invtsc"]
        cmd += ["-kernel", kernel,
                "-drive", f"file=fat:{stage},format=raw,if=ide",
                "-nic", f"user,hostfwd=tcp:127.0.0.1:{args.host_port}-:8086",
                "-display", "none", "-serial", "stdio", "-monitor", "none",
                "-append", vxrun.DEFAULT_BOOTLINE]
        target = vxrun.Target(cmd, args.verbose)
        log(f"QEMU pid {target.proc.pid}, host port {args.host_port} -> guest 8086")
        if args.pid_file:
            with open(args.pid_file, "w") as fh:
                fh.write(f"{target.proc.pid}\n")
        deadline = time.monotonic() + args.timeout
        capture = None
        try:
            target.read_until(vxrun.C_SHELL_PROMPT, deadline)
            target.read_until(vxrun.CMD_SHELL_PROMPT, deadline, target.send("cmd"))
            env = "" if args.kvm else "-e TRACY_NO_INVARIANT_CHECK=1 "
            line = f"rtp exec {env}/ata0a/vxrun_launcher.vxe /ata0a/tracy_demo.vxe {args.frames}"
            # The demo prints "waiting for a server" once Tracy's listen socket
            # is open. Rarely, the first launch right after boot fails with
            # "Executable file not found"; try once more in that case.
            ready = re.compile(rb"waiting for a server|unable to launch process")
            for attempt in range(2):
                start = target.send(line)
                log(f"target: {line}")
                m = target.read_until(ready, deadline, start)
                if m.group(0).startswith(b"waiting"):
                    break
                log("launch failed, retrying")
                time.sleep(2)
            else:
                log("could not launch the demo")
                return 125
            # QEMU accepts the forwarded connection itself and drops it if the
            # guest does not answer yet, so retry a few times.
            cap_cmd = [args.capture, "-f", "-a", "127.0.0.1", "-p", str(args.host_port), "-o", args.output]
            for attempt in range(5):
                capture = subprocess.Popen(cap_cmd)
                try:
                    rc = capture.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    break  # connected and receiving data
                if rc == 0:
                    break
                log(f"capture attempt {attempt + 1} failed ({rc}), retrying")
                time.sleep(1)
            match = target.read_until(vxrun.EXIT_LINE, deadline, start)
            sys.stdout.write(vxrun.program_output(target.buffer[start:start + match.end()]) + "\n")
            rc = capture.wait(timeout=max(1.0, deadline - time.monotonic()))
            log(f"capture exited with {rc}")
            demo_rc = int(match.group(2)) if match.group(2) is not None else 1
            log(f"tracy_demo exited with {match.group(1).decode()}")
            return 0 if rc == 0 and demo_rc == 0 else 1
        except vxrun.Timeout:
            log("timed out; last console output:")
            sys.stderr.write(target.text(target.buffer[-3000:]) + "\n")
            return 124
        finally:
            if capture and capture.poll() is None:
                capture.kill()
                capture.wait()
            target.close()
            log("QEMU stopped")


if __name__ == "__main__":
    sys.exit(main())
