#!/usr/bin/env python3
"""Design + validate the two Joseph maps (camp, road) before pasting into C++.
Builds each map on a guaranteed-40x30 grid, then validates the engine's
walk/key/door/blocking-NPC rules so connectivity is trustworthy.
Dev helper, not part of the build. Prints the final C++ rows on success."""
from collections import deque

W, H = 40, 30


def blank():
    g = [['.'] * W for _ in range(H)]
    for x in range(W):
        g[0][x] = g[H - 1][x] = 'T'
    for y in range(H):
        g[y][0] = g[y][W - 1] = 'T'
    return g


def put(g, x, y, ch):
    g[y][x] = ch


def hline(g, x0, x1, y, ch):
    for x in range(x0, x1 + 1):
        g[y][x] = ch


def vline(g, x, y0, y1, ch):
    for y in range(y0, y1 + 1):
        g[y][x] = ch


def box(g, x0, y0, x1, y1, ch):
    hline(g, x0, x1, y0, ch)
    hline(g, x0, x1, y1, ch)
    vline(g, x0, y0, y1, ch)
    vline(g, x1, y0, y1, ch)


def rect(g, x0, y0, x1, y1, ch):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            g[y][x] = ch


def rows_of(g):
    return ["".join(r) for r in g]


# ============================ CAMP "Jakobs läger" ============================
# A walled camp you wind through: an inner tree hedge, a well (water), Josef locked
# inside his tent (house with door D, interior floor), the tent key hidden in a far
# pocket, Jakob by the start. Ruben (3) blocks the single gap in the north tree wall.
def build_camp():
    g = blank()
    # North tree wall across row 4 with one gap at x=28 (Ruben stands in it); X above it.
    hline(g, 1, W - 2, 4, 'T')
    put(g, 28, 4, '3')            # Ruben in the gap
    put(g, 28, 1, 'X')            # exit, north of the gap
    # Left inner hedge (vertical tree line) with a single pass-through, forces a detour.
    vline(g, 10, 15, 26, 'T')     # lower-left hedge
    vline(g, 10, 6, 13, 'T')      # upper-left hedge  (gap at rows 14 between the two)
    # Right inner hedge
    vline(g, 30, 8, 20, 'T')
    put(g, 30, 14, '.')           # a pass-through in the right hedge
    # Scattered tree clumps (cover / winding)
    for (x, y) in [(4,6),(5,6),(6,6),(4,8),(5,8),(22,6),(23,6),(24,6),(25,6),(26,6),
                   (18,8),(19,8),(34,8),(35,8),(36,8),(37,8),
                   (5,15),(6,15),(7,15),(8,15),(9,15),(24,20),(25,20),(26,20),(27,20),
                   (34,18),(35,18),(36,18)]:
        g[y][x] = 'T'
    # Josef's tent (house): walls, floor interior, door on the south wall. Josef inside.
    box(g, 17, 10, 23, 14, '#')
    rect(g, 18, 11, 22, 13, '_')
    put(g, 20, 14, 'D')           # door, south wall (approach from below)
    put(g, 20, 12, '2')           # Josef inside, on floor-adjacent interior cell
    g[12][20] = '2'               # ensure Josef cell (overwrites floor)
    # Well (sand ring + water) centre-left
    rect(g, 17, 16, 21, 19, ',')
    rect(g, 18, 17, 20, 18, '~')
    # Jakob near the start
    put(g, 4, 17, '1')
    # Checkpoint + start bottom-left
    put(g, 5, 25, 'Q')
    put(g, 4, 27, 'S')
    # Key hidden in the far bottom-right pocket, reached by winding around hedges
    put(g, 35, 26, 'k')
    # a couple of bushes for flavour (hittable, don't block the solution path badly)
    for (x, y) in [(14,22),(6,22),(33,24),(14,7),(26,12)]:
        if g[y][x] == '.':
            g[y][x] = 'b'
    return rows_of(g)


# ============================ ROAD "Vägen till Dotan" ========================
# Very different: an open caravan road winding between rock barriers around a big dry
# cistern (the pit, a sand-ringed water hole). Juda (4), a brother (5); the Gilead
# merchant (6) blocks the caravan gap north to the exit.
def build_road():
    g = blank()
    # North tree wall across row 4, gap at x=11 (merchant), X north-west of it.
    hline(g, 1, W - 2, 4, 'T')
    put(g, 11, 4, '6')            # merchant in the gap
    put(g, 11, 1, 'X')           # exit directly above the gap
    # Rock barriers (chicanes) that make the road wind. Rocks block walking in this model.
    def rockrow(y, xs):
        for x in xs:
            g[y][x] = 'R'
    rockrow(9, range(13, 19))     # upper-left chicane
    rockrow(9, range(23, 29))     # upper-right chicane
    rockrow(18, range(9, 15))     # lower chicanes
    rockrow(18, range(26, 32))
    vline(g, 14, 20, 25, 'R'); vline(g, 21, 20, 25, 'R')   # funnel near start
    # Big dry cistern in the middle: sand ring with water (the empty pit motif)
    rect(g, 14, 11, 22, 17, ',')
    rect(g, 15, 12, 21, 16, '~')
    put(g, 18, 12, ','); put(g, 18, 16, ',')   # notch the ring a touch
    # NPCs (reachable, off the rock lines)
    put(g, 7, 7, '4')            # Juda, upper-left
    put(g, 32, 15, '5')          # a brother, right side
    # Start bottom-centre
    put(g, 19, 27, 'S')
    # Optional gems + bushes
    for (x, y, ch) in [(6,22,'*'),(33,22,'+'),(6,10,'b'),(26,10,'b'),(6,24,'b'),(30,24,'b'),(20,6,'b')]:
        if g[y][x] == '.':
            g[y][x] = ch
    return rows_of(g)


# ============================ validation ====================================
def terrain_walkable(ch):
    return ch not in "T#%~^="


def find(rows, ch):
    for y, r in enumerate(rows):
        x = r.find(ch)
        if x >= 0:
            return x, y
    return None


def bfs(rows, start, have_key=False, moved_npc=None):
    grid = [list(r) for r in rows]
    if moved_npc:
        d, (nx, ny) = moved_npc
        for y in range(H):
            for x in range(W):
                if grid[y][x] == d:
                    grid[y][x] = '.'
        grid[ny][nx] = d
    seen = {start}
    q = deque([start])
    while q:
        cx, cy = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cx + dx, cy + dy
            if not (0 <= nx < W and 0 <= ny < H) or (nx, ny) in seen:
                continue
            ch = grid[ny][nx]
            if not terrain_walkable(ch):
                continue
            if ch in "RrCcb" or ch.isdigit():
                continue
            if ch == 'D' and not have_key:
                continue
            seen.add((nx, ny))
            q.append((nx, ny))
    return seen


def adj(reach, p):
    x, y = p
    return p in reach or any((x + dx, y + dy) in reach for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))


def check_struct(name, rows):
    ok = True
    for i, r in enumerate(rows):
        if len(r) != W:
            print(f"[{name}] row {i} width {len(r)}"); ok = False
    if len(rows) != H:
        print(f"[{name}] height {len(rows)}"); ok = False
    j = "".join(rows)
    for ch, n in (('S', 1), ('X', 1)):
        if j.count(ch) != n:
            print(f"[{name}] {ch} count {j.count(ch)} != {n}"); ok = False
    digs = [c for c in j if c.isdigit()]
    if len(digs) != len(set(digs)):
        print(f"[{name}] dup NPC {digs}"); ok = False
    if ok:
        print(f"[{name}] structure OK 40x30, NPCs {sorted(digs)}")
    return ok


CAMP = build_camp()
ROAD = build_road()

print("=== CAMP ===")
ok = check_struct("camp", CAMP)
s = find(CAMP, 'S'); josef = find(CAMP, '2'); key = find(CAMP, 'k')
ex = find(CAMP, 'X'); ruben = find(CAMP, '3'); jakob = find(CAMP, '1')
r0 = bfs(CAMP, s, have_key=False)
r1 = bfs(CAMP, s, have_key=True)
r2 = bfs(CAMP, s, have_key=True, moved_npc=('3', (ruben[0], ruben[1] + 2)))
t = [("key reachable w/o key", adj(r0, key), True),
     ("Josef adj w/o key", adj(r0, josef), False),
     ("Josef adj WITH key", adj(r1, josef), True),
     ("exit reachable while Ruben blocks", ex in r1, False),
     ("exit reachable after Ruben moves", ex in r2, True),
     ("Jakob reachable", adj(r0, jakob), True)]
for label, got, want in t:
    print(f"   {label}: {got} (want {want})" + ("" if got == want else "  <<< FAIL"))
    ok &= (got == want)
camp_ok = ok

print("=== ROAD ===")
ok = check_struct("road", ROAD)
s = find(ROAD, 'S'); ex = find(ROAD, 'X'); merch = find(ROAD, '6')
r0 = bfs(ROAD, s)
r1 = bfs(ROAD, s, moved_npc=('6', (merch[0], merch[1] + 2)))
t = [("Juda(4) reachable", adj(r0, find(ROAD, '4')), True),
     ("brother(5) reachable", adj(r0, find(ROAD, '5')), True),
     ("exit reachable while merchant blocks", ex in r0, False),
     ("exit reachable after merchant moves", ex in r1, True)]
for label, got, want in t:
    print(f"   {label}: {got} (want {want})" + ("" if got == want else "  <<< FAIL"))
    ok &= (got == want)
road_ok = ok

same = sum(1 for a, b in zip(CAMP, ROAD) if a == b)
print(f"=== DIFFERENCE: identical rows {same}/{H} ===")
differ = same < H // 2

print()
print("CAMP:", "PASS" if camp_ok else "FAIL")
print("ROAD:", "PASS" if road_ok else "FAIL")
print("DIFFER:", "PASS" if differ else "FAIL")
if camp_ok and road_ok and differ:
    print("\n--- CAMP C++ ---")
    for i, r in enumerate(CAMP):
        print(f'    "{r}",  // {i}')
    print("\n--- ROAD C++ ---")
    for i, r in enumerate(ROAD):
        print(f'    "{r}",  // {i}')
import sys
sys.exit(0 if (camp_ok and road_ok and differ) else 1)
