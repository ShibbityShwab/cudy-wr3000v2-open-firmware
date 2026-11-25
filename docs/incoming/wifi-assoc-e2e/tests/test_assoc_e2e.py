#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Unit tests for the wifi-assoc-e2e host client harness (task-158).

HOST-ONLY and RF-FREE: this exercises the committed parsers, classifiers,
config rendering/redaction and lane hygiene. It never touches a network
interface and never starts wpa_supplicant.

Run:  python3 -m unittest discover -s opensource/docs/incoming/wifi-assoc-e2e/tests -v
  or: python3 opensource/docs/incoming/wifi-assoc-e2e/tests/test_assoc_e2e.py
"""

import os
import sys
import unittest
from pathlib import Path

LANE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(LANE / "scripts"))
sys.path.insert(0, str(LANE / "tests"))

import assoc_e2e as ae  # noqa: E402
import fixtures as fx  # noqa: E402


class TestScanParser(unittest.TestCase):
    def test_bss_count(self):
        self.assertEqual(len(ae.parse_iw_scan(fx.IW_SCAN_TARGET_WPA2)),
                         fx.IW_SCAN_TARGET_WPA2_EXPECT)

    def test_target_fields(self):
        bss = ae.parse_iw_scan(fx.IW_SCAN_TARGET_WPA2)
        tgt = next(b for b in bss if b["ssid"] == "cudy-ap-test")
        self.assertEqual(tgt["bssid"], "06:05:04:03:02:01")
        self.assertEqual(tgt["freq"], 2437.0)
        self.assertEqual(tgt["signal"], -63.0)
        self.assertEqual(tgt["ds_channel"], 6)
        self.assertIn("PSK", tgt["rsn_akms"])
        self.assertIn("CCMP", tgt["rsn_pairwise"])
        self.assertFalse(tgt["mfp_required"])

    def test_wpa3_mfp(self):
        bss = ae.parse_iw_scan(fx.IW_SCAN_TARGET_WPA3)
        tgt = bss[0]
        self.assertIn("SAE", tgt["rsn_akms"])
        self.assertTrue(tgt["mfp_required"])
        self.assertIn("BIP-CMAC-256", tgt.get("group_mgmt_cipher", ""))


class TestScanAssess(unittest.TestCase):
    def test_wpa2_pass(self):
        out = ae.assess_scan(fx.IW_SCAN_TARGET_WPA2, "cudy-ap-test", 2437, 6,
                             -70, "psk-ccmp")
        self.assertEqual(out["verdict"], "PASS", out["checks"])

    def test_weak_rssi_fail(self):
        out = ae.assess_scan(fx.IW_SCAN_WEAK_WPA2, "cudy-ap-test", 2437, 6,
                             -70, "psk-ccmp")
        self.assertEqual(out["verdict"], "FAIL")
        names = {c["name"]: c["ok"] for c in out["checks"]}
        self.assertFalse(names["rssi_gate"])

    def test_absent_target(self):
        out = ae.assess_scan(fx.IW_SCAN_OTHER, "cudy-ap-test", 2437, 6,
                             -70, "psk-ccmp")
        self.assertEqual(out["verdict"], "FAIL")
        self.assertIsNone(out["target"])

    def test_open_profile(self):
        out = ae.assess_scan(fx.IW_SCAN_TARGET_OPEN, "wr3000v2-apstart",
                             2437, 6, -70, "none")
        self.assertEqual(out["verdict"], "PASS", out["checks"])

    def test_wpa3_profile(self):
        out = ae.assess_scan(fx.IW_SCAN_TARGET_WPA3, "cudy-wr3000v2-wpa3",
                             2437, 6, -70, "sae-ccmp-mfp")
        self.assertEqual(out["verdict"], "PASS", out["checks"])

    def test_wrong_profile_warns(self):
        out = ae.assess_scan(fx.IW_SCAN_TARGET_WPA3, "cudy-wr3000v2-wpa3",
                             2437, 6, -70, "psk-ccmp")
        self.assertEqual(out["verdict"], "WARN")


class TestSupplicantClassification(unittest.TestCase):
    CASES = [
        ("complete.wpa2", fx.WPA2_COMPLETED_LOG, fx.WPA2_COMPLETED_STATUS,
         "wpa2", "COMPLETED", True),
        ("auth-timeout.wpa2", fx.WPA2_AUTH_TIMEOUT_LOG,
         fx.WPA2_AUTH_TIMEOUT_STATUS, "wpa2", "AUTH-TIMEOUT", False),
        ("assoc-timeout.wpa2", fx.WPA2_ASSOC_TIMEOUT_LOG,
         fx.WPA2_ASSOC_TIMEOUT_STATUS, "wpa2", "ASSOC-TIMEOUT", False),
        ("4way-failed.wpa2", fx.WPA2_4WAY_FAILED_LOG,
         fx.WPA2_4WAY_FAILED_STATUS, "wpa2", "FOURWAY-FAILED", False),
        ("auth-timeout.wpa3", fx.WPA3_AUTH_TIMEOUT_LOG,
         fx.WPA3_AUTH_TIMEOUT_STATUS, "wpa3", "SAE-AUTH-TIMEOUT", False),
        ("not-found", fx.WPA2_NOT_FOUND_LOG, fx.WPA2_NOT_FOUND_STATUS,
         "wpa2", "NETWORK-NOT-FOUND", False),
        ("driver-error", fx.DRIVER_ERROR_LOG, fx.DRIVER_ERROR_STATUS,
         "wpa2", "DRIVER-ERROR", False),
    ]

    def test_cases(self):
        for name, log, status, variant, stage, complete in self.CASES:
            with self.subTest(name=name):
                out = ae.classify_supplicant(log, ae.parse_supplicant_status(status),
                                             variant)
                self.assertEqual(out["stage"], stage, out)
                self.assertEqual(out["complete"], complete)

    def test_progress_only_reports_stage(self):
        log = ("wlan1: SME: Trying to authenticate with 06:05:04:03:02:01\n"
               "wlan1: Trying to associate with 06:05:04:03:02:01\n")
        out = ae.classify_supplicant(log, {"wpa_state": "ASSOCIATING"}, "wpa2")
        self.assertEqual(out["stage"], "ASSOC")
        self.assertFalse(out["complete"])


class TestConfigRendering(unittest.TestCase):
    def test_templates_substitute_and_redact(self):
        cases = [("wpa_supplicant-open.template.conf", "open"),
                 ("wpa_supplicant-wpa2.template.conf", "wpa2"),
                 ("wpa_supplicant-wpa3-sae.template.conf", "wpa3")]
        for name, prof in cases:
            with self.subTest(profile=prof):
                text = ae.read_text(ae.CONFIGS_DIR / name)
                self.assertTrue(text)
                rendered = ae.render_wpa_conf(text, "lab-ssid", "lab-psk",
                                              "/run/x", 2437)
                self.assertNotIn("@SSID@", rendered)
                self.assertNotIn("@PSK@", rendered)
                self.assertNotIn("@CTRL@", rendered)
                self.assertNotIn("@FREQ@", rendered)
                self.assertNotIn("lab-psk", ae.redact(rendered))

    def test_profiles_sane(self):
        prof = ae.load_profiles()
        self.assertEqual(sorted(prof["profiles"]), ["open", "wpa2", "wpa3"])
        for name, entry in prof["profiles"].items():
            with self.subTest(profile=name):
                self.assertTrue(entry.get("ssid"))
                self.assertTrue(entry.get("client_template"))
                self.assertIn(entry.get("rsn_expect"),
                              ("none", "psk-ccmp", "sae-ccmp-mfp"))


class TestLaneHygiene(unittest.TestCase):
    def test_no_nonlab_credentials(self):
        self.assertEqual(ae.validate_lane_secrets(), [])

    def test_self_checks_all_pass(self):
        checks = ae.self_checks()
        failures = [f"{name} ({detail})" for name, ok, detail in checks if not ok]
        self.assertEqual(failures, [])


class TestCliDryRun(unittest.TestCase):
    """Every live subcommand must be a no-op under --dry-run (host-testable)."""

    def _run(self, argv):
        return ae.main(argv + ["--dry-run"])

    def test_preflight_dry(self):
        self.assertEqual(self._run(["preflight"]), 0)

    def test_status_dry(self):
        self.assertEqual(self._run(["status"]), 0)

    def test_reset_dry(self):
        self.assertEqual(self._run(["reset"]), 0)

    def test_scan_dry(self):
        self.assertEqual(self._run(["scan", "--profile", "wpa2"]), 0)

    def test_assoc_dry(self):
        self.assertEqual(self._run(["assoc", "--profile", "wpa2"]), 0)

    def test_watch_dry(self):
        self.assertEqual(self._run(["watch", "--scan-only", "--iterations", "1"]), 0)

    def test_cleanup_dry(self):
        self.assertEqual(self._run(["cleanup"]), 0)

    def test_verify(self):
        self.assertEqual(self._run(["verify"]), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
