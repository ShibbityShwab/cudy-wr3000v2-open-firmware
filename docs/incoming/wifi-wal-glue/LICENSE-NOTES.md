# License notes - wifi-wal-glue

All text committed in this slice is **original writing by this lane,
GPL-2.0** (SPDX tags in every source file).  HiSilicon copyright headers are
retained where the code is the clean-room continuation of the HiSilicon WLAN
framework interface lineage, matching the policy of the previous slices.

Referenced, not duplicated:
- **wifi-algcfg-slice/vendor/hi3881-wal/** - verbatim GPL-2.0-or-later
  HiSilicon text (headers kept in that directory, committed by the algcfg
  slice).  This slice's hipriv entry shape follows those files.
- **hi1105 / hongmengkernel mirror** - carries NO license grant
  ("Copyright HiSilicon ... All rights reserved"); used locally as a
  name/structure reference only.  No text from it is committed here.
- **census CSVs** (wifi-framework-port/census/, wifi-datapath-slice/mapping/) -
  derived structural metadata (function names/sizes/families), committed by
  their slices under the same policy.

No vendor binaries, object code, firmware, INI, calibration data or per-unit
values are committed anywhere in this slice; the .ko/.o build outputs are
gitignored (glue/.gitignore).
