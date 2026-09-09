# KieeKey v1.2.2 Stable — frozen engine copy (benchmark input, not product code)

Byte-for-byte copies of `src/core/` as shipped in v1.2.2 Stable, taken from the
repository at the commit that froze this benchmark baseline. Nothing here is
built by the product; `benchmark/scripts/build.sh` compiles it into
`libkkbase.so` so the campaign can measure **v1.2.2 and the current tree in the
same process, the same round, the same core** — the only way to state a
candidate's gain without campaign-to-campaign drift contaminating it.

`UPSTREAM-SHA256.txt` is verified on every build (`verify_pristine`); if this
copy ever changes, the differential gate and the gain numbers lose their
meaning and the build stops. Restore it with:

    for f in $(git ls-tree --name-only <baseline-commit> src/core/); do \
      git show "<baseline-commit>:$f" > benchmark/reference/kieekey-1.2.2/$(basename $f); done
