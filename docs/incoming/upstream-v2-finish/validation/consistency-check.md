# v2 plan consistency check (host-only, read-only)

Derived from the three v2 plan documents and the tree by
tools/check-v2-plan.py.  Every row is a reproducible check.

| check | expected | actual | verdict | detail |
| --- | --- | --- | --- | --- |
| L0 patch files | 66 (v4 status: 66-file stack) | 66 | PASS | PATCH-CLASSIFICATION still says 67 (pre-013a-fold) |
| v1 review-form patches | 26 | 26 | PASS |  |
| commit-message files | 26 | 26 | PASS | v2 needs 0027..0030 messages (absent today) |
| release-line manifest include | 55 | 55 | PASS |  |
| release-line manifest exclude | 11 | 11 | PASS |  |
| DTS-series patches | 1 | 1 | PASS |  |
| new RFC entries planned | 4 | 4 | PASS | planned: 0027,0028,0029,0030 |
| 013a still a separate L0 patch | no (v4 folded it into 013) | no | PASS |  |
| classification lists 013a fold | yes (historic fold) | yes | PASS | 013a is the one of 17 folds already executed |
| v4-status fold set == classification fold set | 027,031,032,036,038,041,043,044,046,048,051,053,059,060,066,067 | 027,031,032,036,038,041,043,044,046,048,051,053,059,060,066,067 | PASS |  |
| plan delta table fold set == v4-status fold set | 027,031,032,036,038,041,043,044,046,048,051,053,059,060,066,067 | 027,032,036,038,041,043,044,046,048,051,053,059,060,066,067 | FAIL | missing from submission-series.md: 031 |
| manifest hashes match current L0 patches | 0 drift | 0 | PASS |  |
| v1 form entries with module_param gates | 0 (acceptance) | 0020:1,0022:2,0025:3 | FAIL | baseline v1: 0020:1,0022:2,0025:3 |
| gen-series.py --check (v1 form) | clean | series up to date (26 patches) | PASS |  |
| DTS-0001 v4 sha256 | 6fa1c690... | 6fa1c690... | PASS |  |
| DTS-0001 v4 line count | 716 | 716 | PASS |  |
| DTS-0001 carries wifi-pcie-hog | no (retired before v4) | no | PASS | classification doc still says it must be removed |
| 0013 carries reset-gpios | yes (v4 fold) | yes | PASS |  |
| 0013 carries reset-assert-us | yes (v4 fold) | yes | PASS |  |
| 0013 carries reset-deassert-us | yes (v4 fold) | yes | PASS |  |
| new-RFC drafts keep an attending gate | no, if gates->none/DT is decided | yes | FAIL | v2 plan docs contradict each other on the gate policy |

Result: 21 checks, 18 PASS, 3 FAIL.
