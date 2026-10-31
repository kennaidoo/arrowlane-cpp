# Release checklist

Everything that must be true before this repository becomes public, and the exact
commands. Nothing here is a judgement call except the two marked decisions.

## 1. The tree is clean of things that must not ship

```bash
git log --all -p | grep -nEi '(sk-[A-Za-z0-9]{16,}|ghp_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|BEGIN (RSA|OPENSSH) PRIVATE KEY)'
git grep -nIE 'C:[/\\]Users[/\\]Ken|/c/Users/Ken|DESKTOP-|kennedynaidoo@|109119580' -- .
git ls-files | xargs -I{} du -k "{}" 2>/dev/null | sort -rn | head -10
```

Expected: no output from the first two, and the largest tracked file is well
under a megabyte. `vendor/`, `build*/` and `.hermes/` are ignored, so zstd and
the plans never enter history.

## 2. The suite is green on both compilers

```bat
scripts\gate.cmd
tools\run-tests.cmd
```

Expected: `RESULT: PASS` and `100% tests passed out of 24` with six interop
checks. The container is the only place the POSIX lanes run.

## 3. The performance record is current and honest

```bat
python bench\compare.py --rows 200000 --repeats 7 --rounds 3 --check
```

Expected: `bench/results/latest.md` is regenerated and committed. `--check` exits
1 while any row is undetermined or lost. That is acceptable to publish, but only
if `docs/performance.md` says so in the same words.

## 4. Version and tag

Version is 0.1.0 in `include/arrowlane/version.hpp`, in `docs/ai/manifest.json`,
and in `README.md`. The tag matches:

```bash
git tag -l
git describe --tags
```

Expected: `v0.1.0` only. Do not tag 1.0.0 before a release exists.

## 5. The public surface is described

| File | What it must say |
|---|---|
| `README.md` | what it is, what works, what does not, how to build, how to test, where the numbers are |
| `LICENSE` | Apache 2.0 |
| `THIRD_PARTY_NOTICES.md` | every ported algorithm and every fetched library, with its licence |
| `SECURITY.md` | how to report a problem privately |
| `CONTRIBUTING.md` | how to build, test and commit |
| `AGENTS.md` | the rules an automated contributor must follow |
| `docs/performance.md` | every measured number, every missed target, and every unproven row |
| `docs/decisions.md` | why the format, the version and the checksum are what they are |

## 6. Publish

Decision: repository visibility and the account it lives under. Both belong to
Kennedy.

```bash
gh repo create kennaidoo/arrowlane-cpp --public --source=. --remote=origin \
  --description "A C++23 implementation of the arrowlane wire frame, Arrow schema codec and four transport lanes"
git push -u origin main
git push origin v0.1.0
```

Then on the public repository:

```bash
gh repo edit kennaidoo/arrowlane-cpp --add-topic arrow,apache-arrow,cpp23,ipc,flatbuffers,zero-copy
gh run list --limit 3
```

Expected: the `ci` workflow runs on the first push and both jobs pass. The
Windows job fetches zstd and builds with MSVC; the Linux job builds with GCC 14
and sanitizers.

## 7. After the first public CI run

- If both jobs pass, the release is live. Nothing else is required.
- If a job fails, fix forward on `main`. Do not delete the tag: the source is
  correct, the workflow is what failed.

## 8. What stays open after publication

These are recorded, not hidden:

| Item | State |
|---|---|
| ipc encode row against Rust | undetermined on this machine; the reason and the numbers are in `docs/performance.md` |
| release binary size | 1,122,816 bytes against a 512 KB target; zstd is most of it |
| startup time and peak RSS for Rust and Zig | unproven; needs reference images with a time utility |

## 9. Reverse

If something must be unpublished:

```bash
gh repo edit kennaidoo/arrowlane-cpp --visibility private --accept-visibility-change-consequences
```

History stays intact, so a later fix can be published again without rewriting
anything.
