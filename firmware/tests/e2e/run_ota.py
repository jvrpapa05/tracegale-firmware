#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""
End-to-end over-the-air updates, with the real MCUboot.

An emulated Arm board (QEMU's mps2-an385) runs MCUboot and the connect sample from a flash that's a file, so a
killed emulator is a power cut. The device is provisioned through its shell, and updated over HTTPS with client
certificates by the specification's server. Nothing is faked on the device: MCUboot checks signatures, swaps the
images, and puts the old image back when the new one isn't confirmed.

What it proves:

1. An update that loses power in the middle continues where it stopped, installs, and is confirmed.
2. An older image is refused, and the image that's already running isn't downloaded again.
3. After an update that failed or was refused, the device waits before it takes another.
4. An image that can't reach the server is rolled back, and the old image reports it.
5. An image signed with another key never runs.

Usage: run_ota.py --mcuboot BIN --base BIN --next BIN --bad BIN --wrong-key BIN
"""

import argparse
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_e2e as e  # noqa: E402

# The layout in ota/flash.dtsi.
FLASH_SIZE = 16 << 20
SLOT0 = 0x20000
# The address of the emulator's host, as the device sees it.
HOST = "10.0.2.2"

INSTALLED, NOT_STARTED, REVERTED, DOWNGRADE = 1, 2, 3, 7


class Rig(e.Device):
    """The board. Its flash is the file, which outlives the emulator."""

    def __init__(self, folder, flash):
        command = [e.find_qemu("qemu-system-arm"), "-cpu", "cortex-m3", "-machine", "mps2-an385,memory-backend=flash",
                   "-object", f"memory-backend-file,id=flash,size={FLASH_SIZE},mem-path={flash},share=on",
                   # The CPU reads its first stack pointer and address at 0, where this board has other memory.
                   "-device", f"loader,file={folder / 'vectors.bin'},addr=0x0,force-raw=on",
                   "-nic", "user,model=lan9118", "-nographic", "-serial", "stdio", "-monitor", "none"]
        self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     bufsize=0)
        self.output = ""
        self.expect("uart:~$", 30)


def wait_device(device, check, seconds, kick=False):
    """Like Device.wait_status, for a device that may restart while it's asked."""
    end = time.time() + seconds
    last = {}
    while time.time() < end:
        try:
            last = device.status()
            if check(last):
                return last
            if kick:
                device.command("tracegale connect")
        except AssertionError:
            pass
        time.sleep(0.5)
    raise AssertionError(f"the device never reached the expected state: {last}\n{device.output[-1500:]}")


def wait_server(server, check, seconds, device=None):
    end = time.time() + seconds
    last = {}
    while time.time() < end:
        last = server.stats()
        if check(last):
            return last
        if device:
            # Keeps the output moving, so a full pipe never stops the emulator.
            device.pump(0.2)
        else:
            time.sleep(0.2)
    tail = device.output[-1500:] if device else ""
    raise AssertionError(f"the server never saw the expected messages: {last}\n{tail}")


def kick_until_result(server, device, seconds):
    """Has the device check in again and again until the server has a result from it."""
    end = time.time() + seconds
    while not server.stats()["image_results"]:
        assert time.time() < end, f"no result: {server.stats()}\n{device.output[-1500:]}"
        try:
            device.command("tracegale connect")
        except AssertionError:
            # Restarting.
            pass
        device.pump(2)
    return server.stats()


def held(server, device):
    """True if the device checks in and, although the server announces an update, doesn't ask for it."""
    successes = int(device.status()["successes"])
    wait_device(device, lambda s: int(s["successes"]) > successes, 30, kick=True)
    return server.stats()["image_checks"] == 0


def outcomes(stats):
    return [result["outcome"] for result in stats["image_results"]]


def run(args):
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        creds_dir = folder / "good"
        creds_dir.mkdir()
        e.new_ca(creds_dir, "ca")
        e.issue(creds_dir, "device", "tg-e2e-device", "ca")
        # The server's certificate names the address the device connects to.
        key, csr, ext = creds_dir / "server.key", creds_dir / "server.csr", creds_dir / "server.ext"
        e.openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key))
        e.openssl("req", "-new", "-key", str(key), "-subj", "/CN=tracegale-e2e", "-out", str(csr))
        ext.write_text(f"subjectAltName=IP:{HOST}\n")
        e.openssl("x509", "-req", "-in", str(csr), "-CA", str(creds_dir / "ca.pem"), "-CAkey", str(creds_dir / "ca.key"),
                  "-CAcreateserial", "-days", "2", "-sha256", "-extfile", str(ext), "-out", str(creds_dir / "server.pem"))
        creds = {"ca": e.der(creds_dir, "ca", "cert"), "cert": e.der(creds_dir, "device", "cert"),
                 "key": e.der(creds_dir, "device", "key")}

        boot = args.mcuboot.read_bytes()
        base, newer = args.base.read_bytes(), args.next.read_bytes()
        flash = folder / "flash.bin"
        data = bytearray(b"\xff" * FLASH_SIZE)
        data[0:len(boot)] = boot
        data[SLOT0:SLOT0 + len(base)] = base
        flash.write_bytes(data)
        (folder / "vectors.bin").write_bytes(boot[:8])

        port = e.free_port()
        ref = 10

        def serve(image=None, delay=0):
            nonlocal ref
            ref += 1
            extra = ["--image", str(image), "--image-ref", str(ref), "--data-delay", str(delay)] if image else []
            return e.Server(folder, port, "good", extra)

        server = serve()
        device = Rig(folder, flash)
        try:
            e.provision(device, port, creds, host=HOST)
            status = wait_device(device, lambda s: s.get("successes", "0") != "0", 90, kick=True)
            assert status["firmware"] == "1.2.0" and status["update"].startswith("none"), status
            assert server.stats()["image_checks"] == 0, "the device asked for an update that nobody announced"
            print("ok: the first image checks in, and asks for no update")

            # 1. An update, with the power cut in the middle.
            server.stop()
            server = serve(args.next, delay=0.05)
            device.command("tracegale connect")
            wait_device(device, lambda s: s["update"].startswith("downloading") and int(s["update"].split()[1]) >= 60000, 120)
            device.stop()
            before = server.stats()["image_bytes"]
            assert 60000 <= before < len(newer), before
            device = Rig(folder, flash)
            stats = wait_server(server, lambda s: s["image_results"], 180, device)
            assert outcomes(stats) == [INSTALLED], stats["image_results"]
            # A resumed download repeats at most one flash page and the block that was on its way.
            assert stats["image_bytes"] <= len(newer) + 8192, (stats["image_bytes"], len(newer))
            status = wait_device(device, lambda s: s["update"].startswith("none"), 30)
            assert status["firmware"] == "1.3.0" and status["last update result"] == str(INSTALLED), status
            print(f"ok: an update continued after a power cut at {before} of {len(newer)} bytes, "
                  f"and {stats['image_bytes'] - len(newer)} bytes were sent twice")

            # The image was confirmed, so it stays through another power cut.
            device.stop()
            device = Rig(folder, flash)
            status = wait_device(device, lambda s: s.get("successes", "0") != "0", 60)
            assert status["firmware"] == "1.3.0", status
            print("ok: the new image is still there after a power cut")

            # 2 and 3. An older image, and then the same image again.
            server.stop()
            server = serve(args.base)
            device.command("tracegale connect")
            stats = wait_server(server, lambda s: s["image_results"], 60, device)
            assert outcomes(stats) == [DOWNGRADE] and stats["image_gets"] == 1, stats
            server.stop()
            print("ok: an older image is refused after its first block")
            server = serve(args.next)
            # The slot was erased for the older image. So that a server can't have that done again and again, the
            # device now waits, here 20 seconds from the refusal.
            assert held(server, device), "the device didn't wait after a refused update"
            stats = kick_until_result(server, device, 60)
            assert outcomes(stats) == [INSTALLED] and stats["image_gets"] == 0, stats
            print("ok: the device waited after that, and then didn't download the image that it's running")

            # 4. An image that can't reach the server.
            server.stop()
            server = serve(args.bad)
            size = len(args.bad.read_bytes())
            # Timed from the request for a check-in, since the emulator downloads faster than the server is asked how far it is.
            # The figure says nothing about a real link.
            start = time.time()
            device.command("tracegale connect")
            wait_server(server, lambda s: s["image_bytes"] >= size, 180, device)
            seconds = time.time() - start
            wait_device(device, lambda s: s.get("firmware") == "1.4.0" and s["update"].startswith("testing"), 60)
            print(f"ok: {size} bytes downloaded in {seconds:.1f} s on the emulator, and the image is being tested")
            stats = wait_server(server, lambda s: s["image_results"], 120, device)
            assert outcomes(stats) == [REVERTED], stats["image_results"]
            status = wait_device(device, lambda s: s["update"].startswith("none"), 30)
            assert status["firmware"] == "1.3.0" and status["last update result"] == str(REVERTED), status
            print("ok: an image that can't reach the server was rolled back, and the old image reported it")

            # 5. An image signed with another key, offered while the device still waits after the failure.
            server.stop()
            server = serve(args.wrong_key)
            assert held(server, device), "the device didn't wait after a failed update"
            print("ok: after a failed update the device waits before it asks again")
            stats = kick_until_result(server, device, 180)
            assert outcomes(stats) == [NOT_STARTED] and stats["image_bytes"] >= len(args.wrong_key.read_bytes()), stats
            status = wait_device(device, lambda s: s["update"].startswith("none"), 30)
            assert status["firmware"] == "1.3.0", status
            print("ok: an image signed with another key was downloaded, and MCUboot refused to run it")
            print(f"Tracegale's thread: {status.get('stack free', 'unknown')} bytes of stack free")
        finally:
            device.stop()
            server.stop()
    print("PASS")


def main():
    p = argparse.ArgumentParser()
    for name in ("mcuboot", "base", "next", "bad", "wrong-key"):
        p.add_argument(f"--{name}", type=Path, required=True)
    run(p.parse_args())


if __name__ == "__main__":
    main()
