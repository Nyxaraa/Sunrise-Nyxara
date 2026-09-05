# Fresh-state lore visibility repair

The earlier earned-objective repair did not satisfy the fresh-state requirement. It and
the Confessions Family-5 override have been removed. Account projection, settings, and
claim persistence now remain unchanged from the confirmed title-fix commit.

Read-only inspection on 2026-09-04 confirmed these expressions in the running Arrivals
client, not merely the manifest. Nodes and records both carry their identity hash at +40.

| Target | Row/field | Original condition | Change |
| --- | --- | --- | --- |
| Eva's Journey | node 820, +64 | NOT VALUE(10343) | Replace read with constant 1; NOT true is false. |
| The Chronicon | node 837, +64 | 15 chapter-value comparisons with zero, ANDed together | Replace the first read with constant 1; the first comparison is false, so the conjunction is false regardless of progress. |
| Wall of Wishes | records 825–839, +120 | constant true | constant false |
| Confessions parent | record 1707, +136 | NOT FLAG(8702) | Replace read with constant 1. |
| Confessions chapters | records 1708–1716, +120 and +136 | NOT FLAG(8702); constant true | Replace read with constant 1; replace true with false. |

There are 36 edited instructions. All other instructions and all array descriptors remain
unchanged. This avoids relocation or allocation of expression storage. Completion flags,
objectives, reward data, and claims are not edited. Revealing a card does not earn it.

Native tables: records `0x81319339` (2242 rows × 216 bytes), nodes `0x8131933F`
(924 rows × 168 bytes). Preflight checks exact target hashes, complete expression shapes,
array headers and bounds. It also walks both tables' presentation-condition descriptors
to require each edited instruction to have exactly one owner; shared constants are refused.
No writes occur until the whole preflight succeeds. Writes are checked and rolled back on
failure. Original instructions are retained for hook teardown.

The patch runs synchronously at the existing native content-table-patching-complete event,
before returning to investment initialization. It does not poll, scan process memory,
or change the working socket implementation. Expected success log:
`ev=lore_visibility result=applied conditions=36 progress_unchanged=1`.

Portable tests validate every shipped expression shape, reject malformed/already-edited
expressions, and evaluate the patched conditions with zero, partial, and completed reads.
These tests do not substitute for confirming the UI after relaunch.
