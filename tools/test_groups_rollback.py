import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("rollback", Path(__file__).with_name("groups_rollback.py"))
rollback = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rollback)


class RollbackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = {"m_Version": 1, "m_Groups": [{
            "m_GroupID": "g1", "m_GroupName": "POST migration ñ雪", "m_LeaderUID": "u1",
            "m_Members": [{"m_PlayerUID": "u1", "m_PlayerName": "a", "m_JoinTimestamp": 19},
                          {"m_PlayerUID": "u2", "m_PlayerName": "b", "m_JoinTimestamp": 20}],
            "m_FlagPosition": [-1000.25, 53.125, 28000.5], "m_Tier": 3,
            "m_DeployedCount": 37, "m_GardenPlotCount": 4}]}

    def put(self, data=None, suffix="", **overrides):
        if data is None:
            data = self.data
        payload = json.dumps(data, ensure_ascii=False)
        envelope = dict(m_Version=2, m_ExpectedGroups=len(data["m_Groups"]),
                        m_PayloadParts=[payload[i:i+128] for i in range(0,len(payload),128)],
                        m_PayloadBytes=len(payload.encode("utf-8")), m_Digest=rollback.digest(payload))
        envelope.update(overrides)
        path = self.root / ("groups.json" + suffix)
        path.write_text(json.dumps(envelope), encoding="utf-8")
        return path

    def test_published_adler_vector(self):
        self.assertEqual(rollback.digest("Wikipedia"), "adler32:4582:920")

    def test_exports_current_fields_in_order_and_preserves_sources(self):
        source = self.put()
        before = source.read_bytes()
        output = self.root / "export.json"
        rollback.export(self.root, output)
        self.assertEqual(json.loads(output.read_text(encoding="utf-8")), self.data)
        self.assertEqual(source.read_bytes(), before)
        with self.assertRaises(FileExistsError):
            rollback.export(self.root, output)

    def test_omitted_record_count_and_checksum_rejected(self):
        for overrides in (dict(m_ExpectedGroups=2), dict(m_Digest="adler32:0:1"),
                          dict(m_PayloadParts=['{"m_Version":1,"m_Groups":[]}'])):
            with self.subTest(overrides=overrides):
                self.put(**overrides)
                with self.assertRaises(ValueError):
                    rollback.select_source(self.root)

    def test_future_in_each_slot_blocks(self):
        for suffix in ("", ".tmp", ".bak"):
            with self.subTest(suffix=suffix):
                for path in self.root.iterdir():
                    path.unlink()
                self.put()
                self.put(suffix=suffix, m_Version=3)
                with self.assertRaises(ValueError):
                    rollback.select_source(self.root)

    def test_ambiguous_pending_mutation_blocks(self):
        self.put()
        newer = copy.deepcopy(self.data)
        newer["m_Groups"][0]["m_GroupName"] = "newer"
        self.put(newer, suffix=".tmp")
        with self.assertRaisesRegex(ValueError, "final and tmp differ"):
            rollback.select_source(self.root)

    def test_missing_final_uses_tmp_then_backup(self):
        tmp = self.put(suffix=".tmp")
        self.put({"m_Version": 1, "m_Groups": []}, suffix=".bak")
        self.assertEqual(rollback.select_source(self.root)[1], self.data)
        tmp.unlink()
        self.assertEqual(rollback.select_source(self.root)[1]["m_Groups"], [])

    def test_invalid_identities_rejected_even_with_matching_digest(self):
        for kind in ("duplicate_member", "missing_leader", "duplicate_group"):
            data = copy.deepcopy(self.data)
            group = data["m_Groups"][0]
            if kind == "duplicate_member":
                group["m_Members"][1]["m_PlayerUID"] = "u1"
            elif kind == "missing_leader":
                group["m_LeaderUID"] = "absent"
            else:
                data["m_Groups"].append(copy.deepcopy(group))
            self.put(data)
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                rollback.select_source(self.root)

    def test_empty_and_legacy(self):
        self.put({"m_Version": 1, "m_Groups": []})
        self.assertEqual(rollback.select_source(self.root)[1]["m_Groups"], [])
        (self.root / "groups.json").write_text(json.dumps(self.data), encoding="utf-8")
        self.assertEqual(rollback.select_source(self.root)[1], self.data)

    def test_duplicate_keys_and_nonfinite_rejected(self):
        for text in ('{"m_Version":1,"m_Version":2}', '{"x":NaN}'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                rollback.parse(text)

    def test_part_boundaries_omissions_reordering_and_byte_count(self):
        source = self.put()
        base = json.loads(source.read_text())
        for kind in ("omit", "reorder", "oversize", "wrongbytes", "empty"):
            envelope = copy.deepcopy(base)
            parts = envelope["m_PayloadParts"]
            if kind == "omit":
                parts.pop()
            elif kind == "reorder":
                parts[0], parts[1] = parts[1], parts[0]
            elif kind == "oversize":
                envelope["m_PayloadParts"] = ["".join(parts)]
            elif kind == "wrongbytes":
                envelope["m_PayloadBytes"] += 1
            else:
                parts.insert(0, "")
            source.write_text(json.dumps(envelope),encoding="utf-8")
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                rollback.decode(source)


if __name__ == "__main__":
    unittest.main()
