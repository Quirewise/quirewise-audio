# Audio AES source record

The AES implementation in this directory is vendored in the Quirewise Audio
repository and is covered by the accompanying `UNLICENSE.txt`. Its source
comments reference the `kokke/tiny-AES-C` project and its AES implementation
notes reference NIST SP 800-38A test vectors. The workspace does not record an
upstream revision identifier, so this record does not claim a commit pin.

Quirewise Audio uses this source only for AES-128 ECB decryption in NCM key and
metadata handling. `libraries/quirewise-audio/CMakeLists.txt` adds `aes.c`
explicitly to `Quirewise::Audio` and compiles it with CBC and CTR disabled.
`tests/test_aes.c` verifies four AES-128 ECB decrypt vectors from NIST SP
800-38A through the linked Audio target. The AES header remains private and is
not installed as a Quirewise public API.

The Quirewise patch clears the temporary word used by key expansion in `aes.c`
and clears the expanded key schedule after NCM AES operations in
`src/formats/audio/encrypted_music.c`.
