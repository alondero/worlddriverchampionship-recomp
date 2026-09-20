# Testing

Tests should cross the smallest useful seam and use the strongest repeatable
evidence available.

| Evidence | Use |
| --- | --- |
| Host unit test | Pure policy, parsing, math, or classification |
| Synthetic RDRAM test | Guest-memory layout and byte-order contracts |
| ROM-backed test | Translated function or display-list boundary |
| Bounded end-to-end run | Startup, renderer, audio, input, or device path |
| Manual capture | Visual acceptance only, with exact repro details |

Run the source-only checks from the repository root:

```bash
python -B scripts/check_docs.py
python -m unittest discover tests
git diff --check
```

Once the ROM and generated image exist:

```bash
ctest --test-dir build --output-on-failure
```

Headless runs must have a bounded frame/VI budget and a machine-readable result
when they are used in CI. A passing process exit alone is not proof that the
game reached the intended scene or submitted a graphics/audio task.
