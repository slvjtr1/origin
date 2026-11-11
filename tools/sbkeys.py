#!/usr/bin/env python3
# The built-in keys header: tools/sbkeys.py <keys.env> <out sbkeys.h>. Each key scrambled with a pad (not plain
# text in the binary). Used by tools/build.sh; ports/androidtv/app/build.gradle.kts makes the same.
import os, sys, hashlib
src, out = sys.argv[1], sys.argv[2]; kv = {}
if os.path.isfile(src):
    for ln in open(src):
        ln = ln.strip()
        if ln and not ln.startswith('#') and '=' in ln: k, v = ln.split('=', 1); kv[k.strip()] = v.strip().strip('"\'')
def arr(name):
    v = kv.get(name, '').encode(); pad = hashlib.sha256(b'sb:' + name.encode()).digest()
    return 'static const unsigned char SBK_%s[] = { %s0 }, SBK_%s_PAD[32] = { %s };\n#define SBK_%s_LEN %d\n' % (name, ''.join('%d,' % (c ^ pad[i % 32]) for i, c in enumerate(v)), name, ','.join(str(x) for x in pad), name, len(v))
body = '/* made by tools/build.sh from the private keys file: never commit */\n' + arr('TMDB_API_KEY')
if not os.path.isfile(out) or open(out).read() != body: open(out, 'w').write(body)
print('== keys built in: %s' % (', '.join(n for n in ('TMDB_API_KEY',) if kv.get(n)) or 'none (%s)' % src))
