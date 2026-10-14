# Branch consolidation audit

The consolidation target is `fix/duckdb-1.5.6`. The fork's `origin`, the CWIDA
`upstream`, and the separate `patched` remote now use GitHub SSH URLs locally.

## Reviewed fork branches

| Branch | Original tip | Decision |
| --- | --- | --- |
| `lambda-interface` | `ae97f0e` | Superseded by the FileWriter/RowGroupWriter API in the consolidated patch stack. Preserve its history, not the obsolete prototype API. |
| `cmake-overrride` | `6af576c` | Parallel prepare/ordered flush is already present. The current compiler/toolchain fix supersedes the old compiler overrides. |
| `dev` | `4314468` | Contains the above two branches; reconcile its history through `update`. |
| `update` | `7cf27aa` | BOOLEAN support, column views, writer and wizard integration are already present. Restore `ad34adf`: Connection footer/sample/forced-schema options were no longer forwarded to FileWriter, and WithSampleSize was missing. |
| `task/ifstream-to-pread` | `f2969ab` | Positional reads and filesystem helpers are already present, but deferred opening was lost. Port that feature into the current shared-reader implementation using call_once. |
| `release_v0.1` | `f08434c` | Keep as a historical release branch. Its old Rowgroup/Footer refactor targets a retired API and is not appropriate for the current table-API integration. |
| `main` | `6c2e8df` | Leave the existing default branch and its worktree untouched. It has a separate history from dev; do not force an unrelated-history merge. |

The missing writer-options change is cherry-picked with its original commit
recorded. Row-group preparation also skips automatic type narrowing when an
explicit encoding/schema pool is forced; otherwise an INT64 codec can receive an
INT16 column. The small-integer regression failed before this follow-up fix.
After the useful work is ported, history-only merges account for
`update` and `task/ifstream-to-pread` as reviewed/superseded; their older APIs are
not allowed to overwrite the tested integration.

Not restored: generated `.direnv` state, an orphan Nix lockfile, stale Rust vendor
changes, old test-disable toggles, string NULL-sentinel inference, and a verbose
JSON dump using a retired descriptor path. These are not independent working
features of the current DuckDB integration. The latest CWIDA dev/MSVC overhaul
is also not part of this cleanup; that would be a separate upstream rebase.

## Positional I/O port

The port preserves concurrent `pread`, adds deferred initialization shared by
Read/ReadRange/Size, and permits File to create output paths that do not yet
exist. Failed initialization is retryable. Runtime errors and bounds checks are
used instead of FLS_ABORT because FLS_ABORT is compiled out in Release; EOF must
not loop forever, and a failed fstat must not leave an invalid cached size.

## Validation

`FLS_BUILD_STANDALONE_TESTS=ON` enables six self-contained integration cases,
without downloading GoogleTest or datasets. They cover new output creation,
const buffers, concurrent first reads, Size-before-Read, initialization retries,
buffer/file bounds, truncated/empty files, and Connection writer options with
both inline/external footers and explicit/pool encodings.

Through the paired extension build:

```sh
cd ~/repos/duckdb-fastlanes.fix-duckdb-1.5.6
CMAKE_BUILD_PARALLEL_LEVEL=8 make release GEN=ninja \
  FASTLANES_SOURCE_DIR=../FastLanes.fix-duckdb-1.5.6 \
  EXT_FLAGS=-DFLS_BUILD_STANDALONE_TESTS=ON
build/release/_deps/fls_repo-build/test/standalone/fastlanes_integration_test
make test
```

The extension carries the same changes in
`patches/fastlanes-branch-consolidation.patch`, applied after its original build
patch, so a clean download does not depend on an unpublished local commit.

## Recovery and deletion status

Every original origin branch tip is preserved locally under the annotated tag
`archive/branch-consolidation/origin/<branch>`. The previous known-good integration
is tagged `archive/branch-consolidation/current`. A verified full Git bundle is
stored at `~/repos/FastLanes/.git/branch-archives/pre-consolidation.bundle`.

No GitHub branches or tags have been deleted or pushed. After publishing the
consolidated branch and archive tags, the old feature branches listed above can
be retired; retain `main` and `release_v0.1`. There were no redundant local
feature branches to delete: only main and the integration worktree branch exist.
