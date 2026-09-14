#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""E1000/DHCP/ICMP/UDP/DNS with a loopback-only fixture and headless QEMU."""
import importlib.util
import ipaddress
import json
from pathlib import Path
import socket
import struct
import threading
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("utamo_network_gate", ROOT/"scripts/test-process-qemu.py")
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)
H = P.HARNESS

class Fixture:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(0.1)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.events = []
        self.errors = []
        self.thread = threading.Thread(target=self.run, name="utamo-local-udp", daemon=True)
    def run(self):
        try:
            while not self.stop.is_set():
                try:
                    data, peer = self.socket.recvfrom(2048)
                except socket.timeout:
                    continue
                if data.startswith(b"UTAMO"):
                    response = data
                    kind = "udp-echo"
                else:
                    if len(data) < 17 or data[2:12] != b"\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00":
                        self.errors.append("unexpected query header")
                        continue
                    position, labels = 12, []
                    while position < len(data) and data[position]:
                        size = data[position]
                        if size > 63 or position + 1 + size >= len(data):
                            raise ValueError("invalid fixture query name")
                        labels.append(data[position+1:position+1+size].decode("ascii"))
                        position += size + 1
                    if data[position:] != b"\x00\x00\x01\x00\x01":
                        raise ValueError("invalid fixture query tail")
                    name = ".".join(labels)
                    response = bytearray(data)
                    response[2:4] = b"\x81\x80"
                    response[6:8] = b"\x00\x01"
                    pointer = 12 if name == "fixture.test" else len(data)
                    response += struct.pack("!HHHIH4s", 0xc000 | pointer, 1, 1, 60, 4,
                                            socket.inet_aton("192.0.2.123"))
                    kind = "dns-a" if name == "fixture.test" else "dns-invalid-loop"
                self.socket.sendto(response, peer)
                self.events.append({"kind": kind, "request_bytes": len(data), "response_bytes": len(response)})
        except (OSError, ValueError, UnicodeError) as error:
            if not self.stop.is_set():
                self.errors.append(str(error))
    def __enter__(self):
        self.thread.start()
        return self
    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(1)
        self.socket.close()
        if self.thread.is_alive():
            self.errors.append("fixture thread did not stop")

def packet_counts(path):
    data = path.read_bytes()
    if len(data) < 24 or data[:4] != b"\xd4\xc3\xb2\xa1":
        raise ValueError("Expected little-endian QEMU pcap")
    if struct.unpack_from("<I", data, 20)[0] != 1:
        raise ValueError("Expected Ethernet pcap")
    counts = {"frames": 0, "arp_request": 0, "arp_reply": 0, "icmp_request": 0,
              "icmp_reply": 0, "udp": 0, "dhcp_1": 0, "dhcp_2": 0, "dhcp_3": 0, "dhcp_5": 0}
    position = 24
    while position < len(data):
        if position + 16 > len(data):
            raise ValueError("Truncated pcap record")
        size = struct.unpack_from("<I", data, position+8)[0]
        position += 16
        frame = data[position:position+size]
        if len(frame) != size:
            raise ValueError("Truncated pcap frame")
        position += size
        counts["frames"] += 1
        if len(frame) < 14:
            continue
        ethertype = int.from_bytes(frame[12:14], "big")
        if ethertype == 0x806 and len(frame) >= 42:
            op = int.from_bytes(frame[20:22], "big")
            if op in (1, 2):
                counts["arp_request" if op == 1 else "arp_reply"] += 1
        elif ethertype == 0x800 and len(frame) >= 34:
            ip = frame[14:]
            head = (ip[0] & 15)*4
            total = int.from_bytes(ip[2:4], "big")
            if head < 20 or total > len(ip):
                raise ValueError("Invalid captured IPv4")
            body = ip[head:total]
            if ip[9] == 1 and body and body[0] in (0, 8):
                counts["icmp_request" if body[0] == 8 else "icmp_reply"] += 1
            if ip[9] == 17 and len(body) >= 8:
                counts["udp"] += 1
                ports = struct.unpack_from("!HH", body)
                payload = body[8:]
                if set(ports) == {67, 68} and len(payload) >= 240:
                    offset = 240
                    while offset < len(payload):
                        option = payload[offset]
                        offset += 1
                        if option == 255:
                            break
                        if option == 0:
                            continue
                        if offset >= len(payload):
                            raise ValueError("Truncated DHCP option")
                        length = payload[offset]
                        offset += 1
                        if offset + length > len(payload):
                            raise ValueError("DHCP option overflow")
                        if option == 53 and length == 1:
                            key = "dhcp_" + str(payload[offset])
                            if key in counts:
                                counts[key] += 1
                        offset += length
    return counts

def suite(vm, symbols):
    vm.report["mode"] = "E1000 Ethernet ARP IPv4 ICMP UDP DHCP DNS suite"
    vm.report["separate_host_coverage"] = [
        "Actual E1000 driver MMIO/DMA model, ring wrap, reset/descriptor failures and quarantine",
        "Packet bounds/checksums/fragments, DHCP rejection, DNS compression loops",
        "Transactions, invalid peers, timeout cleanup, lease and ARP expiration"]
    if not vm.args.network:
        boot = vm.wait_for(H.PROMPT)
        vm.check("Version: " + vm.args.version in boot, "Boot version matches candidate without NIC")
        vm.check("E1000 initialized" not in boot, "Absent NIC does not publish a driver")
        vm.command("netinfo", ("E1000 ready=No link=Down DMA_pages=0 quarantine=No",
                              "IPv4 configured=No IP=0.0.0.0"))
        vm.command("dhcp", ("DHCP: FAIL",))
        vm.command("ping gateway", ("Ping unavailable or invalid address.",))
        vm.command("resolve fixture.test", ("DNS resolution failed.",))
        vm.command("heap", ("Heap integrity: OK",))
        vm.command("schedulerstats", ("Scheduler integrity: OK",))
        start = len(vm.serial())
        vm.type_text("halt\n")
        vm.wait_for("System halted.", start)
        vm.halt_checks()
        return
    subnet = ipaddress.IPv4Network(vm.args.network_subnet)
    client, gateway, dns = (str(subnet.network_address + n) for n in (100, 2, 3))
    boot = vm.wait_for(H.PROMPT, timeout=20)
    vm.check("Version: " + vm.args.version in boot, "Boot version matches candidate")
    vm.check("DHCP bound: IP=" + client in boot, "DHCP acquires requested emulated subnet")
    vm.check("gateway=" + gateway + " DNS=" + dns in boot, "DHCP learns gateway and DNS options")
    vm.command("lspci", ("vendor=0x8086 device=0x100e", "class=0x2"))
    vm.command("netinfo", ("E1000 ready=Yes link=Up DMA_pages=6 quarantine=No",
                           "IPv4 configured=Yes IP=" + client, "errors=0 timeouts=0"))
    if vm.args.disk:
        vm.report["disk_sha256_before"] = H.digest(vm.args.disk)
        vm.command("storage", ("AHCI ready=Yes readonly=Yes", "quarantine=No", "FAT32 mounted=Yes"))
        vm.command("disktest", ("Storage self-test: PASS", "snapshot_equal=Yes accounting=PASS"))
        if vm.args.expect_nx == "on":
            vm.command("exec /bin/diskread", ("diskread: PASS", "Exec: PASS"))
    with Fixture() as fixture:
        try:
            vm.command("ping gateway", ("Ping: sent=4 received=4 lost=0",))
            vm.command("resolve fixture.test gateway " + str(fixture.port),
                       ("DNS A fixture.test = 192.0.2.123",))
            vm.command("resolve invalid.fixture.test gateway " + str(fixture.port),
                       ("DNS resolution failed.",))
            steady = P.SCHED.memory_snapshots(vm, "Network baseline")
            for index in range(3):
                vm.command("nettest " + str(fixture.port),
                           ("Network stress: rounds=16 ping_dns_udp=PASS accounting=PASS",
                            "Network self-test: PASS"))
                current = P.SCHED.memory_snapshots(vm, "Network repeat")
                vm.check(all(current[0][key] == steady[0][key] for key in P.HEAP.STATE_FIELDS)
                         and current[1:] == steady[1:], "Network stress preserves exact PMM/heap/VMM accounting")
            vm.qmp("set_link", {"name": "utamo_nic", "up": False})
            vm.command("netinfo", ("link=Down", "quarantine=No"))
            vm.command("ping gateway", ("Ping: sent=4 received=0 lost=4",))
            vm.qmp("set_link", {"name": "utamo_nic", "up": True})
            vm.command("dhcp", ("DHCP bound: IP=" + client,))
            vm.command("ping gateway", ("Ping: sent=4 received=4 lost=0",))
            vm.command("resolve fixture.test gateway " + str(fixture.port),
                       ("DNS A fixture.test = 192.0.2.123",))
            vm.command("netinfo", ("E1000 ready=Yes link=Up DMA_pages=6 quarantine=No",
                                   "errors=0 timeouts=0"))
            final = P.SCHED.memory_snapshots(vm, "Network recovered")
            vm.check(all(final[0][key] == steady[0][key] for key in P.HEAP.STATE_FIELDS)
                     and final[1:] == steady[1:], "Link loss and DHCP reacquisition do not leak memory")
            vm.report["network_stress"] = {"runs":3, "rounds_per_run":16,
                                          "transactions_per_round":3}
        finally:
            vm.report["fixture"] = {"address":"127.0.0.1", "port":fixture.port,
                                   "events":fixture.events, "errors":fixture.errors}
    vm.check(not fixture.errors and not fixture.thread.is_alive(), "Local UDP/DNS fixture completed and stopped")
    vm.check(sum(e["kind"] == "dns-a" for e in fixture.events) == 50,
             "Fixture observed 50 real DNS queries and replies")
    vm.check(sum(e["kind"] == "udp-echo" for e in fixture.events) == 48,
             "Fixture observed 48 real generic UDP echo exchanges")
    vm.check(sum(e["kind"] == "dns-invalid-loop" for e in fixture.events) == 1,
             "Guest rejected a real malformed DNS compression reply")
    if vm.args.disk:
        vm.report["disk_sha256_after"] = H.digest(vm.args.disk)
        vm.check(vm.report["disk_sha256_before"] == vm.report["disk_sha256_after"],
                 "Combined networking and storage preserve the disk base")
    vm.command("heap", ("Heap integrity: OK",))
    vm.command("schedulerstats", ("Scheduler integrity: OK",))
    if vm.args.expect_nx == "on":
        vm.command("exec /bin/filetest", ("filetest: PASS", "Exec: PASS"))
    vm.check("UTAMO OS KERNEL EXCEPTION" not in vm.serial() and "UTAMO KERNEL PANIC" not in vm.serial(),
             "No kernel exception or panic during networking")
    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted.", start)
    vm.halt_checks()
    # Stop the capture writer before reading the final complete pcap.
    vm.close()
    counts = packet_counts(vm.directory / "network.pcap")
    vm.report["packet_counts"] = counts
    for key in ("dhcp_1", "dhcp_2", "dhcp_3", "dhcp_5"):
        vm.check(counts[key] >= 2, "Wire capture contains initial and repeated " + key)
    for key in ("arp_request", "arp_reply", "icmp_request", "icmp_reply"):
        vm.check(counts[key] > 0, "Wire capture contains " + key)
    vm.check(counts["icmp_reply"] >= 56 and counts["udp"] >= 200,
             "Wire capture confirms repeated ICMP and UDP traffic")

if __name__ == "__main__":
    P.suite = suite
    raise SystemExit(P.main())
