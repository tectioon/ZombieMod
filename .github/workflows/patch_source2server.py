# CS2 1.42.0.1 (2026-11-24) added a virtual to ISource2Server right after GameCreateNetworkStringTables (it clears the
# game's network string tables), so every ISource2Server call / hook after it is one slot off: a GameFrame hook lands on
# ApplyGameSettings and the server segfaults while the map loads (alliedmodders/s2sdk#128, CounterStrikeSharp #1504 / 060a16f).
# Inserts that virtual into an SDK checkout; a no-op if it's already there. Takes the SDK root and looks in every header that
# declares ISource2Server (public/eiface.h in older pins, public/engine2/isource2server.h in newer ones).
import os
import re
import sys

root = sys.argv[1]
MARK = "ClearNetworkStringTables_ZMPATCH"
candidates = [os.path.join(root, "public", "eiface.h"), os.path.join(root, "public", "engine2", "isource2server.h")]
patched = 0
for path in candidates:
    if not os.path.exists(path):
        continue
    src = open(path, encoding="utf-8", newline="").read()
    m = re.search(r"abstract_class\s+ISource2Server\s*:\s*public\s+IAppSystem\s*\{", src)
    if not m:
        continue
    body_start = m.end()
    anchor = re.compile(r"virtual\s+void\s+GameCreateNetworkStringTables\s*\(\s*void\s*\)\s*=\s*0\s*;")
    a = anchor.search(src, body_start)
    assert a, "GameCreateNetworkStringTables not found in ISource2Server in " + path
    # Already there (upstream fix or an earlier run): the next virtual after the anchor isn't WriteSignonMessages
    nxt = re.search(r"virtual[^;]*;", src[a.end():])
    if MARK in src or (nxt and "WriteSignonMessages" not in nxt.group(0)):
        print("already patched:", path)
        patched += 1
        continue
    nl = "\r\n" if "\r\n" in src else "\n"
    insert = nl + nl + "\t// CS2 1.42.0.1: clears the game's network string tables" + nl + "\tvirtual void\t\t\t" + MARK + "( void ) = 0;"
    src = src[:a.end()] + insert + src[a.end():]
    open(path, "w", encoding="utf-8", newline="").write(src)
    print("patched:", path)
    patched += 1
assert patched, "ISource2Server not found under " + root
