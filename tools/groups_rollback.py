#!/usr/bin/env python3
"""Export a stopped SimpleGroup profile to v1 without changing any source file.

Usage: python groups_rollback.py PROFILE/SimpleGroup --output PATH/groups.v1.json
The destination must not exist. Read GROUPS-FORMAT.md before deploying the result.
"""
import argparse
import json
from pathlib import Path
import zlib


def digest(payload):
    value = zlib.adler32(payload.encode("utf-8"))
    return f"adler32:{value >> 16}:{value & 65535}"


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def parse(text):
    def reject(value):
        raise ValueError(f"non-finite JSON value: {value}")
    return json.loads(text, object_pairs_hook=unique_object, parse_constant=reject)


def validate(data):
    if not isinstance(data, dict) or type(data.get("m_Version")) is not int or data["m_Version"] != 1:
        raise ValueError("unsupported logical payload")
    groups = data.get("m_Groups")
    if not isinstance(groups, list):
        raise ValueError("missing groups array")
    ids, members = set(), set()
    for group in groups:
        if not isinstance(group, dict):
            raise ValueError("invalid group")
        group_id = group.get("m_GroupID")
        leader = group.get("m_LeaderUID")
        if not isinstance(group_id, str) or not group_id or group_id in ids:
            raise ValueError("invalid/duplicate group ID")
        ids.add(group_id)
        roster = group.get("m_Members")
        if not isinstance(roster, list) or not roster:
            raise ValueError("empty/invalid roster")
        leader_present = False
        for member in roster:
            if not isinstance(member, dict):
                raise ValueError("invalid member")
            uid = member.get("m_PlayerUID")
            if not isinstance(uid, str) or not uid or uid in members:
                raise ValueError("invalid/duplicate member ID")
            members.add(uid)
            leader_present |= uid == leader
        if not leader_present:
            raise ValueError("leader absent from roster")
    return data


def decode(path):
    envelope = parse(path.read_text(encoding="utf-8-sig"))
    if not isinstance(envelope, dict) or type(envelope.get("m_Version")) is not int:
        raise ValueError("missing/invalid file version")
    version = envelope["m_Version"]
    if version == 1:
        return validate(envelope)
    if version != 2:
        raise ValueError("unsupported file version")
    payload = envelope.get("m_Payload")
    count = envelope.get("m_ExpectedGroups")
    if not isinstance(payload, str) or not payload or type(count) is not int or count < 0:
        raise ValueError("incomplete envelope")
    if envelope.get("m_Digest") != digest(payload):
        raise ValueError("payload checksum mismatch")
    data = validate(parse(payload))
    if len(data["m_Groups"]) != count:
        raise ValueError("group count mismatch")
    return data


def select_source(directory):
    """Refuse ambiguities rather than discarding a verified pending mutation."""
    paths = [directory / ("groups.json" + suffix) for suffix in ("", ".tmp", ".bak")]
    candidates = {}
    for path in paths:
        if not path.exists():
            continue
        # Deliberately stricter than runtime discard policy: the operator must
        # preserve and resolve every invalid candidate before an offline rollback.
        candidates[path.name] = decode(path)
    final, tmp, bak = (candidates.get(path.name) for path in paths)
    if final is not None and tmp is not None and final != tmp:
        raise ValueError("final and tmp differ; resolve the pending save before rollback")
    if final is not None:
        return paths[0], final
    if tmp is not None:
        return paths[1], tmp
    if bak is not None:
        return paths[2], bak
    raise ValueError("no groups candidate")


def export(directory, output):
    source, data = select_source(directory)
    content = json.dumps(data, ensure_ascii=False, allow_nan=False, indent=2) + "\n"
    # Validate the actual bytes to be written, not a second independently built DTO.
    validate(parse(content))
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(content)
    if decode(output) != data:
        raise ValueError("export readback mismatch; preserve output for investigation")
    return {"source": str(source), "output": str(output), "groups": len(data["m_Groups"])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(export(args.directory, args.output), ensure_ascii=False))
    except (ValueError, OSError) as error:
        parser.exit(1, f"Rollback export refused: {error}\n")


if __name__ == "__main__":
    main()
