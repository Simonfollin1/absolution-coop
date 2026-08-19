#!/usr/bin/env python3
"""A headless second player for Absolution Co-op.

Speaks the mod's wire protocol - the one defined in mods/Coop/src/Net/Protocol.cpp
- well enough to stand in for a friend: it joins a hosted session, keeps its
link alive, walks in a circle next to the real player, announces a scene so the
"Go there" button lights up, drops markers, reaches checkpoints, and dies on
command. Everything a second machine would do, minus the second machine.

One file, standard library only. Run it on the same PC as the game:

    py coopbot.py 127.0.0.1 --name Bot

Host from the co-op panel first (F6 -> Host), then start the bot. Type `help`
at its prompt once it is connected. Everything it sees is also written to
coopbot.log next to it, in keeping with the house rule that nothing worth
knowing lives only on a screen.

The wire format is duplicated here by hand, so it can drift from the C++ if
nobody is watching. Somebody is: the build runs goldengen.cpp - the real codec
- and this file's --selftest against each other, byte for byte.
"""

import argparse
import math
import queue
import select
import socket
import struct
import sys
import threading
import time

# ---- Protocol constants, mirrored from Net/Protocol.h ----------------------

MAGIC            = 0x504F4F43      # 'COOP' on the wire
PROTOCOL_VERSION = 1
MAX_PACKET       = 1100
MAX_PEERS        = 8
HOST_PEER_ID     = 0
INVALID_PEER_ID  = 0xFF
MAX_NAME         = 24
MAX_EVENT_TEXT   = 160

MSG_HELLO   = 1
MSG_WELCOME = 2
MSG_REJECT  = 3
MSG_BYE     = 4
MSG_PING    = 5
MSG_PONG    = 6
MSG_STATE   = 7
MSG_ROSTER  = 8
MSG_EVENT   = 9
MSG_ACK     = 10

REJECT_REASONS = {1: "protocol mismatch", 2: "wrong password",
                  3: "session is full", 4: "banned"}

SF_HAS_POSITION = 1 << 0
SF_RUNNING      = 1 << 1
SF_DEAD         = 1 << 2
SF_LOADING      = 1 << 3
SF_IN_MENU      = 1 << 4

EV_CHAT               = 1
EV_MARKER             = 2
EV_ACTOR_DIED         = 3
EV_LEVEL_CHANGED      = 4
EV_CHECKPOINT_REACHED = 5
EV_OBJECTIVE_NOTE     = 6
EV_PLAYER_JOINED      = 7
EV_PLAYER_LEFT        = 8
EV_SESSION_RESET      = 9

EVENT_NAMES = {v: k for k, v in [
    ("Chat", EV_CHAT), ("Marker", EV_MARKER), ("ActorDied", EV_ACTOR_DIED),
    ("LevelChanged", EV_LEVEL_CHANGED), ("CheckpointReached", EV_CHECKPOINT_REACHED),
    ("ObjectiveNote", EV_OBJECTIVE_NOTE), ("PlayerJoined", EV_PLAYER_JOINED),
    ("PlayerLeft", EV_PLAYER_LEFT), ("SessionReset", EV_SESSION_RESET)]}

# Cadences, mirrored from Session.cpp. The bot keeps the same rhythm as the
# real thing so a session with it in looks like a session.
STATE_INTERVAL      = 0.050
PING_INTERVAL       = 1.0
HELLO_INTERVAL      = 0.5
HANDSHAKE_TIMEOUT   = 10.0
LINK_TIMEOUT        = 8.0
RETRANSMIT_INTERVAL = 0.25
SCENE_ANNOUNCE      = 3.0

HEADER = struct.Struct("<IHBBIH")           # magic, version, type, sender, session, reliableSeq
assert HEADER.size == 14


def fnv1a(password: str) -> int:
    """HashPassword from Protocol.cpp: FNV-1a over the raw bytes."""
    value = 2166136261
    for byte in password.encode("utf-8"):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


# ---- Encoding. Field order and widths come from Protocol.cpp, nothing else.

def pack_string(text: str, max_length: int) -> bytes:
    raw = text.encode("utf-8")[:max_length]
    return struct.pack("<B", len(raw)) + raw


def unpack_string(data: bytes, offset: int, max_length: int):
    if offset >= len(data):
        raise ValueError("truncated string length")
    length = data[offset]
    if length > max_length or offset + 1 + length > len(data):
        raise ValueError("malformed string")
    return data[offset + 1:offset + 1 + length].decode("utf-8", "replace"), offset + 1 + length


def pack_header(msg_type: int, sender: int, session: int, reliable_seq: int = 0) -> bytes:
    return HEADER.pack(MAGIC, PROTOCOL_VERSION, msg_type, sender, session, reliable_seq)


def unpack_header(data: bytes):
    if len(data) < HEADER.size:
        raise ValueError("short packet")
    magic, version, msg_type, sender, session, seq = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError("bad magic")
    if version != PROTOCOL_VERSION:
        raise ValueError(f"protocol v{version}, this bot speaks v{PROTOCOL_VERSION}")
    return msg_type, sender, session, seq, data[HEADER.size:]


def pack_hello(password_hash: int, fingerprint: int, name: str) -> bytes:
    return struct.pack("<II", password_hash, fingerprint) + pack_string(name, MAX_NAME)


def unpack_welcome(payload: bytes):
    return struct.unpack_from("<BIQIQ", payload)   # id, session, startMs, ruleFlags, hostNowMs


def pack_state(peer, ts, x, y, z, yaw, vx, vy, vz, flags, level, section, score=0) -> bytes:
    return struct.pack("<BIfffffffHBBH", peer, ts & 0xFFFFFFFF,
                       x, y, z, yaw, vx, vy, vz, flags, level, section, score)


def unpack_state(payload: bytes):
    return struct.unpack_from("<BIfffffffHBBH", payload)


def pack_event(ev_type, origin, ts, x, y, z, text) -> bytes:
    return (struct.pack("<BBIfff", ev_type, origin, ts & 0xFFFFFFFF, x, y, z)
            + pack_string(text, MAX_EVENT_TEXT))


def unpack_event(payload: bytes):
    ev_type, origin, ts, x, y, z = struct.unpack_from("<BBIfff", payload)
    text, _ = unpack_string(payload, struct.calcsize("<BBIfff"), MAX_EVENT_TEXT)
    return ev_type, origin, ts, x, y, z, text


def unpack_roster(payload: bytes):
    if not payload:
        raise ValueError("empty roster")
    count = payload[0]
    if count > MAX_PEERS:
        raise ValueError("oversized roster")
    entries, offset = [], 1
    for _ in range(count):
        peer = payload[offset]; offset += 1
        name, offset = unpack_string(payload, offset, MAX_NAME)
        level, section, flags = struct.unpack_from("<BBH", payload, offset); offset += 4
        entries.append((peer, name, level, section, flags))
    return entries


def pack_roster(entries) -> bytes:
    out = struct.pack("<B", len(entries))
    for peer, name, level, section, flags in entries:
        out += struct.pack("<B", peer) + pack_string(name, MAX_NAME)
        out += struct.pack("<BBH", level, section, flags)
    return out


# ---- The bot ----------------------------------------------------------------

class Bot:
    def __init__(self, args):
        self.args     = args
        self.address  = self._parse_endpoint(args.host, args.port)
        self.sock     = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", 0))
        self.sock.setblocking(False)

        self.peer_id    = INVALID_PEER_ID
        self.session_id = 0
        self.connected  = False
        self.running    = True

        self.next_seq  = 1                 # our reliable channel to the host
        self.pending   = {}                # seq -> [bytes, last_send, attempts]
        self.seen      = []                # host's reliable seqs, ring of 512
        self.last_heard = 0.0
        self.rtt_ms    = 0

        self.peers      = {}               # id -> dict(name/pos/level/section/flags/when)
        self.flags      = SF_HAS_POSITION
        self.level      = args.level
        self.section    = args.section
        self.scene      = args.scene
        self.checkpoint = args.checkpoint
        self.center     = list(args.pos)
        self.offset     = [2.5, 0.0, 0.0]
        self.shadow     = not args.no_shadow

        self.t0 = time.monotonic()
        self.timers = {"hello": 0.0, "state": 0.0, "ping": 0.0, "scene": 0.0}
        self.started = time.monotonic()

        self.commands = queue.Queue()
        self.logfile  = open(args.log, "a", encoding="utf-8")

    # -- plumbing --------------------------------------------------------

    @staticmethod
    def _parse_endpoint(host, default_port):
        if ":" in host:
            name, _, port = host.rpartition(":")
            return (name, int(port))
        return (host, default_port)

    def now_ms(self):
        return int((time.monotonic() - self.t0) * 1000)

    def log(self, line):
        stamp = time.strftime("%H:%M:%S")
        text = f"[{stamp}] {line}"
        print(text, flush=True)
        self.logfile.write(text + "\n")
        self.logfile.flush()

    def send(self, data: bytes):
        try:
            self.sock.sendto(data, self.address)
        except OSError as error:
            self.log(f"send failed: {error}")

    def send_unreliable(self, msg_type: int, payload: bytes):
        self.send(pack_header(msg_type, self.peer_id, self.session_id) + payload)

    def send_reliable(self, msg_type: int, payload: bytes):
        seq = self.next_seq
        self.next_seq = self.next_seq + 1
        if self.next_seq == 0 or self.next_seq > 0xFFFF:
            self.next_seq = 1
        packet = pack_header(msg_type, self.peer_id, self.session_id, seq) + payload
        self.pending[seq] = [packet, time.monotonic(), 1]
        self.send(packet)

    def send_event(self, ev_type, x=0.0, y=0.0, z=0.0, text=""):
        self.send_reliable(MSG_EVENT,
                           pack_event(ev_type, self.peer_id, self.now_ms(), x, y, z, text))

    def mark_seen(self, seq) -> bool:
        """Returns True when this seq is new. Mirrors Link::HasSeen/MarkSeen."""
        if seq in self.seen:
            return False
        self.seen.append(seq)
        if len(self.seen) > 512:
            self.seen.pop(0)
        return True

    # -- receive ---------------------------------------------------------

    def handle_packet(self, data: bytes):
        try:
            msg_type, sender, session, seq, payload = unpack_header(data)
        except ValueError as error:
            self.log(f"dropped a packet: {error}")
            return

        if not self.connected:
            if msg_type == MSG_WELCOME:
                peer, sess, start_ms, rule_flags, host_now = unpack_welcome(payload)
                self.peer_id, self.session_id = peer, sess
                self.connected  = True
                self.last_heard = time.monotonic()
                self.log(f"connected: peer id {peer}, session {sess:#x}")
                self.log("type `help` for commands")
            elif msg_type == MSG_REJECT:
                reason = REJECT_REASONS.get(payload[0] if payload else 0, "no reason given")
                self.log(f"host refused the connection: {reason}")
                self.running = False
            return

        if session != self.session_id:
            return

        self.last_heard = time.monotonic()

        if msg_type == MSG_PING:
            (stamp,) = struct.unpack_from("<Q", payload)
            self.send_unreliable(MSG_PONG, struct.pack("<Q", stamp))
        elif msg_type == MSG_PONG:
            (stamp,) = struct.unpack_from("<Q", payload)
            if stamp <= self.now_ms():
                self.rtt_ms = self.now_ms() - stamp
        elif msg_type == MSG_ACK:
            (acked,) = struct.unpack_from("<H", payload)
            self.pending.pop(acked, None)
        elif msg_type == MSG_STATE:
            state = unpack_state(payload)
            self.record_state(sender, state)
        elif msg_type == MSG_ROSTER:
            self.send_unreliable(MSG_ACK, struct.pack("<H", seq))
            if self.mark_seen(seq):
                self.handle_roster(unpack_roster(payload))
        elif msg_type == MSG_EVENT:
            self.send_unreliable(MSG_ACK, struct.pack("<H", seq))
            if self.mark_seen(seq):
                self.handle_event(unpack_event(payload))
        elif msg_type == MSG_BYE:
            self.log("the host closed the session")
            self.running = False

    def record_state(self, sender, state):
        (_, ts, x, y, z, yaw, vx, vy, vz, flags, level, section, score) = state
        fresh = sender not in self.peers
        self.peers.setdefault(sender, {"name": f"peer {sender}"}).update(
            x=x, y=y, z=z, yaw=yaw, flags=flags, level=level, section=section,
            when=time.monotonic())
        if fresh:
            self.log(f"seeing state from peer {sender} "
                     f"(chapter {level}, section {section})")

    def handle_roster(self, entries):
        names = {peer: name for peer, name, *_ in entries}
        for peer, name in names.items():
            self.peers.setdefault(peer, {}).update(name=name)
        gone = [p for p in self.peers if p not in names and p != self.peer_id]
        for peer in gone:
            self.peers.pop(peer, None)
        listed = ", ".join(f"{p}:{n}" for p, n in sorted(names.items()))
        self.log(f"roster: {listed}")

    def handle_event(self, event):
        ev_type, origin, ts, x, y, z, text = event
        if origin == self.peer_id:
            return
        name = self.peers.get(origin, {}).get("name", f"peer {origin}")
        kind = EVENT_NAMES.get(ev_type, f"type {ev_type}")
        detail = f' "{text}"' if text else ""
        position = f" at ({x:.1f}, {y:.1f}, {z:.1f})" if ev_type in (EV_MARKER, EV_ACTOR_DIED) else ""
        self.log(f"event from {name}: {kind}{detail}{position}")

    # -- behaviour -------------------------------------------------------

    def shadow_target(self):
        """The freshest peer with a real position - the one to walk beside."""
        best, best_when = None, 0.0
        for peer, info in self.peers.items():
            if peer == self.peer_id or "when" not in info:
                continue
            if not (info.get("flags", 0) & SF_HAS_POSITION):
                continue
            if info["when"] > best_when:
                best, best_when = info, info["when"]
        if best and time.monotonic() - best_when < 3.0:
            return best
        return None

    def tick(self, now):
        if not self.connected:
            if now - self.started > HANDSHAKE_TIMEOUT:
                self.log(f"no answer from {self.address[0]}:{self.address[1]} - "
                         "is the game hosting?")
                self.running = False
                return
            if now - self.timers["hello"] >= HELLO_INTERVAL:
                self.timers["hello"] = now
                packet = pack_header(MSG_HELLO, INVALID_PEER_ID, 0)
                packet += pack_hello(fnv1a(self.args.password), 0, self.args.name)
                self.send(packet)
            return

        if now - self.last_heard > LINK_TIMEOUT:
            self.log("nothing from the host for eight seconds - link is dead")
            self.running = False
            return

        if now - self.timers["state"] >= STATE_INTERVAL:
            self.timers["state"] = now
            self.send_state(now)

        if now - self.timers["ping"] >= PING_INTERVAL:
            self.timers["ping"] = now
            self.send_unreliable(MSG_PING, struct.pack("<Q", self.now_ms()))

        if self.scene and now - self.timers["scene"] >= SCENE_ANNOUNCE:
            self.timers["scene"] = now
            self.send_event(EV_LEVEL_CHANGED, x=float(self.checkpoint), text=self.scene)

        for seq, entry in list(self.pending.items()):
            if now - entry[1] >= RETRANSMIT_INTERVAL:
                entry[1] = now
                entry[2] += 1
                self.send(entry[0])

    def send_state(self, now):
        target = self.shadow_target() if self.shadow else None

        if target:
            center = (target["x"] + self.offset[0],
                      target["y"] + self.offset[1],
                      target["z"] + self.offset[2])
            level, section = target["level"], target["section"]
        else:
            center = tuple(self.center)
            level, section = self.level, self.section

        # A slow, watchable circle: obviously alive, never in the way.
        angle = (now - self.started) * 0.6
        radius = self.args.radius
        x = center[0] + math.cos(angle) * radius
        y = center[1] + math.sin(angle) * radius
        z = center[2]
        yaw = angle + math.pi / 2            # facing along the motion
        vx = -math.sin(angle) * radius * 0.6
        vy = math.cos(angle) * radius * 0.6

        self.send_unreliable(MSG_STATE, pack_state(
            self.peer_id, self.now_ms(), x, y, z, yaw, vx, vy, 0.0,
            self.flags, level, section))

    # -- console ---------------------------------------------------------

    HELP = """commands:
  say <text>           chat line
  marker [x y z]       drop a marker (default: where the bot is standing)
  checkpoint <n>       announce reaching checkpoint n in the current chapter
  scene <path> [n]     announce being in a scene (lights up Go there); n = checkpoint
  scene off            stop announcing
  die / revive         set or clear the dead flag (die also sends the death note)
  level <a> [b]        chapter / section bytes to claim when nobody is shadowed
  where <x> <y> <z>    orbit centre when nobody is shadowed
  offset <x> <y> <z>   how far from the shadowed player to walk
  shadow on|off        follow the freshest real player, or stay at the centre
  loading on|off       set or clear the loading flag
  peers                who the bot can see, and how fresh
  rtt                  last measured round trip to the host
  leave                say goodbye and exit"""

    def handle_command(self, line):
        parts = line.split()
        if not parts:
            return
        word, rest = parts[0].lower(), parts[1:]

        try:
            if word == "help":
                for row in self.HELP.splitlines():
                    self.log(row)
            elif word == "say":
                self.send_event(EV_CHAT, text=" ".join(rest))
            elif word == "marker":
                if len(rest) >= 3:
                    x, y, z = (float(v) for v in rest[:3])
                else:
                    x, y, z = self.last_sent_position()
                self.send_event(EV_MARKER, x=x, y=y, z=z)
                self.log(f"marker at ({x:.1f}, {y:.1f}, {z:.1f})")
            elif word == "checkpoint":
                n = int(rest[0])
                self.send_event(EV_CHECKPOINT_REACHED, x=float(n), y=float(self.level),
                                text=f"checkpoint {n}")
                self.log(f"announced checkpoint {n}")
            elif word == "scene":
                if rest and rest[0].lower() == "off":
                    self.scene = None
                    self.log("stopped announcing a scene")
                else:
                    self.scene = rest[0]
                    self.checkpoint = int(rest[1]) if len(rest) > 1 else 0
                    self.timers["scene"] = 0.0
                    self.log(f"announcing {self.scene} at checkpoint {self.checkpoint}")
            elif word == "die":
                self.flags |= SF_DEAD
                self.send_event(EV_OBJECTIVE_NOTE, text="died")
                self.log("down (flag set, death note sent)")
            elif word == "revive":
                self.flags &= ~SF_DEAD
                self.log("back up")
            elif word == "level":
                self.level = int(rest[0])
                if len(rest) > 1:
                    self.section = int(rest[1])
                self.log(f"claiming chapter {self.level}, section {self.section}")
            elif word == "where":
                self.center = [float(v) for v in rest[:3]]
                self.log(f"orbit centre {self.center}")
            elif word == "offset":
                self.offset = [float(v) for v in rest[:3]]
                self.log(f"shadow offset {self.offset}")
            elif word == "shadow":
                self.shadow = rest[0].lower() != "off"
                self.log(f"shadowing {'on' if self.shadow else 'off'}")
            elif word == "loading":
                if rest[0].lower() == "on":
                    self.flags |= SF_LOADING
                else:
                    self.flags &= ~SF_LOADING
                self.log(f"loading flag {'set' if self.flags & SF_LOADING else 'clear'}")
            elif word == "peers":
                now = time.monotonic()
                for peer, info in sorted(self.peers.items()):
                    age = now - info["when"] if "when" in info else None
                    freshness = f"{age:.1f}s ago" if age is not None else "no state yet"
                    self.log(f"  {peer}: {info.get('name', '?')} - {freshness}")
                if not self.peers:
                    self.log("  nobody yet")
            elif word == "rtt":
                self.log(f"round trip {self.rtt_ms} ms")
            elif word in ("leave", "quit", "exit"):
                self.send(pack_header(MSG_BYE, self.peer_id, self.session_id))
                self.log("left the session")
                self.running = False
            else:
                self.log(f"unknown command '{word}' - try help")
        except (ValueError, IndexError):
            self.log(f"could not parse that - try help")

    def last_sent_position(self):
        target = self.shadow_target() if self.shadow else None
        if target:
            return (target["x"] + self.offset[0], target["y"] + self.offset[1],
                    target["z"] + self.offset[2])
        return tuple(self.center)

    # -- main loop -------------------------------------------------------

    def run(self):
        self.log(f"joining {self.address[0]}:{self.address[1]} as {self.args.name!r}")

        reader = threading.Thread(target=self._stdin_loop, daemon=True)
        reader.start()

        while self.running:
            readable, _, _ = select.select([self.sock], [], [], 0.02)
            for _ in readable:
                while True:
                    try:
                        data, sender = self.sock.recvfrom(MAX_PACKET + 100)
                    except BlockingIOError:
                        break
                    except OSError:
                        break
                    if sender[1] == self.address[1]:
                        try:
                            self.handle_packet(data)
                        except (ValueError, struct.error) as error:
                            self.log(f"dropped a malformed packet: {error}")

            while not self.commands.empty():
                self.handle_command(self.commands.get_nowait())

            self.tick(time.monotonic())

        self.logfile.close()

    def _stdin_loop(self):
        for line in sys.stdin:
            self.commands.put(line.strip())


# ---- Self test ---------------------------------------------------------------
#
# Two layers. The roundtrip layer needs nothing: everything this file encodes,
# it must decode back unchanged. The golden layer takes a file produced by
# goldengen.cpp - the actual C++ codec, compiled and run - and requires this
# file to produce the identical bytes and to read them back to the same values.
# Every constant below matches a sample in goldengen.cpp; change one and the
# other must follow.

GOLDEN_SAMPLES = {
    "header":  lambda: pack_header(MSG_EVENT, 3, 0xDEADBEEF, 0x1234),
    "hello":   lambda: pack_hello(fnv1a("hemligt"), 0x11223344, "Agent 47"),
    "welcome": lambda: struct.pack("<BIQIQ", 2, 0xCAFEBABE, 123456789, 7, 987654321),
    "state":   lambda: pack_state(1, 100000, 1.5, -2.25, 3.75, 0.5,
                                  0.125, 0.25, -0.375, 3, 4, 2, 41),
    "roster":  lambda: pack_roster([(0, "Host", 0xFF, 0xFF, 0),
                                    (5, "Agent 47", 4, 2, 3)]),
    "event":   lambda: pack_event(EV_LEVEL_CHANGED, 1, 4242, 13.0, 0.0, 0.0,
                                  "assembly:/scenes/m04/scene_rfyl.entity"),
}


def selftest(golden_path):
    failures = 0

    def check(name, condition, detail=""):
        nonlocal failures
        if condition:
            print(f"ok   {name}")
        else:
            failures += 1
            print(f"FAIL {name}  {detail}")

    # Roundtrips.
    header_bytes = GOLDEN_SAMPLES["header"]()
    msg_type, sender, session, seq, rest = unpack_header(header_bytes)
    check("header roundtrip",
          (msg_type, sender, session, seq, rest) == (MSG_EVENT, 3, 0xDEADBEEF, 0x1234, b""))

    state_bytes = GOLDEN_SAMPLES["state"]()
    state = unpack_state(state_bytes)
    check("state roundtrip",
          state == (1, 100000, 1.5, -2.25, 3.75, 0.5, 0.125, 0.25, -0.375, 3, 4, 2, 41),
          repr(state))

    event_bytes = GOLDEN_SAMPLES["event"]()
    event = unpack_event(event_bytes)
    check("event roundtrip",
          event == (EV_LEVEL_CHANGED, 1, 4242, 13.0, 0.0, 0.0,
                    "assembly:/scenes/m04/scene_rfyl.entity"), repr(event))

    roster_bytes = GOLDEN_SAMPLES["roster"]()
    roster = unpack_roster(roster_bytes)
    check("roster roundtrip",
          roster == [(0, "Host", 0xFF, 0xFF, 0), (5, "Agent 47", 4, 2, 3)], repr(roster))

    welcome = unpack_welcome(GOLDEN_SAMPLES["welcome"]())
    check("welcome roundtrip", welcome == (2, 0xCAFEBABE, 123456789, 7, 987654321))

    check("password hash", fnv1a("") == 2166136261)

    # The C++ codec's bytes, if provided.
    if golden_path:
        golden = {}
        with open(golden_path, encoding="ascii") as handle:
            for line in handle:
                if "=" in line:
                    name, hexdata = line.strip().split("=", 1)
                    golden[name] = bytes.fromhex(hexdata)

        for name, build in GOLDEN_SAMPLES.items():
            check(f"golden {name} present", name in golden)
            if name in golden:
                ours = build()
                check(f"golden {name} bytes match C++",
                      ours == golden[name],
                      f"python {ours.hex()} vs c++ {golden[name].hex()}")

    print("selftest:", "FAILED" if failures else "passed",
          f"({failures} failures)" if failures else "")
    return 1 if failures else 0


# ---- Entry -------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="A headless second player for Absolution Co-op.")
    parser.add_argument("host", nargs="?", default="127.0.0.1",
                        help="host address, with optional :port (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=47474,
                        help="port when the address does not carry one")
    parser.add_argument("--name", default="Bot", help="player name (max 24 bytes)")
    parser.add_argument("--password", default="", help="session password")
    parser.add_argument("--scene", default=None,
                        help="scene resource path to announce, lighting up Go there")
    parser.add_argument("--checkpoint", type=int, default=0,
                        help="checkpoint index announced with --scene")
    parser.add_argument("--level", type=int, default=0xFF,
                        help="chapter byte to claim when nobody is shadowed")
    parser.add_argument("--section", type=int, default=0xFF,
                        help="section byte to claim when nobody is shadowed")
    parser.add_argument("--pos", type=float, nargs=3, default=[0.0, 0.0, 0.0],
                        metavar=("X", "Y", "Z"), help="orbit centre without a shadow target")
    parser.add_argument("--radius", type=float, default=1.8, help="orbit radius in metres")
    parser.add_argument("--no-shadow", action="store_true",
                        help="do not follow the real player; stay at --pos")
    parser.add_argument("--log", default="coopbot.log", help="log file (appended)")
    parser.add_argument("--selftest", action="store_true",
                        help="verify the codec and exit")
    parser.add_argument("--golden", default=None,
                        help="with --selftest: file of name=hex lines from goldengen")

    args = parser.parse_args()

    if args.selftest:
        sys.exit(selftest(args.golden))

    bot = Bot(args)

    try:
        bot.run()
    except KeyboardInterrupt:
        bot.send(pack_header(MSG_BYE, bot.peer_id, bot.session_id))
        print()


if __name__ == "__main__":
    main()
