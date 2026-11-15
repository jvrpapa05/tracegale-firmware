#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""A test server following docs/protocol/README.md, for tests and measurements. It isn't the product's server.

Usage: spec-server.py <http|https|mqtt> <port> <creds-dir> <state-file> [--chunk-max N]
                      [--image FILE [--image-ref N] [--data-delay SECONDS]]

With --image, it offers that file as an update the way the product's server does: it sets the "pending" bit, answers
IMG_CHECK with an offer, and stops offering to a device that installed the image, refused it for good, or failed it
three times. Without it, IMG_CHECK always gets an offer of a made-up image, for measurements.
"""
import argparse
import hashlib
import json
import os
import socket
import ssl
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tracegale_wire as wire  # noqa: E402

IMAGE = bytes(((i * 31) ^ (i >> 8)) & 0xFF for i in range(64 * 1024))
IMAGE_REF = 3
ERROR, ACK, STATUS, OFFER, DATA = 3, 2, 12, 21, 23


class Server:
    """All protocol behaviour. Transfers are found by device and hash, as the specification says."""

    def __init__(self, state_file, chunk_max, image=None, image_ref=IMAGE_REF, data_delay=0):
        self.state_file = state_file
        self.chunk_max = chunk_max
        # A rollout of one image, when a file is given.
        self.rollout = image is not None
        self.image = image if self.rollout else IMAGE
        self.image_ref = image_ref
        self.data_delay = data_delay
        self.image_failures = {}
        self.image_done = set()
        self.transfers = {}
        self.lock = threading.Lock()
        self.stats = {"messages": 0, "malformed": 0, "repeated_chunks": 0, "resumed": 0, "transfers_verified": 0,
                      "hash_failures": 0, "completed": [], "hello_build_ids": [], "image_checks": 0, "image_gets": 0,
                      "image_bytes": 0, "image_results": []}
        self.save()

    def save(self):
        # Replaced in one step, so a test that reads it never finds half a file.
        partial = Path(f"{self.state_file}.partial")
        partial.write_text(json.dumps(self.stats))
        os.replace(partial, self.state_file)

    def error(self, ident, code):
        return wire.encode(ERROR, ident, {0: code})

    def status(self, ident, xfer):
        return wire.encode(STATUS, ident, {0: xfer["id"], 1: len(xfer["data"]), 2: self.chunk_max,
                                           3: xfer["complete"]})

    def handle(self, device, raw):
        try:
            _version, kind, ident, body = wire.decode(raw)
        except wire.WireError:
            with self.lock:
                self.stats["malformed"] += 1
                self.save()
            return self.error(0, 1)
        with self.lock:
            self.stats["messages"] += 1
            reply = self.dispatch(device, kind, ident, body)
            self.save()
            return reply

    def dispatch(self, device, kind, ident, body):
        if kind == 1 and isinstance(body.get(1), bytes) and body[1].hex() not in self.stats["hello_build_ids"]:
            self.stats["hello_build_ids"].append(body[1].hex())
        if kind == 24:
            self.img_result(device, body)
        if kind in (1, 4, 24):
            return self.ack(device, ident)
        if kind == 10:
            return self.xfer_start(device, ident, body)
        if kind == 11:
            return self.xfer_chunk(device, ident, body)
        if kind == 20:
            self.stats["image_checks"] += 1
            if self.rollout and device in self.image_done:
                return self.ack(device, ident)
            return wire.encode(OFFER, ident, {0: self.image_ref, 1: len(self.image), 2: 0, 4: "1.3.0",
                                              5: hashlib.sha256(self.image).digest()})
        if kind == 22:
            return self.img_get(ident, body)
        return self.error(ident, 3)

    def ack(self, device, ident):
        """Bit 0 of "pending" tells a device to ask for its update."""
        if self.rollout and device not in self.image_done:
            return wire.encode(ACK, ident, {0: 1})
        return wire.encode(ACK, ident, {})

    def img_result(self, device, body):
        outcome = body.get(1)
        self.stats["image_results"].append({"device": device, "image_ref": body.get(0), "outcome": outcome})
        if body.get(0) != self.image_ref:
            return
        # 1 is installed, 2 to 4 may work at another try, and the rest never will.
        if outcome in (2, 3, 4):
            self.image_failures[device] = self.image_failures.get(device, 0) + 1
        if outcome not in (2, 3, 4) or self.image_failures[device] >= 3:
            self.image_done.add(device)

    def xfer_start(self, device, ident, body):
        if not all(k in body for k in (0, 1, 2)):
            return self.error(ident, 4)
        key = (device, body[2])
        xfer = self.transfers.get(key)
        if xfer is None:
            xfer = {"id": len(self.transfers) + 1, "total": body[1], "sha": body[2], "data": bytearray(),
                    "complete": False, "device": device, "kind": body[0], "build_id": body.get(3, b""), "flags": body.get(4, 0)}
            self.transfers[key] = xfer
        elif xfer["data"]:
            self.stats["resumed"] += 1
        return self.status(ident, xfer)

    def xfer_chunk(self, device, ident, body):
        if not all(k in body for k in (0, 1, 2)):
            return self.error(ident, 4)
        if len(body[2]) > self.chunk_max:
            return self.error(ident, 5)
        xfer = next((x for x in self.transfers.values() if x["id"] == body[0] and x["device"] == device), None)
        if xfer is None:
            return self.error(ident, 8)
        # A repeat or a gap changes nothing, and the reply tells the device where to continue.
        if body[1] != len(xfer["data"]) or xfer["complete"]:
            self.stats["repeated_chunks"] += 1
            return self.status(ident, xfer)
        xfer["data"] += body[2]
        if len(xfer["data"]) >= xfer["total"]:
            if hashlib.sha256(xfer["data"]).digest() != xfer["sha"]:
                self.stats["hash_failures"] += 1
                del self.transfers[(device, xfer["sha"])]
                return self.error(ident, 7)
            xfer["complete"] = True
            self.stats["transfers_verified"] += 1
            # Tests read what arrived from here.
            self.stats["completed"].append({"kind": xfer["kind"], "data": bytes(xfer["data"]).hex(),
                                            "build_id": xfer["build_id"].hex(), "flags": xfer["flags"]})
        return self.status(ident, xfer)

    def img_get(self, ident, body):
        if body.get(0) != self.image_ref:
            return self.error(ident, 9)
        offset, length = body.get(1, -1), body.get(2, 0)
        if not 0 <= offset < len(self.image) or length <= 0:
            return self.error(ident, 6)
        data = self.image[offset:offset + min(length, self.chunk_max)]
        self.stats["image_gets"] += 1
        self.stats["image_bytes"] += len(data)
        if self.data_delay:
            time.sleep(self.data_delay)
        return wire.encode(DATA, ident, {0: self.image_ref, 1: offset, 2: data})


def serve_http(server, port, context):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        disable_nagle_algorithm = True
        timeout = 60

        def setup(self):
            if context is not None:
                self.request.do_handshake()
            super().setup()

        def device(self):
            if context is None:
                return "psk-device"
            subject = dict(item[0] for item in self.request.getpeercert()["subject"])
            return subject["commonName"]

        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            if self.path != "/v1/m" or length > wire.MAX_MESSAGE:
                self.send_error(404)
                return
            raw = self.rfile.read(length)
            if len(raw) != length:
                self.close_connection = True
                return
            reply = server.handle(self.device(), raw)
            # Only what HTTP requires, so the cost measured is close to a server tuned for this.
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Type: application/vnd.tracegale+cbor\r\nContent-Length: "
                             + str(len(reply)).encode() + b"\r\n\r\n" + reply)

        def log_message(self, *args):
            pass

    class Quiet(ThreadingHTTPServer):
        daemon_threads = True

        def handle_error(self, request, client_address):
            pass

    httpd = Quiet(("127.0.0.1", port), Handler)
    if context is not None:
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True, do_handshake_on_connect=False)
    httpd.serve_forever()


def serve_mqtt(server, port):
    import paho.mqtt.client as mqtt

    def on_message(client, _userdata, msg):
        device = msg.topic.split("/")[2]
        client.publish(f"tg/v1/{device}/down", server.handle(device, msg.payload), qos=1)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="tracegale-server")
    client.on_message = on_message
    client.max_inflight_messages_set(1000)
    client.connect("127.0.0.1", port)
    client.subscribe("tg/v1/+/up", qos=1)
    client.loop_forever()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mode", choices=["http", "https", "mqtt"])
    p.add_argument("port", type=int)
    p.add_argument("creds", type=Path)
    p.add_argument("state")
    p.add_argument("--chunk-max", type=int, default=4096)
    p.add_argument("--image", type=Path)
    p.add_argument("--image-ref", type=int, default=IMAGE_REF)
    p.add_argument("--data-delay", type=float, default=0)
    a = p.parse_args()
    server = Server(a.state, a.chunk_max, a.image.read_bytes() if a.image else None, a.image_ref, a.data_delay)
    if a.mode == "mqtt":
        serve_mqtt(server, a.port)
        return
    context = None
    if a.mode == "https":
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(a.creds / "server.pem", a.creds / "server.key")
        context.load_verify_locations(a.creds / "ca.pem")
        context.verify_mode = ssl.CERT_REQUIRED
    serve_http(server, a.port, context)


if __name__ == "__main__":
    main()
