#!/usr/bin/env bash
# Completes $INSTALL (DLL dependencies, strip, documentation) and zips it.
# Usage: package.sh [previous-release-dir]
#   With a previous unpacked release, CHANGED-FILES-<version>.txt lists the
#   files that differ, so an existing installation can be updated in place.
set -e
. "$(dirname "$0")/config.sh"
VERSION=$(sed -n 's/^PACKAGE_VERSION=.\([0-9.]*\).$/\1/p' $OCP_SRC/configure)
mkdir -p $DIST

python3 - "$INSTALL" "$PREFIX/bin" "$PREFIX/lib" "$TC/$HOST/bin" <<'PY'
import os, re, shutil, subprocess, sys
inst, search = sys.argv[1], sys.argv[2:]
def deps(f):
    out = subprocess.run(['x86_64-w64-mingw32-objdump', '-p', f], capture_output=True, text=True).stdout
    return re.findall(r'DLL Name: (\S+)', out)
todo = [os.path.join(dp, f) for dp, _, fs in os.walk(inst) for f in fs if f.lower().endswith(('.exe', '.dll'))]
seen = set()
while todo:
    for d in deps(todo.pop()):
        if d.lower() in seen:
            continue
        seen.add(d.lower())
        for s in search:
            for n in os.listdir(s):
                if n.lower() == d.lower() and not os.path.exists(os.path.join(inst, d)):
                    shutil.copy(os.path.join(s, n), os.path.join(inst, d))
                    todo.append(os.path.join(inst, d))
PY
( cd $INSTALL && $HOST-strip *.exe *.dll autoload/*.dll 2>/dev/null || true )
sed 's/$/\r/' $OCP_SRC/windows/README-Windows.txt > $INSTALL/README-Windows.txt

if test -n "$1"; then
python3 - "$1" "$INSTALL" "$VERSION" <<'PY'
import hashlib, os, struct, sys
old, new, version = sys.argv[1:]
def norm(path):  # ignore the PE timestamp and checksum
    d = bytearray(open(path, 'rb').read())
    if d[:2] == b'MZ':
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        if d[pe:pe+4] == b'PE\0\0':
            d[pe+8:pe+12] = b'\0' * 4
            d[pe+24+64:pe+24+68] = b'\0' * 4
    return hashlib.sha1(d).hexdigest()
def tree(root):
    return {os.path.relpath(os.path.join(dp, f), root): os.path.join(dp, f)
            for dp, _, fs in os.walk(root) for f in fs if not f.startswith('CHANGED-FILES-')}
a, b = tree(old), tree(new)
changed = sorted(k for k in b if k in a and norm(a[k]) != norm(b[k]))
added = sorted(k for k in b if k not in a)
removed = sorted(k for k in a if k not in b)
win = lambda l: ['  ' + k.replace('/', '\\') for k in l] or ['  (none)']
title = 'Files changed in %s' % version
L = [title, '=' * len(title), '', 'New:'] + win(added + ['CHANGED-FILES-%s.txt' % version]) + \
    ['', 'Changed:'] + win(changed) + ['', 'To delete:'] + win(removed) + \
    ['  CHANGED-FILES-*.txt of previous versions', '', 'Every other file is unchanged.']
open(os.path.join(new, 'CHANGED-FILES-%s.txt' % version), 'w', newline='').write('\r\n'.join(L) + '\r\n')
PY
fi

rm -f $DIST/ocp-$VERSION-win64.zip
( cd $(dirname $INSTALL) && zip -qr $DIST/ocp-$VERSION-win64.zip $(basename $INSTALL) )
echo "$DIST/ocp-$VERSION-win64.zip"
