# Security

## What this library parses

Every byte that arrives is untrusted. The parser reads a prefix, then a header,
then a payload, and it caps each length before it allocates:

- header 65,535 bytes
- payload 536,870,912 bytes
- uncompressed 1,073,741,824 bytes
- chunk count 1,000,000
- rows per chunk 10,000,000

A frame that declares more is rejected with `OversizeFrame`. A frame whose CRC
does not match is rejected with `ChecksumMismatch`. A chunk stream with a gap, a
duplicate, a changed id, or a frame after the last one is rejected with `Chunk`.

The shm lane compares tokens in constant time, creates segments with mode 0600
and `O_EXCL`, verifies the CRC after mapping, and removes the name before the
mapping is released.

## Reporting a vulnerability

Report privately to the repository owner rather than in a public issue. Include
the input bytes if you can, the build type, and the platform. A crash, a hang,
or a read past the end of a buffer all count.

Do not open a public issue for a memory safety problem before a fix exists.

## Supported versions

The current minor version receives fixes. This project is alpha, so the version
will move quickly, and older versions will not be patched.

## What is out of scope

- A malicious cost model file. It is local configuration, not remote input, and
  the parser validates every field before use.
- Resource exhaustion by a caller who chooses to decode a 512 MB payload. The
  limits bound the allocation; they do not make it free.
