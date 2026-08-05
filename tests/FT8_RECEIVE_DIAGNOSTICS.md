# FT8 receive diagnostics

Run from the repository root:

```sh
./tests/run_ft8_receive_diagnostics.sh
```

For AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
SANITIZE=1 ./tests/run_ft8_receive_diagnostics.sh
```

The test uses the exact 77-bit payloads and 79-tone sequences printed by
reference WSJT-X `ft8code` for:

- `CQ OK/SP5DAA`
- `CQ OK/SP5DAA/P`

It performs two independent checks:

1. direct payload unpacking through `ftx_message_decode()`;
2. GFSK synthesis followed by the zBitx-style FFT waterfall,
   candidate search, LDPC/CRC decoding and message unpacking.

The audio test is repeated at timing shifts of -0.10, 0 and +0.10 seconds.
The output reports candidate count, best sync score, successful channel
decodes and unpack failures.

Interpretation:

- payload failure: message type-4 unpacking is incompatible with WSJT-X;
- payload passes but audio fails: candidate search, FFT, LDPC or CRC path;
- all tests pass but radio reception fails: inspect the captured zBitx audio,
  slot timing, input level/AGC and application transport/display path.
