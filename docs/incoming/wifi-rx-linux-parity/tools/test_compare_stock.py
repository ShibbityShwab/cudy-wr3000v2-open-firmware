#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# tools/test_compare_stock.py - host unit tests for compare_stock.py
# (wifi-rx-linux-parity lane, shared task task-166).  No device, no network.
#
# Run:  python3 tools/test_compare_stock.py
import contextlib
import importlib.util
import io
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_PATH = os.path.join(HERE, "compare_stock.py")
spec = importlib.util.spec_from_file_location("compare_stock", MOD_PATH)
cs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cs)

DUMP_PATH = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "..", "dumps", "reg_all.txt"))


def words_line(kind, addr, values):
    return "%s %08x: %s" % (kind, addr, " ".join("%08x" % v for v in values))


class TestParse(unittest.TestCase):
    def test_parse_probe(self):
        text = "\n".join([
            "DP rx parity probe #1 (read-only, ...):",
            words_line("live", 0x40050000, [0x400, 0, 0x20, 0x20, 0, 0x200020,
                                            0, 0, 0, 0x800000]),
            words_line("cfg", 0x40040000, [0x100, 0x83fd7000, 0]),
            words_line("cfg", 0x40040040, [0, 0xfffffcc1]),
            words_line("blk", 0x40042000, [0x1010a0, 0x16203e]),
            "legend: * = ...",
        ])
        w = cs.parse_probe(text)
        self.assertEqual(w[0x40050000], 0x400)
        self.assertEqual(w[0x4005000c], 0x20)
        self.assertEqual(w[0x40050024], 0x800000)
        self.assertEqual(w[0x40040000], 0x100)
        self.assertEqual(w[0x40040044], 0xfffffcc1)
        self.assertEqual(w[0x40042004], 0x16203e)
        self.assertEqual(len(w), 10 + 5 + 2)

    def test_parse_probe_garbage_word(self):
        text = words_line("cfg", 0x40040000, [1, 2])
        text += "\n" + "cfg 40040008: zzzz\n"
        with self.assertRaises(ValueError):
            cs.parse_probe(text)

    def test_parse_probe_empty(self):
        with self.assertRaises(ValueError):
            cs.parse_probe("no probe here\n")

    def test_parse_dump(self):
        text = "addr = 40040000, value = 100\naddr = 40040048, value = fffffcc1\n"
        w = cs.parse_dump(text)
        self.assertEqual(w[0x40040000], 0x100)
        self.assertEqual(w[0x40040048], 0xfffffcc1)


class TestCompare(unittest.TestCase):
    def test_statuses(self):
        probe = {0x4004001c: 0xb0063c, 0x4004005c: 0,
                 0x40040048: 0xdead, 0x40050000: 0x400}
        dump = {0x4004001c: 0xb0063c, 0x4004005c: 4,
                0x40040048: 0xfffffcc1, 0x40050000: 0x400}
        rows = {r["addr"]: r for r in cs.compare(probe, dump)}
        self.assertEqual(rows[0x4004001c]["status"], "match")
        self.assertEqual(rows[0x4004005c]["status"], "DIFF-other")
        self.assertEqual(rows[0x40040048]["status"], "DIFF-program")
        self.assertEqual(rows[0x40050000]["status"], "match")
        self.assertEqual(rows[0x4004005c]["role"], "mac-cfg")
        self.assertEqual(rows[0x40050000]["role"], "live-bank")

    def test_no_stock(self):
        rows = cs.compare({0x40042000: 1}, {})
        self.assertEqual(rows[0]["status"], "no-stock")
        self.assertIsNone(rows[0]["stock"])

    def test_gaps(self):
        dump = {0x40040000 + o: 1 for o in cs.PROGRAM_OFFS}
        dump[0x4004005c] = 4
        dump[0x4004006c] = 4
        dump[0x40040008] = 0
        gaps = cs.mac_cfg_gaps({}, dump)
        self.assertEqual([g[0] for g in gaps], [0x5c, 0x6c])


class TestAgainstCommittedDump(unittest.TestCase):
    def test_committed_capture(self):
        """The committed vendor capture must classify cleanly against the
        embedded program/stock model: every MAC-config word the capture shows
        is either a programmed word (match) or one of the two identified
        missing-write candidates."""
        self.assertTrue(os.path.exists(DUMP_PATH), DUMP_PATH)
        dump = cs.parse_dump(open(DUMP_PATH, encoding="utf-8",
                                  errors="replace").read())
        cfg = {a: dump[a] for a in dump if 0x40040000 <= a < 0x400400b0}
        self.assertGreater(len(cfg), 30)
        rows = cs.compare(cfg, dump)
        diffs = [r for r in rows if r["status"].startswith("DIFF")]
        self.assertEqual(diffs, [])
        gaps = cs.mac_cfg_gaps(cfg, dump)
        self.assertEqual([g[0] for g in gaps], [0x5c, 0x6c])
        self.assertEqual(cs.parse_dump("") .get(0x40040000), None)


class TestCli(unittest.TestCase):
    def _probe_file(self, text):
        fd, path = tempfile.mkstemp(suffix=".txt")
        os.close(fd)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        self.addCleanup(os.unlink, path)
        return path

    def test_cli_matching(self):
        text = "\n".join([
            words_line("cfg", 0x40040000, [0x100, 0x83fd7000, 0, 0, 0x02060850]),
            words_line("cfg", 0x4004001c,
                       [0xb0063c, 0x844c7000, 0x844c8000, 0x83734000,
                        0x100, 0x200, 0xfff, 0x8000, 0x8000, 0, 0,
                        0xfffffcc1, 0, 0, 0, 0]),
        ])
        path = self._probe_file(text)
        with contextlib.redirect_stdout(io.StringIO()):
            rc = cs.main(["--probe", path, "--dump", DUMP_PATH,
                          "--fail-on-diff"])
        self.assertEqual(rc, 0)

    def test_cli_diff_fails(self):
        text = words_line("cfg", 0x4004001c,
                          [0xdead, 0x844c7000, 0x844c8000, 0x83734000,
                           0x100, 0x200, 0xfff, 0x8000, 0x8000, 0, 0,
                           0xfffffcc1, 0, 0, 0, 0])
        path = self._probe_file(text)
        with contextlib.redirect_stdout(io.StringIO()):
            rc = cs.main(["--probe", path, "--dump", DUMP_PATH,
                          "--fail-on-diff"])
        self.assertEqual(rc, 3)

    def test_cli_missing_probe(self):
        with contextlib.redirect_stdout(io.StringIO()),                 contextlib.redirect_stderr(io.StringIO()):
            rc = cs.main(["--probe", "/nonexistent/probe.txt"])
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
