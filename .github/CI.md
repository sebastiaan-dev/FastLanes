# CI matrix policy and build reuse

## Primary PR coverage: `cpp.yaml`

| Runner | Configurations | Validation per linkage |
| --- | --- | --- |
| Ubuntu 24.04 x64 | Debug, Release | Debug: standalone regressions; Release: full non-QuickFuzz suite, standalone regressions, example, install |
| Ubuntu 24.04 ARM | Debug, Release | Same; retain the existing Debug/shared exclusion |
| `macos-latest` ARM | Release | Full non-QuickFuzz suite, standalone regressions, example, install |
| Windows latest | Release | Standalone regressions, example, install (no full dataset suite) |

Both static and shared are validated except for the exclusion above. There is
**one** primary macOS build job, not separate latest/pinned jobs. Debug is not
replaced with RelWithDebInfo: the latter normally defines `NDEBUG` and disables
FastLanes assertions. Non-Windows Debug now enables standalone regressions
without GoogleTest, dataset fetching, or synthetic-data generation, using the
separate `standalone-tests` composite-action input.

Primary CPP has **10 runner jobs**: six builds, two Linux lint jobs, one
format/helper check, and the stable `cpp-ci` aggregate. This means six initial
static builds and five incremental shared transitions. Full datasets are
produced in the three Release full-test jobs only. Lint covers Ubuntu 24.04
x64 Debug/static and Ubuntu 24.04 ARM Release/shared. Older Ubuntu, pinned
macOS, Intel macOS, macOS Debug, and macOS lint are not ordinary PR CPP jobs.
These counts exclude the independent Examples, FSST, FlatBuffers, Python, and
Rust workflows.

## Compatibility coverage: `compatibility.yaml`

| Runner | Configurations | Validation per linkage |
| --- | --- | --- |
| Ubuntu 22.04 x64 and ARM | Debug, Release | Debug: standalone regressions; Release: full suite, standalone regressions, example, install |
| macOS 14 and macOS 15 (pinned ARM images) | Debug, Release | Same, retaining macOS assertion-enabled coverage |
| macOS 15 Intel | Release | Full suite, standalone regressions, example, install |

All nine build jobs validate both linkages (nine static builds and nine shared
transitions). Add one macOS 15 Debug/shared lint job, one macOS latest FSST job,
and the `compatibility-ci` aggregate: **12 runner jobs** when enabled. The five
Release build jobs generate datasets. With both tiers enabled, the C++ workflows
use 22 runner jobs, including 15 builds; normal PRs pay only the primary ten.
Intel macOS Debug and the redundant Ubuntu-latest Release build are deliberately
not retained. The earlier local reuse proposal had 17 build jobs and three lint
jobs; the pre-optimization CPP workflow had 53 independent build jobs.

Compatibility runs:

- Weekly, Monday at 04:23 UTC (GitHub may delay schedules).
- Manually via **Run workflow**, selecting the branch/ref to validate.
- On version-tag pushes matching `v*` or `[0-9]*`, including prerelease tags
  such as `v1.2.3-rc.1`. These are validation runs only; nothing is published.
- On PRs with the **`ci:full`** label. Adding the label starts coverage;
  `synchronize` reruns it for later commits while the label remains. Opening or
  reopening an already-labelled PR also runs it. Unlabelled PR events skip all
  compatibility jobs; no `pull_request_target` or write token is used.

**Scheduled workflows execute only on the repository's default branch.** If
that branch is `main`, the weekly run does not test `integration/duckdb`. The
workflow must first exist on the default branch to be scheduled. GitHub also
requires a `workflow_dispatch` workflow to exist on the default branch before
manual dispatch is available. Once deployed there, dispatch can select
`integration/duckdb`; until then, use the opt-in PR label or version-tag events.
Changing the cron does not select another branch.

Compatibility failures are red/visible: all matrix strategies disable fail-fast,
no job uses `continue-on-error`, and the aggregate runs even after failures and
requires build, lint, and FSST success. Treat `compatibility-ci` as a release
validation requirement when running version tags, and resolve failures before
publishing through any separate release process. The aggregate does not itself
publish or enforce another workflow's release ordering.

## Shared implementation and observability

`cpp-build.yml` and `cpp-lint.yml` are reusable workflows used by both tiers.
Each build validates static first, then reconfigures **the same Ninja tree** for
shared. CMake/Ninja track changed definitions and dependencies; objects are not
copied or assumed interchangeable. Tests/examples run before reconfiguration.
Static and shared installations use separate staging trees, and the install
helper verifies every public header. Release optimization, assertion settings,
QuickFuzz exclusion, and the 5000-second CTest timeout remain unchanged.

CI explicitly enables `FLS_ENABLE_LINE_TABLES_ONLY`. In Debug and RelWithDebInfo,
this adds Clang's `-gline-tables-only`: source-level stack traces remain available,
but full type/local-variable debug information is omitted. It does not change
optimization or define `NDEBUG`. The option is **OFF by default**, so normal
local/embedded builds retain full debug information. Release is unaffected.
Debug negative tests intentionally abort; the CI test step disables core dumps.

Lint analyzes `compile_commands.json` without compiling/linking another library.
The source regex in `cpp-lint.yml` excludes vendored `src/cor/prm/fsst` and
`src/cor/prm/fsst12`, matching their empty `CXX_CLANG_TIDY` target properties.
Keep these exclusions synchronized. All other library sources, including
generated primitives, remain selected; checks/warnings-as-errors are unchanged.

- Ccache remains the C/C++ compiler launcher. Compiler contents, flags, sources,
  and headers participate in validity checks; no sloppiness is enabled.
- Restore prefixes separate image, architecture, build type, and compiler
  version. Static/shared share a cache. Save keys are unique per revision,
  run, and attempt. Each entry is limited to 512 MB; repository quotas apply.
- Only compiler results are cached, not build trees, tests, or clang-tidy output.
- Every build reports cache statistics and uploads its Ninja log, even on
  failure where available. Artifacts retain the seven-day policy.
- Install artifacts retain `fastlanes-cpp-install-<platform>` names and
  `install-OFF` / `install-ON` layouts. Primary Ubuntu 24.04 x64 Release replaces
  Ubuntu-latest as the Linux artifact producer; macOS-latest and Windows-latest
  remain. ARM Linux and compatibility jobs verify installs without uploading
  duplicate install trees. Artifact upload is not package/release publishing.

### Triggers and required checks

Primary CPP, Examples, FlatBuffers, FSST, Python, and Rust run on PRs targeting
any branch, pushes to `main`, `dev`, and `integration/duckdb`, and all tags.
Feature-branch pushes without a PR do not start those workflows. CPP also allows
manual runs. Integration branches with open PRs can still trigger both events.
Concurrency cancels superseded runs per workflow/ref; the two C++ tiers have
separate groups. Reusable workflows do not add a competing concurrency group.
Existing manual Python publishing controls are unchanged.

**Require `CPP / cpp-ci` (the `cpp-ci` job/check), not matrix job names, in branch
protection.** It always runs and requires format/helper checks, all primary builds,
and both primary lint jobs to succeed. Compatibility is separately opt-in and
must not replace that stable gate. Do not remove other required workflows.

## FSST target narrowing

The independent primary FSST workflow runs Linux; compatibility runs macOS.
Both configure Release with `FLS_ENABLE_FSST_TESTING_AND_BENCHMARKING=ON`, build
only `test_fsst test_fls_fsst`, and run:

```bash
ctest --test-dir build/fsst-release --output-on-failure --no-tests=error \
  -R '^TestFsst\.fsst(12)?_dbtext$'
```

Keep compatibility commands synchronized with `fsst.yaml`. All seven FSST tests
remain registered, but only the two upstream FSST/FSST12 checked-in dbtext tests
are currently executable CI coverage. Other cases depend on a pre-existing
missing `generated.csv` corpus or helper benchmarks with existing memory-safety
issues. The helper target is still compile-checked. These limitations were
recorded in tagged Beans issues; this optimization does not claim seven passing
FSST tests or fix those unrelated issues.

## Local measurements and validation limits

Baseline: PR #5 revision `80aabf0`, merged into `integration/duckdb` by `89e35a2`
with the same source tree. All figures below are **local observations**,
not promised hosted-runner or whole-project speedups. Later iterations use
single samples and one batched full-suite build, rather than repeating a build
matrix for each change.

### Earlier build-reuse measurement

Apple M5 Pro, Apple Clang 21, Ninja, eight workers, Release with tests,
standalone regressions, examples, and installation enabled:

| Build operation | Wall time | Compiler invocations |
| --- | ---: | ---: |
| Initial static build, empty compiler cache | 129.10 s | 172 |
| Static-to-shared incremental build | 17.94 s | 48 |
| Clean shared build, cache disabled | 108.29 s | 172 |
| Clean shared build, warm cache | 8.45 ± 0.26 s | 172 cache hits/run |

Hyperfine: single samples for cold/incremental builds and three warm runs,
cleaning Ninja outputs before each. Warm runs had 516/516 direct cache hits;
Hyperfine flagged a possible outlier. Configuration, fetching, tests, and
artifact transfer are excluded. These earlier measurements predate the final
dispatch changes below and must not be combined into a new whole-build estimate.
Earlier local Release static/shared runs passed 261 selected tests, example,
and install verification; Debug static/shared passed standalone integration.
Those are historical validation results, not a hosted run of this final matrix.

### Targeted dispatch measurements

From the local variant-dispatch report: Apple M5 Pro arm64, Apple Clang 21,
Ninja `-j2`, cache disabled, three Hyperfine samples, rebuilding only
`rsum_operator`, `slpatch_operator`, `transpose_operator`, and
`physical_expression` objects (including the new diagnostic overhead):

| Configuration | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| RelWithDebInfo | 129.360 ± 1.912 s | 37.068 ± 0.473 s | 71.3% |
| Release | 87.687 ± 0.274 s | 25.984 ± 0.141 s | 70.4% |

Combined object disk size was 507.42 → 155.39 MiB (RelWithDebInfo),
104.35 → 26.83 MiB (Release), and 4652.58 → 1853.12 MiB (Debug), **not RSS**.
The three registered standalone CTest entries in that report passed in Debug,
RelWithDebInfo, and Release, including 144 invalid dispatch cases. The E2E
runtime comparison (20 samples) was 74.370 ± 5.247 → 71.500 ± 7.525 ms,
statistically unchanged. Focused runtimes were too short/noisy for a speed claim.
This is a four-translation-unit result, not a 70% whole-project improvement.

From the materializer report: same local architecture/compiler, isolated Ninja
RelWithDebInfo (`-O2 -g -DNDEBUG`), cache disabled, **one** Hyperfine sample per
side for `materializer.cpp.o`: 59.452 → 12.330 s (79.26% reduction).
Object disk size: 206,986,704 → 28,671,128 bytes. This is neither a statistical
benchmark nor a whole-build result. Focused before/after dispatch checks and the
11-case standalone E2E passed via a targeted smoke link. That report did not
validate an assertion-enabled materializer build, full dataset suite, every
codec family, or another platform, and makes no runtime-speed claim.

### Further output reduction and final combined validation

Cold failure-message construction is now shared in `physical_expression.cpp`,
including the encode-transpose fallback. Visitors retrieve RTTI or call the
shared failure path rather than instantiate string formatting/throwing for
hundreds of alternatives each. The final measured RelWithDebInfo object sizes
(with full debug information, **not** the line-table option) are:

| Object | Original | Optimized |
| --- | ---: | ---: |
| rsum_operator | 155.81 MiB | 10.43 MiB |
| slpatch_operator | 157.20 MiB | 11.35 MiB |
| transpose_operator | 158.91 MiB | 45.72 MiB |
| physical_expression | 35.49 MiB | 31.23 MiB |
| materializer | 197.40 MiB | 27.34 MiB |
| **Five-object total** | **704.82 MiB** | **126.08 MiB** |

This is an 82.1% reduction for those objects, not for the entire library. A
single build of the two final diagnostic-cleanup objects took 22.08 seconds.
No additional controlled before/after timing was run for that cleanup.

Separately, the opt-in line-table setting reduced the unchanged interpreter's
RelWithDebInfo object from 717.70 MiB to 55.06 MiB. That object compiled in
51.82 seconds in one Hyperfine sample. Its historical full-debug Ninja timing
was 91.44 seconds, but it was not a controlled paired timing measurement. An
in-place variant-construction experiment was discarded: it increased output
size and did not demonstrate a repeatable compile-time improvement.

One batched Release/shared build of the combined production changes passed
**all 265 selected CTest entries**, `cpp_example`, and install-header validation.
After include-only lint cleanup, the five focused integration/dispatch entries
passed again. Narrow dispatch covers all eight integer widths and now 168
invalid cases (including encode transpose); the materializer adds nine invalid
operator/column pairs. Assertion-mode drivers require an actual subprocess
abort and case-specific type diagnostics, with negative controls that reject
ordinary error exits and wrong-type diagnostics. All nine Python CI-helper tests
pass. The final Debug library has now compiled/archived successfully in both
symbol modes, but final assertion-mode execution remains pending in the Debug
CI standalone jobs. Earlier Debug runtime validation above applies only to the
earlier narrow-dispatch stage.

### Current complete static-library sizes

All four `FastLanes` targets were subsequently rebuilt from the current source
with Ninja and Apple Clang 21 on macOS ARM64. Each finished with a no-op Ninja
check. These are complete, unstripped `libFastLanes.a` archives, excluding tests:

| Configuration | Full debug information | CI line-table-only information |
| --- | ---: | ---: |
| RelWithDebInfo | 1.401 GiB (1,504,244,424 bytes) | 339.15 MiB (355,623,528 bytes) |
| Debug | 14.397 GiB (15,458,994,336 bytes) | 11.381 GiB (12,220,451,832 bytes) |

Debug remains very large even with reduced debug information. The five-object
improvement above must not be extrapolated to a whole-library reduction.
Raw measurements are in `build/ci-measurements/current-library-sizes.json`.
These builds validate compilation/linking, not an additional test-suite run.

### FSST measurement

Local Release, Apple Clang 21, Ninja: broad default build **81.345 s / 146 Ninja
steps** → explicit `test_fsst test_fls_fsst` build **4.652 s / 32 steps**.
This measures FSST target selection only, not a whole-project build improvement.
The two selected upstream dbtext tests passed; the seven-test registration does
not imply all seven ran successfully.

### Remaining validation gaps

YAML parsing, actionlint, matrix/trigger assertions, and CI-helper unit tests
validate the workflow configuration locally; they do not execute GitHub Actions.
Hosted Windows/macOS/Linux execution, caches, artifact layouts, and required-check
names must also be validated in the PR checks. Compatibility CI performs
validation only; publishing remains separate.

Earlier lint selection checks matched CMake exactly (128 sources, excluding four
vendored FSST/FSST12 files). LLVM 18 smoke checks passed in an Ubuntu 24.04 ARM
container, but **full lint was not validated**: a 2 GB Docker VM killed heavy
translation units with signal 9, including 12 that could not complete on serial
retry. Completed analyses reported no diagnostics; the VM configuration and CI
lint policy were not relaxed. Existing obsolete action versions in external
Examples/Rust workflows are tracked separately. The final changed-file LLVM 18
pass caught four unused includes, which were removed; rsum/slpatch then passed
lint. The materializer, physical-expression and transpose analyses still exceeded
the local 2 GB VM limit. Local Apple Clang results do not establish correctness
on every hosted compiler, and hosted validation remains pending.
