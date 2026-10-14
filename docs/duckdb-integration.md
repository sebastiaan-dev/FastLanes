# DuckDB 1.5.6 integration branch

The `fix/duckdb-1.5.6` branch is based on
`sebastiaan-dev/fastlanes-patched@7f91afdbd916d24ab5b39a990fbdf7d9157c0f69`.
This consolidates the projection, statistics, DATE, owned-span, table-API,
parallel writer, positional-I/O and buffer-pool changes required by
`sebastiaan-dev/duckdb-fastlanes`. The older experimental branches in this fork
are preserved; this branch does not rebase those patches onto current upstream
`dev`.

Two additional build fixes:

- Respect an embedding project's compiler/toolchain instead of forcibly changing
  cached C/C++ compilers. Standalone builds still default to Clang.
- Value-initialize `TypedStats<PT>::last_seen_val`, which must not construct a
  `std::string` from a null pointer.

The extension pins the same source revision and carries these two changes in
`patches/fastlanes-build.patch`, so it can also build without this local checkout.
Subsequent [branch consolidation](branch-consolidation.md) restores writer
option forwarding and safe deferred positional I/O. Those changes are mirrored
in the extension's `patches/fastlanes-branch-consolidation.patch`.

For paired development, both repositories have a Worktrunk-managed
`fix/duckdb-1.5.6` worktree under `~/repos`:

```sh
cd ~/repos/duckdb-fastlanes.fix-duckdb-1.5.6
CC=clang CXX=clang++ CMAKE_BUILD_PARALLEL_LEVEL=10 make release GEN=ninja \
  FASTLANES_SOURCE_DIR=../FastLanes.fix-duckdb-1.5.6
make test
```

Validated through the DuckDB 1.5.6 extension on macOS arm64 / Apple Clang 21.
The extension's `docs/development.md` describes the migration and test coverage.

## CI follow-ups

Compatibility CI also exercises the native library, examples, and bindings:

- Writer forwarding uses explicit template arguments supported by Clang 18.
- `Connection::to_fls()` retains its table for subsequent inspection/comparison,
  and already-padded CSV vectors are not padded a second time.
- Native FSST/FSST12 decoding allocates scratch storage on demand.
- Nested statistics are initialized recursively; temporal/decimal columns select
  their physical codecs without discarding logical metadata.
- Windows uses binary, non-inheritable CRT descriptors with 64-bit sizes/offsets
  and a per-file lock around seek/read. POSIX retains concurrent `pread`.
- clang-cl receives the GNU `-Wall` group explicitly, not MSVC's `/Wall`.

Local Apple Clang 21 validation includes all **260 native CTest cases** (excluding
QuickFuzz, as in CI), the standalone I/O/materialization regressions, and the Rust
example. The extension mirrors these fixes in its checked-in patch stack.
