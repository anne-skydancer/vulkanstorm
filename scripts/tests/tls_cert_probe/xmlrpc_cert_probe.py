# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""LEAP plugin for the TLS certificate probe (run_probe.ps1 starts it).

Posts one XML-RPC request per local test server to the viewer's
"LLXMLRPCTransaction" pump, the path the login request takes (method
vks_tls_probe, no credentials), and writes what the listener answers:
status, errorcode, and the certificate data that llstartup uses to pick
its dialog. Usage (as a LeapCommand): python -I xmlrpc_cert_probe.py <out.json>
"""
import json
import queue
import sys
import threading
import time

import llsd  # pip package "llsd"

OUT = sys.argv[1]
TARGETS = [
    ("valid", "https://localhost:18441/"),       # leaf from the CA put in the profile's CA.pem
    ("expired", "https://localhost:18442/"),     # same CA, leaf expired on 2025-01-01
    ("untrusted", "https://localhost:18443/"),   # leaf from a CA in no store
    ("wronghost", "https://localhost:18444/"),   # trusted CA, SAN not-localhost.invalid
    ("selfsigned", "https://localhost:18445/"),  # self-signed leaf
]
if len(sys.argv) > 2:
    # extra targets: a JSON list of [name, url]
    TARGETS += [tuple(t) for t in json.load(open(sys.argv[2]))]
TIMEOUT = 120.0
stdin = sys.stdin.buffer
stdout = sys.stdout.buffer


def log(*args):
    print("probe:", *args, file=sys.stderr, flush=True)


def read_msg():
    length = b""
    while True:
        c = stdin.read(1)
        if not c:
            return None
        if c == b":":
            break
        length += c
    return llsd.parse(stdin.read(int(length)))


def write_msg(pump, data):
    body = llsd.format_notation({"pump": pump, "data": data})
    stdout.write(str(len(body)).encode() + b":" + body)
    stdout.flush()


def llstartup_branch(status, errorcode, has_cert_key, cert):
    """The branch llstartup.cpp (login failure handling) takes for this reply.
    llstartup tests that the reply has a "certificate" key, whatever its
    value, before it tries to decode it."""
    if status == "Complete":
        return "none (TLS and the XML-RPC reply were accepted)"
    if status != "CURLError" or errorcode not in ("SSL_CACERT", "SSL_PEER_CERTIFICATE") or not has_cert_key:
        return "generic login error"
    if not (isinstance(cert, dict) and cert.get("pem")):
        # getCertificate(response["certificate"]) turns the LLSD map into ""
        return "GeneralCertificateErrorShort (certificate data has no PEM)"
    return "TrustCertificateError" if errorcode == "SSL_CACERT" else "GeneralCertificateError"


results = {}


def finish():
    with open(OUT, "w") as f:
        json.dump(results, f, indent=2, default=str)
    log("wrote", OUT)


reply_pump = read_msg()["pump"]
for i, (name, uri) in enumerate(TARGETS):
    write_msg("LLXMLRPCTransaction", {"uri": uri, "method": "vks_tls_probe", "reply": reply_pump,
                                      "reqid": i, "params": {"probe": name}})
# a reader thread feeds a queue, so the main thread stops at the deadline
# even when a target never replies, and writes the result once
inbox = queue.Queue()


def reader():
    while True:
        m = read_msg()
        inbox.put(m)
        if m is None:
            return


threading.Thread(target=reader, daemon=True).start()
deadline = time.time() + TIMEOUT
while len(results) < len(TARGETS) and time.time() < deadline:
    try:
        msg = inbox.get(timeout=max(0.1, deadline - time.time()))
    except queue.Empty:
        break
    if msg is None:
        break
    data = msg.get("data", {})
    if not isinstance(data, dict) or "reqid" not in data or data.get("status") == "Downloading":
        continue
    name, uri = TARGETS[int(data["reqid"])]
    cert = data.get("certificate")
    has_cert_key = "certificate" in data
    results[name] = {
        "uri": uri,
        "status": data.get("status"),
        "errorcode": data.get("errorcode"),
        "has_certificate": has_cert_key,
        "validation_failure": cert.get("validation_failure") if isinstance(cert, dict) else None,
        "cert_subject": cert.get("subject_name") if isinstance(cert, dict) else None,
        "llstartup": llstartup_branch(data.get("status"), data.get("errorcode"), has_cert_key, cert),
        "error": data.get("error"),
    }
    log(name, results[name]["status"], results[name]["errorcode"])
finish()
