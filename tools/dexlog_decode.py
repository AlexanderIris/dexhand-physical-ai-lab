#!/usr/bin/env python3
"""Decoder and completeness checker for dexhand-reference .dexlog files.

The tool intentionally uses only the Python standard library so it can run in a
bare CI container or on a laptop without ROS/MCAP dependencies.
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Dict, Iterable, Iterator, List, Optional, Tuple

MAGIC0 = 0xA5
MAGIC1 = 0x5A
VERSION = 1
HEADER_SIZE = 28
CRC_SIZE = 2
MAX_PAYLOAD = 512

FRAME_NAMES = {
    0x01: "session_metadata",
    0x10: "control_sample",
    0x11: "sensor_batch",
    0x20: "event",
    0x21: "clock_sync",
    0x30: "gap_marker",
}

CONTROL_MODE_NAMES = {0: "disabled", 1: "position_p", 2: "safe_hold"}
SAFETY_NAMES = {0: "ok", 1: "warn", 2: "fault"}


class DexLogError(RuntimeError):
    pass


def crc16_ccitt_false(data: bytes, crc: int = 0xFFFF) -> int:
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def build_frame(frame_type: int, seq: int, device_ns: int, host_ns: int,
                payload: bytes = b"", flags: int = 0) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    header = struct.pack(
        "<6BIQQH",
        MAGIC0, MAGIC1, VERSION, frame_type, flags, 0,
        seq & 0xFFFFFFFF, device_ns & 0xFFFFFFFFFFFFFFFF,
        host_ns & 0xFFFFFFFFFFFFFFFF, len(payload),
    )
    body = header + payload
    crc = crc16_ccitt_false(body)
    return body + struct.pack("<H", crc)


def _read_exact(fp: BinaryIO, size: int, eof_ok: bool = False) -> Optional[bytes]:
    data = fp.read(size)
    if not data and eof_ok:
        return None
    if len(data) != size:
        raise DexLogError(f"truncated file: wanted {size} bytes, got {len(data)}")
    return data


@dataclass
class Frame:
    frame_type: int
    seq: int
    device_ns: int
    host_ns: int
    flags: int
    payload: bytes
    crc_ok: bool = True
    decoded: object = None

    @property
    def type_name(self) -> str:
        return FRAME_NAMES.get(self.frame_type, f"unknown_0x{self.frame_type:02x}")


def parse_control_payload(payload: bytes) -> Dict[str, object]:
    if len(payload) < 16:
        raise DexLogError("control payload shorter than 16 bytes")
    pver, count, safety, mode, cycle, budget_us, cycle_time_us, dropped = struct.unpack_from(
        "<4B I H H I", payload, 0
    )
    expected = 16 + count * 20
    if pver != 1 or count > 20 or len(payload) != expected:
        raise DexLogError(f"bad control payload header/length: version={pver} count={count}")

    joints: List[Dict[str, int]] = []
    off = 16
    for _ in range(count):
        target, position, velocity, current, temp, flags = struct.unpack_from(
            "<iiiiH H", payload, off
        )
        joints.append({
            "target_mrad": target,
            "position_mrad": position,
            "velocity_mrad_s": velocity,
            "current_ma": current,
            "temperature_cdeg": temp if temp < 32768 else temp - 65536,
            "flags": flags,
        })
        off += 20
    return {
        "version": pver,
        "joint_count": count,
        "safety_level": safety,
        "safety": SAFETY_NAMES.get(safety, str(safety)),
        "control_mode": mode,
        "mode": CONTROL_MODE_NAMES.get(mode, str(mode)),
        "cycle": cycle,
        "cycle_budget_us": budget_us,
        "cycle_time_us": cycle_time_us,
        "recorder_dropped": dropped,
        "joints": joints,
    }


def parse_event_payload(payload: bytes) -> Dict[str, object]:
    if len(payload) < 8:
        raise DexLogError("event payload too short")
    code, severity, _reserved, cycle = struct.unpack_from("<HBB I", payload, 0)
    message = payload[8:].decode("utf-8", errors="replace")
    return {"event_code": code, "severity": severity, "cycle": cycle, "message": message}


def parse_gap_payload(payload: bytes) -> Dict[str, int]:
    if len(payload) != 16:
        raise DexLogError("gap payload must be 16 bytes")
    dropped, pending, cycle, _reserved = struct.unpack("<IIII", payload)
    return {"dropped_frames": dropped, "pending_dropped": pending, "cycle": cycle}


def read_frames(path: str | Path) -> Iterator[Frame]:
    with Path(path).open("rb") as fp:
        while True:
            header = _read_exact(fp, HEADER_SIZE, eof_ok=True)
            if header is None:
                return
            magic0, magic1, version, frame_type, flags, _rsv, seq, dev_ns, host_ns, plen = struct.unpack(
                "<6BIQQH", header
            )
            if magic0 != MAGIC0 or magic1 != MAGIC1 or version != VERSION:
                raise DexLogError("bad frame magic/version")
            if plen > MAX_PAYLOAD:
                raise DexLogError("payload length exceeds limit")

            rest = _read_exact(fp, plen + CRC_SIZE)
            assert rest is not None
            payload = rest[:plen]
            stored_crc = struct.unpack("<H", rest[plen:])[0]
            calc_crc = crc16_ccitt_false(header + payload)
            crc_ok = stored_crc == calc_crc
            frame = Frame(frame_type, seq, dev_ns, host_ns, flags, payload, crc_ok)

            if crc_ok:
                if frame_type == 0x10:
                    frame.decoded = parse_control_payload(payload)
                elif frame_type == 0x20:
                    frame.decoded = parse_event_payload(payload)
                elif frame_type == 0x30:
                    frame.decoded = parse_gap_payload(payload)
                elif frame_type == 0x01:
                    frame.decoded = payload.decode("utf-8", errors="replace")
            yield frame


@dataclass
class Summary:
    path: str
    total_frames: int = 0
    crc_errors: int = 0
    counts: Dict[str, int] = field(default_factory=dict)
    seq_gaps: int = 0
    missing_control_cycles: List[int] = field(default_factory=list)
    first_device_ns: Optional[int] = None
    last_device_ns: Optional[int] = None
    first_host_ns: Optional[int] = None
    last_host_ns: Optional[int] = None
    control_count: int = 0
    first_cycle: Optional[int] = None
    last_cycle: Optional[int] = None
    max_cycle_time_us: int = 0
    deadline_miss_samples: int = 0
    fault_samples: int = 0
    gap_markers: int = 0
    gap_dropped_total: int = 0
    events: List[str] = field(default_factory=list)
    metadata: List[str] = field(default_factory=list)

    @property
    def duration_s(self) -> Optional[float]:
        if self.first_device_ns is None or self.last_device_ns is None:
            return None
        return max(0, self.last_device_ns - self.first_device_ns) / 1e9


def summarize(frames: Iterable[Frame]) -> Tuple[Summary, List[Frame]]:
    summary = Summary(path="")
    materialized: List[Frame] = []
    expected_seq: Optional[int] = None
    expected_cycle: Optional[int] = None

    for frame in frames:
        materialized.append(frame)
        summary.total_frames += 1
        name = frame.type_name
        summary.counts[name] = summary.counts.get(name, 0) + 1

        if not frame.crc_ok:
            summary.crc_errors += 1
            continue

        if expected_seq is not None and frame.seq != expected_seq:
            summary.seq_gaps += max(0, frame.seq - expected_seq)
        expected_seq = frame.seq + 1

        summary.first_device_ns = summary.first_device_ns if summary.first_device_ns is not None else frame.device_ns
        summary.last_device_ns = frame.device_ns
        summary.first_host_ns = summary.first_host_ns if summary.first_host_ns is not None else frame.host_ns
        summary.last_host_ns = frame.host_ns

        if frame.frame_type == 0x01 and isinstance(frame.decoded, str):
            summary.metadata.append(frame.decoded)
        elif frame.frame_type == 0x20 and isinstance(frame.decoded, dict):
            d = frame.decoded
            summary.events.append(f"cycle={d['cycle']} code={d['event_code']} {d['message']}")
        elif frame.frame_type == 0x30 and isinstance(frame.decoded, dict):
            d = frame.decoded
            summary.gap_markers += 1
            summary.gap_dropped_total = max(summary.gap_dropped_total, int(d["dropped_frames"]))
        elif frame.frame_type == 0x10 and isinstance(frame.decoded, dict):
            d = frame.decoded
            cycle = int(d["cycle"])
            summary.control_count += 1
            summary.first_cycle = summary.first_cycle if summary.first_cycle is not None else cycle
            summary.last_cycle = cycle
            summary.max_cycle_time_us = max(summary.max_cycle_time_us, int(d["cycle_time_us"]))
            if int(d["cycle_time_us"]) > int(d["cycle_budget_us"]):
                summary.deadline_miss_samples += 1
            if d["safety_level"] != 0:
                summary.fault_samples += 1
            if expected_cycle is not None and cycle != expected_cycle:
                summary.missing_control_cycles.extend(range(expected_cycle, cycle))
            expected_cycle = cycle + 1

    return summary, materialized


def print_summary(s: Summary) -> None:
    print(f"file: {s.path}")
    print(f"frames: total={s.total_frames} crc_errors={s.crc_errors} seq_gaps={s.seq_gaps}")
    print("frame_counts: " + ", ".join(f"{k}={v}" for k, v in sorted(s.counts.items())))
    if s.control_count:
        missing = len(s.missing_control_cycles)
        print(
            f"control: samples={s.control_count} cycles={s.first_cycle}..{s.last_cycle} "
            f"missing_cycles={missing} max_cycle_time_us={s.max_cycle_time_us} "
            f"deadline_miss_samples={s.deadline_miss_samples} fault_samples={s.fault_samples}"
        )
    if s.duration_s is not None:
        print(f"duration_device_s: {s.duration_s:.6f}")
    print(f"recording_gaps: markers={s.gap_markers} cumulative_dropped={s.gap_dropped_total}")
    for meta in s.metadata:
        print(f"metadata: {meta}")
    for event in s.events:
        print(f"event: {event}")

    healthy = (
        s.crc_errors == 0 and s.seq_gaps == 0 and not s.missing_control_cycles
        and s.gap_markers == 0 and s.gap_dropped_total == 0
    )
    print("completeness:", "PASS" if healthy else "DEGRADED_OR_INCOMPLETE")


def write_csv(frames: List[Frame], path: str) -> int:
    control_frames = [f for f in frames if f.frame_type == 0x10 and isinstance(f.decoded, dict)]
    if not control_frames:
        return 0
    max_joints = max(int(f.decoded["joint_count"]) for f in control_frames)  # type: ignore[index]
    header = [
        "seq", "device_ns", "host_ns", "cycle", "safety", "mode",
        "cycle_budget_us", "cycle_time_us", "recorder_dropped",
    ]
    for i in range(max_joints):
        header.extend([
            f"target_{i}_mrad", f"actual_{i}_mrad", f"velocity_{i}_mrad_s",
            f"current_{i}_ma", f"temp_{i}_cdeg", f"flags_{i}",
        ])

    with open(path, "w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        writer.writerow(header)
        for frame in control_frames:
            d = frame.decoded
            row = [
                frame.seq, frame.device_ns, frame.host_ns, d["cycle"], d["safety"], d["mode"],
                d["cycle_budget_us"], d["cycle_time_us"], d["recorder_dropped"],
            ]
            for joint in d["joints"]:
                row.extend([
                    joint["target_mrad"], joint["position_mrad"], joint["velocity_mrad_s"],
                    joint["current_ma"], joint["temperature_cdeg"], joint["flags"],
                ])
            for _ in range(max_joints - int(d["joint_count"])):
                row.extend([""] * 6)
            writer.writerow(row)
    return len(control_frames)


def write_jsonl(frames: List[Frame], path: str) -> None:
    with open(path, "w", encoding="utf-8") as fp:
        for frame in frames:
            obj = {
                "type": frame.type_name,
                "type_id": frame.frame_type,
                "seq": frame.seq,
                "device_ns": frame.device_ns,
                "host_ns": frame.host_ns,
                "crc_ok": frame.crc_ok,
                "payload": frame.decoded if frame.decoded is not None else frame.payload.hex(),
            }
            fp.write(json.dumps(obj, ensure_ascii=False, sort_keys=True) + "\n")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Decode dexhand-reference binary logs")
    parser.add_argument("logfile", help="path to .dexlog file")
    parser.add_argument("--csv", help="write control samples to CSV")
    parser.add_argument("--jsonl", help="write all frames to JSONL")
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero when CRC, sequence or control-cycle completeness fails")
    args = parser.parse_args(argv)

    try:
        summary, frames = summarize(read_frames(args.logfile))
    except (OSError, DexLogError, struct.error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    summary.path = args.logfile
    print_summary(summary)
    if args.csv:
        count = write_csv(frames, args.csv)
        print(f"csv: wrote {count} control rows to {args.csv}")
    if args.jsonl:
        write_jsonl(frames, args.jsonl)
        print(f"jsonl: wrote {len(frames)} frames to {args.jsonl}")

    if args.strict:
        incomplete = (
            summary.crc_errors or summary.seq_gaps or summary.missing_control_cycles
            or summary.gap_markers or summary.gap_dropped_total
        )
        return 1 if incomplete else 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
