#!/usr/bin/env python3
# scrub_ipk.py <file.ipk>: the archives record who built them (the user and group names, the ids); the package
# carries "root" and 0 instead, so nothing of the build machine travels with it. The contents are unchanged.
# File modes are set as on a Mac build: 0644, and 0755 for the app binary and its .so libraries.
import io, sys, tarfile

def scrub_tar(blob, gz):
    src = tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz' if gz else 'r:')
    out = io.BytesIO()
    dst = tarfile.open(fileobj=out, mode='w:gz' if gz else 'w', format=tarfile.GNU_FORMAT)
    for m in src.getmembers():
        m.uid = m.gid = 0; m.uname = m.gname = 'root'
        if m.isfile():   # packaged on Windows every file comes out 0666: the app binary and its libraries must stay executable
            m.mode &= ~0o022
            if m.name.endswith(('/stremioorigin', '.so')): m.mode |= 0o755
        dst.addfile(m, src.extractfile(m) if m.isfile() else None)
    dst.close(); return out.getvalue()

def main(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'!<arch>\n', 'not an ar archive'
    i, members = 8, []
    while i < len(data):
        h = data[i:i + 60]; name = h[:16].decode().strip(); size = int(h[48:58].decode().strip()); body = data[i + 60:i + 60 + size]
        members.append((name, h, body)); i += 60 + size + (size & 1)
    out = bytearray(b'!<arch>\n')
    for name, h, body in members:
        n = name.rstrip('/')
        if n.endswith('.tar.gz'): body = scrub_tar(body, True)
        elif n.endswith('.tar'): body = scrub_tar(body, False)
        hdr = (name.ljust(16) + h[16:28].decode() + '0'.ljust(6) + '0'.ljust(6) + h[40:48].decode() + str(len(body)).ljust(10) + '`\n').encode()
        out += hdr + body + (b'\n' if len(body) & 1 else b'')
    open(path, 'wb').write(out)
    # checked again from the file: every entry of every inner archive now says root
    d = bytes(out); i = 8
    while i < len(d):
        h = d[i:i + 60]; n = h[:16].decode().strip().rstrip('/'); size = int(h[48:58].decode().strip()); body = d[i + 60:i + 60 + size]; i += 60 + size + (size & 1)
        if n.endswith('.tar.gz') or n.endswith('.tar'):
            for m in tarfile.open(fileobj=io.BytesIO(body), mode='r:gz' if n.endswith('.gz') else 'r:').getmembers():
                if m.uname not in ('root', '') or m.gname not in ('root', '') or m.uid or m.gid: sys.exit('scrub failed: ' + m.name)

if __name__ == '__main__':
    main(sys.argv[1])
