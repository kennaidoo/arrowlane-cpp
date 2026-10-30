# Contributing

## Before you change anything

Run the gate and the container suite, so you know the tree was green before you
touched it:

```bat
scripts\gate.cmd
tools\run-tests.cmd
```

## The rules that matter

- The wire format is frozen. A change that alters the bytes on the wire is a
  format version bump, not a patch.
- Lane ids are fixed: 1 json, 2 ipc, 3 ipc-zstd, 4 shm.
- Every new behaviour needs a test that fails without it. The test prints
  `ok N tests, M checks`, and the commit message states the count.
- Interop is the acceptance test, not the unit test. If the reference disagrees,
  the reference wins unless you can show the reference is wrong.
- No em dash anywhere.
- Do not commit `vendor/`. Run the fetch script.

## Style

C++23. Prefer explicit bounds, simple control flow and visible invariants. A
loop that reads from the wire needs a cap before it allocates.

Comments explain why, not what. A comment that repeats the code is noise.

## Commits

One task per commit. The subject is a scope and a verb: `feat(chunks): add the
lazy chunk producer`. The body says what the tests printed and what you did not
do.

## Reporting a problem

Open an issue with the frame bytes if you can share them, the command you ran,
and what you expected. A failing interop check is the most useful report there
is.
