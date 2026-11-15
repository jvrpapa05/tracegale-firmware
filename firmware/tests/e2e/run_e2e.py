#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""End-to-end test: a simulated device, provisioned at runtime through its shell, against a real TLS server.

It creates throwaway credentials, which are never stored in the repository.
Usage: run_e2e.py <zephyr.exe of samples/connect, built for native_sim>
       run_e2e.py --qemu <zephyr.elf of samples/connect, built for qemu_cortex_m3 with overlay-qemu.conf,
                          or for qemu_riscv32 with overlay-qemu-riscv.conf>

The second form runs on Zephyr's own TCP stack: on Arm with 64 KB of RAM, and on RISC-V with Tracegale's default stack. The
machine is chosen from the ELF file. It also crashes the device on purpose and checks that the crash arrives after the
restart. These targets have no flash, so the device is provisioned again after restarting.
"""
import base64
import json
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SERVER = ROOT / "tools" / "wire" / "spec_server.py"
# Shell lines are limited to 256 characters by default, and anything longer is cut off, so pieces stay well below that.
CHUNK = 120


def openssl(*args):
    subprocess.run(["openssl", *args], check=True, capture_output=True)


def issue(folder, name, common_name, ca):
    key, csr, pem, ext = (folder / f"{name}.{kind}" for kind in ("key", "csr", "pem", "ext"))
    openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key))
    openssl("req", "-new", "-key", str(key), "-subj", f"/CN={common_name}", "-out", str(csr))
    ext.write_text(f"subjectAltName=DNS:{common_name}\n")
    openssl("x509", "-req", "-in", str(csr), "-CA", str(folder / f"{ca}.pem"), "-CAkey", str(folder / f"{ca}.key"), "-CAcreateserial",
            "-days", "2", "-sha256", "-extfile", str(ext), "-out", str(pem))


def new_ca(folder, name):
    key = folder / f"{name}.key"
    openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key))
    openssl("req", "-x509", "-new", "-key", str(key), "-subj", f"/CN={name}", "-days", "2", "-sha256", "-out", str(folder / f"{name}.pem"))


def der(folder, name, kind):
    out = folder / f"{name}.der"
    if kind == "cert":
        openssl("x509", "-in", str(folder / f"{name}.pem"), "-outform", "DER", "-out", str(out))
    else:
        openssl("pkcs8", "-topk8", "-nocrypt", "-in", str(folder / f"{name}.key"), "-outform", "DER", "-out", str(out))
    return out.read_bytes()


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Device:
    def __init__(self, exe, flash):
        self.proc = subprocess.Popen([exe, f"--flash={flash}"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.output = ""
        self.expect("uart:~$", 20)

    def pump(self, seconds):
        ready, _, _ = select.select([self.proc.stdout], [], [], seconds)
        if ready:
            data = self.proc.stdout.read1(4096) if hasattr(self.proc.stdout, "read1") else self.proc.stdout.read(4096)
            self.output += data.decode(errors="replace")

    def expect(self, text, seconds=10):
        end = time.time() + seconds
        while time.time() < end:
            if text in self.output:
                return
            self.pump(0.1)
        raise AssertionError(f"never saw {text!r}. Output ended with:\n{self.output[-1500:]}")

    def command(self, line, expect=None):
        self.output = ""
        for start in range(0, len(line), 32):
            self.proc.stdin.write(line[start:start + 32].encode())
            time.sleep(0.01)
        self.proc.stdin.write(b"\n")
        self.expect(expect or "uart:~$")
        self.pump(0.2)
        return self.output

    def status(self):
        self.command("tracegale status", "build id:")
        # The last line may still be arriving.
        end = time.time() + 5
        while "\n" not in self.output.split("build id:")[-1] and time.time() < end:
            self.pump(0.1)
        out = self.output
        fields = {}
        for line in out.replace("\r", "\n").split("\n"):
            if ": " in line:
                key, value = line.split(": ", 1)
                fields[key.split("uart:~$")[-1].strip()] = value.strip()
        return fields

    def wait_status(self, check, seconds=20, kick=False):
        end = time.time() + seconds
        last = {}
        while time.time() < end:
            last = self.status()
            if check(last):
                return last
            if kick:
                self.command("tracegale connect")
            time.sleep(0.5)
        raise AssertionError(f"status never reached the expected state: {last}")

    def stop(self):
        self.proc.kill()
        self.proc.wait()


class Server:
    def __init__(self, folder, port, name, extra=()):
        self.state = folder / f"state-{name}-{port}.json"
        self.proc = subprocess.Popen([sys.executable, str(SERVER), "https", str(port), str(folder / name), str(self.state),
                                      *extra], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            with socket.socket() as s:
                if s.connect_ex(("127.0.0.1", port)) == 0:
                    return
            time.sleep(0.1)
        raise AssertionError("the test server didn't start")

    def stats(self):
        return json.loads(self.state.read_text())

    def stop(self):
        self.proc.terminate()
        self.proc.wait()


def provision(device, port, creds, host="localhost"):
    device.command(f"tracegale server {host} {port}")
    for name, data in creds.items():
        for start in range(0, len(data), CHUNK):
            piece = base64.b64encode(data[start:start + CHUNK]).decode()
            total = min(start + CHUNK, len(data))
            # The device reports its running total, which also catches a piece being cut off.
            device.command(f"tracegale cred {name} {piece}", f"{name}: {total} bytes")
    device.command("tracegale commit", "provisioned")


def find_qemu(name):
    found = shutil.which(name)
    if found:
        return found
    # CI has QEMU only inside the Zephyr SDK.
    matches = sorted(Path.home().glob(f"zephyr-sdk-*/hosttools/sysroots/*/usr/bin/{name}"))
    if not matches:
        raise AssertionError(f"{name} wasn't found")
    return str(matches[-1])


def qemu_command(elf, folder):
    """The emulator for the ELF file's architecture, as Zephyr's boards qemu_cortex_m3 and qemu_riscv32 start it."""
    data = Path(elf).read_bytes()
    order = "<" if data[5] == 1 else ">"
    machine, = struct.unpack_from(order + "H", data, 18)
    if machine == 40:  # EM_ARM
        return [find_qemu("qemu-system-arm"), "-cpu", "cortex-m3", "-machine", "lm3s6965evb",
                "-netdev", "user,id=n0", "-net", "nic,netdev=n0", "-kernel", elf], "Arm Cortex-M3 with 64 KB of RAM"
    if machine == 243 and data[4] == 1:  # EM_RISCV, 32-bit
        # The CPU has the entropy source (zkr) that the board's random number driver reads.
        command = [find_qemu("qemu-system-riscv32"), "-machine", "virt", "-bios", "none", "-m", "256",
                   "-cpu", "rv32i,i=on,m=on,a=on,f=on,d=on,c=on,zicsr=on,zifencei=on,zkr=on,pmp=on,u=on",
                   "-global", "virtio-mmio.force-legacy=false",
                   "-netdev", "user,id=n0", "-device", "virtio-net-device,netdev=n0,bus=virtio-mmio-bus.0"]
        # This image runs from RAM. Given the ELF file, QEMU loads it again at every reset and clears the rest of each
        # segment, which includes the RAM that Zephyr leaves alone at boot, where the crash waits. A real chip's reset
        # doesn't do that. So only the bytes that are in the file are loaded, each part at its address.
        phoff, = struct.unpack_from(order + "I", data, 0x1C)
        size, count = struct.unpack_from(order + "HH", data, 0x2A)
        for i in range(count):
            kind, offset, _, address, in_file = struct.unpack_from(order + "IIIII", data, phoff + i * size)
            if kind == 1 and in_file > 0:  # PT_LOAD
                part = Path(folder) / f"image-{i}.bin"
                part.write_bytes(data[offset:offset + in_file])
                command += ["-device", f"loader,file={part},addr={address:#x},force-raw=on"]
        return command, "RISC-V"
    raise AssertionError(f"no emulator for ELF machine {machine}")


def elf_build_id(elf):
    """The GNU build ID that the firmware should report, read from its ELF file's notes."""
    data = Path(elf).read_bytes()
    order = "<" if data[5] == 1 else ">"
    if data[4] == 2:
        shoff, = struct.unpack_from(order + "Q", data, 0x28)
        size, count = struct.unpack_from(order + "HH", data, 0x3A)
    else:
        shoff, = struct.unpack_from(order + "I", data, 0x20)
        size, count = struct.unpack_from(order + "HH", data, 0x2E)
    for i in range(count):
        header = shoff + i * size
        kind, = struct.unpack_from(order + "I", data, header + 4)
        if kind != 7:  # SHT_NOTE
            continue
        offset, length = struct.unpack_from(order + ("QQ" if data[4] == 2 else "II"), data, header + (24 if data[4] == 2 else 16))
        notes = data[offset:offset + length]
        while len(notes) >= 12:
            name_size, desc_size, note_type = struct.unpack_from(order + "III", notes)
            name_end = 12 + (name_size + 3) // 4 * 4
            if note_type == 3 and notes[12:16] == b"GNU\0":
                return notes[name_end:name_end + desc_size].hex()
            notes = notes[name_end + (desc_size + 3) // 4 * 4:]
    raise AssertionError("the image has no build ID")


class QemuDevice(Device):
    def __init__(self, elf, folder):
        # A restart resets the emulated board, and keeps its RAM, as a real warm reset does.
        command, self.target = qemu_command(elf, folder)
        self.proc = subprocess.Popen(command + ["-nographic", "-serial", "stdio", "-monitor", "none"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.output = ""
        self.expect("uart:~$", 30)


def run_qemu(elf):
    with tempfile.TemporaryDirectory(prefix="tg-e2e-") as tmp:
        folder = Path(tmp)
        good = folder / "good"
        good.mkdir()
        new_ca(good, "ca")
        issue(good, "device", "tg-e2e-device", "ca")
        # QEMU's network reaches this machine at 10.0.2.2, so the certificate names that address. Mbed TLS checks it.
        key, csr, ext = good / "server.key", good / "server.csr", good / "server.ext"
        openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key))
        openssl("req", "-new", "-key", str(key), "-subj", "/CN=tracegale-e2e", "-out", str(csr))
        ext.write_text("subjectAltName=IP:10.0.2.2\n")
        openssl("x509", "-req", "-in", str(csr), "-CA", str(good / "ca.pem"), "-CAkey", str(good / "ca.key"), "-CAcreateserial",
                "-days", "2", "-sha256", "-extfile", str(ext), "-out", str(good / "server.pem"))
        creds = {"ca": der(good, "ca", "cert"), "cert": der(good, "device", "cert"), "key": der(good, "device", "key")}
        port = free_port()
        server = Server(folder, port, "good")
        device = QemuDevice(elf, folder)
        try:
            provision(device, port, creds, host="10.0.2.2")
            status = device.wait_status(lambda s: s["successes"] != "0", 60, kick=True)
            stats = server.stats()
            assert stats["messages"] >= 1 and stats["malformed"] == 0, stats
            print(f"PASS Zephyr's own TCP stack on {device.target} connects. Stack left: {status.get('stack free')} bytes")
            build_id = elf_build_id(elf)
            assert status["build id"] == build_id, (status["build id"], build_id)
            assert server.stats()["hello_build_ids"] == [build_id], server.stats()
            print("PASS the device reports the build ID that its ELF file has")

            assert status["crash waiting"] == "0 bytes", status
            device.output = ""
            device.proc.stdin.write(b"tracegale test fault\n")
            device.expect("crashing with an undefined instruction")
            device.expect("Booting Zephyr", 30)
            device.expect("uart:~$", 30)
            # The emulated UART drops what arrives straight after a reset.
            time.sleep(1)
            device.command("")
            status = device.status()
            assert status["crash waiting"] != "0 bytes", status
            provision(device, port, creds, host="10.0.2.2")
            status = device.wait_status(lambda s: s["crashes sent"] == "1", 60, kick=True)
            assert status["crash waiting"] == "0 bytes", status
            stats = server.stats()
            assert len(stats["completed"]) == 1 and stats["hash_failures"] == 0, stats
            crash = stats["completed"][0]
            dump = bytes.fromhex(crash["data"])
            assert crash["kind"] == 1 and dump[:2] == b"ZE", crash
            assert crash["build_id"] == build_id, crash
            print(f"PASS a crash survives the restart and arrives complete: {len(dump)} bytes, flags {crash['flags']}, "
                  f"with the build ID it crashed in. Stack left after sending it: {status.get('stack free')} bytes")
            if len(sys.argv) > 3:
                Path(sys.argv[3]).write_bytes(dump)
        finally:
            device.stop()
            server.stop()


def main():
    # run_e2e.py --qemu <elf> [file to save the crash in]
    if sys.argv[1] == "--qemu":
        run_qemu(sys.argv[2])
        return
    exe = sys.argv[1]
    passed = []
    with tempfile.TemporaryDirectory(prefix="tg-e2e-") as tmp:
        folder = Path(tmp)
        good = folder / "good"
        wrong = folder / "wrong"
        for path, ca in ((good, "ca"), (wrong, "ca")):
            path.mkdir()
            new_ca(path, ca)
            issue(path, "server", "localhost", ca)
        issue(good, "device", "tg-e2e-device", "ca")
        creds = {"ca": der(good, "ca", "cert"), "cert": der(good, "device", "cert"), "key": der(good, "device", "key")}
        flash = folder / "flash.bin"
        port = free_port()

        device = Device(exe, flash)
        try:
            status = device.status()
            assert status["state"] == "unprovisioned", status
            assert status["attempts"] == "0", status
            passed.append("a new device waits to be provisioned, and doesn't connect")

            # Nothing is listening yet, so the device must back off, not spin or crash.
            provision(device, port, creds)
            status = device.wait_status(lambda s: s["state"] == "backing off")
            # Zephyr v3.7 reports a refused connection as a failed handshake, so it shows as error 4 there.
            assert status["last error"][:2] in ("3,", "4,"), status
            passed.append("with no server, it reports a connection error and backs off")

            server = Server(folder, port, "good")
            try:
                status = device.wait_status(lambda s: s["successes"] != "0", kick=True)
                assert status["state"] == "idle", status
                stats = server.stats()
                assert stats["messages"] >= 1 and stats["malformed"] == 0, stats
                passed.append("once the server is up, it connects using runtime credentials, and the strict server accepts it")

                out = device.command("tracegale status", "build id:")
                for secret in (base64.b64encode(creds["key"][:30]).decode()[:20],):
                    assert secret not in out
                passed.append("status doesn't show the private key")

                device.stop()
                device = Device(exe, flash)
                status = device.wait_status(lambda s: s["successes"] != "0")
                assert status["server"] == f"localhost:{port}", status
                passed.append("after a restart, it reconnects from stored credentials, with nothing sent again")
            finally:
                server.stop()

            # A server with a certificate from another CA must be refused, and must not receive any message.
            impostor = Server(folder, port, "wrong")
            try:
                device.command("tracegale connect")
                status = device.wait_status(lambda s: s["last error"].startswith("4,"), kick=True)
                assert status["state"] == "backing off" or status["last error"].startswith("4,"), status
                assert impostor.stats()["messages"] == 0, impostor.stats()
                passed.append("a server with the wrong certificate gets a TLS error, and no messages")
            finally:
                impostor.stop()

            device.command("tracegale erase", "erased")
            device.stop()
            device = Device(exe, flash)
            time.sleep(1)
            status = device.status()
            assert status["state"] == "unprovisioned", status
            passed.append("erase forgets everything, including after a restart")
        finally:
            device.stop()
    for line in passed:
        print(f"PASS {line}")
    print(f"All {len(passed)} end-to-end checks passed.")


if __name__ == "__main__":
    main()
