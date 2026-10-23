# CS2 1.41.9.2 (2026-10-22) added IVEngineServer2::IsSpawnGroupLoadedForClient right after MakeSpawnGroupActive
# (alliedmodders/hl2sdk#438). Inserts it into an SDK checkout's public/eiface.h; a no-op if it's already there.
import sys

path = sys.argv[1]
src = open(path, encoding="utf-8", newline="").read()
if "IsSpawnGroupLoadedForClient" in src:
    print("already patched:", path)
    sys.exit(0)
anchor = "virtual void\tMakeSpawnGroupActive( SpawnGroupHandle_t spawnGroup ) = 0;"
assert src.count(anchor) == 1, "MakeSpawnGroupActive not found once in " + path
nl = "\r\n" if "\r\n" in src else "\n"
src = src.replace(anchor, anchor + nl + "\tvirtual bool\tIsSpawnGroupLoadedForClient( CPlayerSlot nSlot, SpawnGroupHandle_t spawnGroup ) = 0;")
open(path, "w", encoding="utf-8", newline="").write(src)
print("patched:", path)
