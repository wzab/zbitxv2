# Shawn ft8_lib port for zBitx V2

Base zBitx V2 commit: b10422f2e36bcc822ed024f5471303a5ff5c8957
Shawn ft8_lib commit: 5cd269099ab8ca226cfb9a4e62b304bed2e20673

Changes:
- replaced the in-tree ft8_lib with Shawn Rutledge's pinned revision;
- retained all zBitx V2 files, including the Wi-Fi GUI changes in the base commit;
- enlarged application-side FT8 token/callsign buffers;
- added compound-callsign regression vectors, including both stations using compound callsigns.

Build on the zBitx:

    make clean
    make -j2

The full RF/audio regression still requires running on the zBitx or feeding known FT8 WAV captures. A successful compile alone does not prove hash-cache behaviour.
