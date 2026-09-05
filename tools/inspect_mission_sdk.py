#!/usr/bin/env python3
"""Inspect installed SDK topology without copying game data into the source tree.

Layouts come from Sunrise/src/state/activity_sdk/format.h (version 37).
The JSON report is local generated evidence, not a distributable mission asset.
"""

import argparse
import collections
import hashlib
import json
import mmap
from pathlib import Path
import struct


SECTIONS = {
    "strings": (0, 1), "scenarios": (2, 48), "bubbles": (3, 40),
    "states": (4, 64), "objects": (5, 52), "occurrences": (6, 56),
    "slots": (7, 80), "actors": (12, 68), "squads": (16, 52), "members": (17, 44),
}
SQUAD_FLAGS = {
    1: "source_descriptor_exact", 2: "spawner_rule_edge_exact",
    4: "scenario_occurrence_exact", 8: "all_points_exact",
    16: "member_count_valid", 32: "candidate_counts_invariant_complete",
}


class Pack:
    def __init__(self, path):
        with open(path, "rb") as source:
            self.data = mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            self.validate()
        except Exception:
            self.data.close()
            raise

    def validate(self):
        data = self.data
        if len(data) < 848 or data[:8] != b"SRSDKP01":
            raise ValueError("not a runtime activity SDK pack")
        version, header, size = struct.unpack_from("<IIQ", data, 8)
        if version != 37 or header != 848 or size != len(data):
            raise ValueError("unsupported SDK version/header or truncated file")
        if struct.unpack_from("<I", data, 152)[0] != 43:
            raise ValueError("unexpected SDK section count")
        payload = memoryview(data)[header:]
        digest = hashlib.sha256(payload).digest()
        payload.release()
        if digest != data[24:56]:
            raise ValueError("SDK payload checksum mismatch")
        self.sections = [struct.unpack_from("<QII", data, 160 + i * 16)
                         for i in range(43)]
        for offset, count, stride in self.sections:
            if count and (offset < header or stride == 0 or offset + count * stride > size):
                raise ValueError("SDK section exceeds file bounds")
        for name, (index, expected) in SECTIONS.items():
            if self.sections[index][2] != expected:
                raise ValueError(f"unexpected {name} stride")
        self.build_id = "sha256:" + data[56:88].hex()

    def close(self):
        self.data.close()

    def rows(self, name):
        index, _ = SECTIONS[name]
        offset, count, stride = self.sections[index]
        if stride % 4:
            raise ValueError("section is not composed of u32 fields")
        return [struct.unpack_from("<" + "I" * (stride // 4), self.data, offset + i * stride)
                for i in range(count)]

    def text(self, row, index=0):
        start, length = row[index:index + 2]
        bank, count, _ = self.sections[0]
        if start + length > count:
            raise ValueError("string exceeds SDK string bank")
        return self.data[bank + start:bank + start + length].decode("utf-8")

    def inspect(self, tag):
        scenarios = self.rows("scenarios")
        matches = [i for i, row in enumerate(scenarios) if row[0] == tag]
        if len(matches) != 1:
            raise ValueError("scenario tag must resolve exactly once")
        scenario_index = matches[0]
        bubbles, states = self.rows("bubbles"), self.rows("states")
        objects, slots = self.rows("objects"), self.rows("slots")
        occurrences = [row for row in self.rows("occurrences") if row[8] == scenario_index]
        squad_rows = [row for row in self.rows("squads") if row[2] == scenario_index]
        members, actors = self.rows("members"), self.rows("actors")
        squads_by_slot = collections.defaultdict(list)
        for row in squad_rows:
            selected = members[row[9]:row[9] + row[10]]
            definition_ready = row[7] & 63 == 63 and 1 <= row[10] <= 15
            profiles = set()
            actors_exact = len(selected) == row[10]
            for member in selected:
                if member[10] >= 0x80000000:
                    actors_exact = False
                elif member[10] > 0:
                    if not member[6] & 1 or member[5] >= len(actors):
                        actors_exact = False
                    else:
                        profiles.add(actors[member[5]][-1])
            profile_ready = actors_exact and len(profiles) == 1
            squads_by_slot[row[4]].append({
                "id": self.text(row), "flags": row[7], "member_count": row[10],
                "anchor_count": row[12], "occurrence_index": row[8],
                "spawner_tag": f"{row[5]:08x}", "rule_tag": f"{row[6]:08x}",
                "definition_ready": definition_ready, "spawn_profile_ready": profile_ready,
                "runnable": definition_ready and profile_ready,
                "missing_flags": [name for bit, name in SQUAD_FLAGS.items() if not row[7] & bit],
            })
        mission_objects = {row[11] for row in occurrences}
        mission_slots = {i: row for i, row in enumerate(slots) if row[8] in mission_objects}
        state_reports = []
        for index, row in enumerate(states):
            if row[6] != scenario_index:
                continue
            owned = {occ[11] for occ in occurrences if occ[10] == index}
            entries = []
            for slot_index, slot in mission_slots.items():
                if slot[8] not in owned:
                    continue
                entries.append({"id": self.text(slot), "name": self.text(slot, 2),
                                "type": slot[10], "object_tag": f"{objects[slot[8]][2]:08x}",
                                "squads": squads_by_slot.get(slot_index, [])})
            state_reports.append({
                "id": self.text(row), "bubble": self.text(bubbles[row[7]], 2),
                "region_index": row[10] + row[8], "map_bubble_index": row[11],
                "registry_tag": f"{row[15]:08x}", "slots": entries,
            })
        squad_slots = {i: row for i, row in mission_slots.items() if row[10] == 1}
        missing = [self.text(row) for i, row in squad_slots.items() if i not in squads_by_slot]
        runnable = sum(q["runnable"] for group in squads_by_slot.values() for q in group)
        return {
            "scenario": f"{tag:08x}", "name": self.text(scenarios[scenario_index], 4),
            "sdk_build_id": self.build_id,
            "counts": {"states": len(state_reports), "slots": len(mission_slots),
                       "squad_sensors": len(squad_slots), "squad_definitions": len(squad_rows),
                       "runnable_squads": runnable, "sensors_without_squad_definition": len(missing)},
            "readiness_scope": "static definition and default actor-profile gates; live leases, placement transport and AI require an in-game test",
            "sensors_without_squad_definition": missing, "states": state_reports,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack", type=Path, help="installed Sunrise/activity_sdk.pack")
    parser.add_argument("--scenario", type=lambda value: int(value, 16), default=0x80B3C09E)
    parser.add_argument("--output", type=Path, required=True, help="local generated JSON path")
    parser.add_argument("--shard", type=Path, help="matching generated-world scenario pack")
    args = parser.parse_args()
    pack = Pack(args.pack)
    try:
        report = pack.inspect(args.scenario)
    finally:
        pack.close()
    if args.shard:
        report["world_shard"] = inspect_shard(args.shard, args.scenario)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["counts"], indent=2))


def inspect_shard(path, scenario):
    """Check v13 shard identity and report whether authored squad contexts were exported."""
    data = Path(path).read_bytes()
    if len(data) < 664 or data[:8] != b"SRGWSHRD":
        raise ValueError("not a generated-world scenario shard")
    version, header, size, tag, count = struct.unpack_from("<IIQII", data, 8)
    if (version, header, size, tag, count) != (13, 664, len(data), scenario, 35):
        raise ValueError("unsupported or mismatched scenario shard")
    if hashlib.sha256(data[header:]).digest() != data[72:104]:
        raise ValueError("scenario shard payload checksum mismatch")
    sections = [struct.unpack_from("<QII", data, 104 + i * 16) for i in range(count)]
    for offset, rows, stride in sections:
        if rows and (offset < header or stride == 0 or offset + rows * stride > size):
            raise ValueError("scenario shard section exceeds file bounds")
    names = ("config_contexts", "placement_contexts", "point_contexts",
             "point_placement_matches", "edge_contexts")
    return {"source_fingerprint": data[40:72].hex(),
            "authored_squad_context_counts": {name: sections[30 + i][1]
                                              for i, name in enumerate(names)}}


if __name__ == "__main__":
    main()
