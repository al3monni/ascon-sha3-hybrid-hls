#!/usr/bin/env python3
"""
gen_golden.py - Generate golden.txt for the equivalence test from references
that are independent of this repo's C code:

  SHA3-256 / SHA3-512 : Python hashlib (FIPS 202)
  Ascon-Hash256       : pyascon, the designers' Python reference (NIST SP 800-232)
                        https://github.com/meichlseder/pyascon

Usage:  python3 gen_golden.py <path-to-pyascon-clone> > golden.txt

Same message and length range as kat_sweep.c: msg[i] = (i*167 + 13) mod 256,
len = 0..256. golden.txt is committed; regenerating it is only needed if the
message or the length range change.
"""

import hashlib
import sys

sys.path.insert(0, sys.argv[1])
import ascon  # noqa: E402  (pyascon/ascon.py)

MAX_LEN = 256
MSG = bytes((i * 167 + 13) % 256 for i in range(MAX_LEN))

ALGOS = [
    ("sha3-256", lambda m: hashlib.sha3_256(m).digest()),
    ("sha3-512", lambda m: hashlib.sha3_512(m).digest()),
    ("ascon",    lambda m: ascon.ascon_hash(m, "Ascon-Hash256", 32)),
]

for name, fn in ALGOS:
    for n in range(MAX_LEN + 1):
        print(f"{name} {n} {fn(MSG[:n]).hex().upper()}")
