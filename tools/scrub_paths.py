#!/usr/bin/env python3
# scrub_paths.py <file> <prefix>...: each build-machine folder (prefix) named in the file's text, mostly source file
# names kept for error messages, becomes "/src" and slashes of the same length, so the file keeps its layout and
# nothing of the build machine travels with it. Prints how many were replaced.
import sys

path, prefixes = sys.argv[1], [p for p in sys.argv[2:] if p]
data = open(path, 'rb').read(); n = 0
for p in prefixes:
    for form in {p, p.replace('/', '\\')}:
        b = form.encode()
        if len(b) < 5:
            continue
        rep = (b'/src' + b'/' * len(b))[:len(b)]
        n += data.count(b); data = data.replace(b, rep)
open(path, 'wb').write(data)
print('%s: %d build paths scrubbed' % (path.replace('\\', '/').split('/')[-1], n))
