# Portable regressions

Run without Wine or the game:

```sh
cmake -S tests -B /tmp/sunrise-portable-tests
cmake --build /tmp/sunrise-portable-tests
ctest --test-dir /tmp/sunrise-portable-tests --output-on-failure
```

The fixture contains 71 original general/leg member rows and 57 opaque condition records captured
from the Arrivals build on 2026-09-04, before relocation. Each 64-byte record contains the 32-byte
member followed by its 32-byte condition allocation (zero for an empty condition). The first 19
members are general; the remaining 52 are legs. Original relative offsets are deliberately retained
so the test detects the shallow-copy bug. There are no account records or absolute process addresses.

The test uses the production relocation header. It checks membership/order, payload preservation,
relocation to another allocation, and rejection of bad pointers, substituted conditions, duplicates,
unexpected shapes, insufficient storage, and misalignment. Assertions remain enabled in Release.

`lore_visibility_test` checks all 36 targeted native expression shapes, rejects changed or
already-patched inputs, and evaluates the repaired conditions with fresh, partial, and completed
unlock reads. Only visibility fields are targeted. See `docs/lore-visibility.md` for the evidence.
