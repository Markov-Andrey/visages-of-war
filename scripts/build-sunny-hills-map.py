"""Build the authored Sunny Hills asset-review landscape (standard-library Python only)."""
import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / 'assets/world/tilesets/sunny-hills'
SIZE = 96
RNG = random.Random(6102026)
def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))
def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

objects = {}
for category in ['trees', 'shrubs', 'rocks', 'coast', 'ruins', 'vineyards']:
    for definition in read(PACK / (category + '.json')):
        objects[definition['id']] = definition
heights = [[0] * SIZE for _ in range(SIZE)]
surfaces = [['L'] * SIZE for _ in range(SIZE)]
blocked = [['0'] * SIZE for _ in range(SIZE)]
ramps = {}
occupied = set()
reserved = set()
scenario = {'format': 'rts-world', 'name': 'Sunny Hills — Coast, Vineyards & Ruins', 'size': [SIZE, SIZE],
            'start': {'hall': [33, 77], 'workers': [[34, 81], [35, 81], [36, 81]], 'hero': [38, 81], 'crystals': 1500},
            'terrain': {}, 'paint': [], 'environment': [], 'decorations': [], 'resources': [], 'units': []}
next_id = 1000

def inside(x, y, polygon):
    result = False
    previous = polygon[-1]
    for current in polygon:
        ax, ay = previous; bx, by = current
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            result = not result
        previous = current
    return result

def stamp(material, x, y, radius, opacity=1, hardness=.5):
    scenario['paint'].append({'material': 'sunny_hills.' + material, 'position': [round(x, 3), round(y, 3)],
                              'radius': radius, 'opacity': opacity, 'hardness': hardness, 'erase': False})

def road(points, material='dirt', radius=1.4, reserve=2.1):
    for a, b in zip(points, points[1:]):
        length = math.dist(a, b)
        count = max(1, math.ceil(length / .75))
        for step in range(count + 1):
            t = step / count
            x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            stamp(material, x, y, radius, .88, .5)
            for cy in range(max(0, int(y - reserve)), min(SIZE, math.ceil(y + reserve))):
                for cx in range(max(0, int(x - reserve)), min(SIZE, math.ceil(x + reserve))):
                    if math.hypot(cx + .5 - x, cy + .5 - y) < reserve:
                        reserved.add((cx, cy))

def place(key, x, y, *, scale=1, force=False):
    global next_id
    key = key if key.startswith('sunny_hills.') else 'sunny_hills.' + key
    d = objects[key]
    if not (0 <= x < SIZE and 0 <= y < SIZE):
        return False
    if not d['gameplay']:
        scenario['decorations'].append({'id': next_id, 'asset': key, 'position': [round(x, 3), round(y, 3)], 'scale': round(scale, 3), 'rotation': 0})
    else:
        x, y = int(x), int(y)
        w, h = d['footprint']
        cells = [(x + dx, y + dy) for dy in range(h) for dx in range(w)]
        if any(cx >= SIZE or cy >= SIZE or surfaces[cy][cx] != 'L' or heights[cy][cx] != heights[y][x] or
               (cx, cy) in occupied or (cx, cy) in ramps or (not force and (cx, cy) in reserved) for cx, cy in cells):
            return False
        scenario['environment'].append({'id': next_id, 'asset': key, 'cell': [x, y], 'health': d['health']})
        occupied.update(c for c, collision in zip(cells, d['collision']) if collision)
    next_id += 1
    return True

def ramp_lane(x, y, dx, dy):
    for offset in [-1, 0, 1]:
        cx, cy = x - dy * offset, y + dx * offset
        assert surfaces[cy][cx] == surfaces[cy + dy][cx + dx] == 'L'
        assert heights[cy + dy][cx + dx] == heights[cy][cx] + 1, (cx, cy)
        ramps[cx, cy] = [dx, dy]
        for step in [-1, 0, 1]:
            reserved.add((cx + dx * step, cy + dy * step))

# The sea follows an irregular western shore. The northwest headland projects over it.
coast = []
for y in range(SIZE):
    edge = round(14 + 2.8 * math.sin(y / 10) + 1.3 * math.sin(y / 4.1))
    if 19 <= y <= 29:
        edge = min(edge, 11 + abs(y - 24) // 3)
    if 61 <= y <= 69:
        edge = 13
    coast.append(edge)
    for x in range(edge):
        surfaces[y][x] = 'D' if x < edge - 3 else 'S'
        heights[y][x] = -1
    if 51 <= y <= 73:
        for x in range(edge, edge + 3):
            heights[y][x] = -1
    if y % 2 == 0:
        stamp('sand', edge + 1.5, y + .5, 3.4, .95, .58)

# Three uneven hill systems divide the walkable valleys and road approaches.
headland = [(12, 20), (16, 16), (25, 16), (31, 20), (31, 31), (24, 35), (16, 32), (12, 27)]
forum = [(38, 35), (47, 32), (55, 37), (56, 47), (51, 54), (40, 54), (35, 45)]
north_ridge = [(52, 4), (62, 1), (64, 22), (59, 34), (52, 30), (48, 17)]
south_ridge = [(59, 65), (63, 68), (64, 84), (59, 90), (54, 80), (54, 72)]
for y in range(SIZE):
    for x in range(66):
        if surfaces[y][x] != 'L':
            continue
        if any(inside(x + .5, y + .5, polygon) for polygon in [headland, forum, north_ridge, south_ridge]):
            heights[y][x] = 1
        if 18 <= x <= 26 and 20 <= y <= 26:
            heights[y][x] = 2
        if (55 <= x <= 60 and 9 <= y <= 23) or (58 <= x <= 61 and 73 <= y <= 81):
            heights[y][x] = 2
# Hand-authored, three-cell approaches through the terrace edges.
for x in range(43, 46):
    heights[53][x] = 1; heights[54][x] = 0; heights[55][x] = 0
for x in range(44, 47):
    heights[35][x] = 1; heights[34][x] = 0; heights[33][x] = 0
ramp_lane(44, 54, 0, -1)
ramp_lane(45, 34, 0, 1)
ramp_lane(31, 28, -1, 0)
ramp_lane(27, 24, -1, 0)
ramp_lane(15, 65, 1, 0)

# Warm meadow patches, bare summits and sandy margins sit below all road paint.
for _ in range(100):
    x, y = RNG.uniform(19, 63), RNG.uniform(3, 93)
    stamp('dry_grass', x, y, RNG.uniform(2.4, 5.8), RNG.uniform(.25, .68), .25)
for x, y in [(56, 13), (58, 20), (59, 76), (24, 24), (28, 44)]:
    stamp('dirt', x, y, 4, .66, .3)
for x, y, radius in [(44, 44, 6.5), (46, 49, 4), (23, 24, 3.5)]:
    stamp('ancient_paving', x, y, radius, .95, .7)

roads = [([(35, 82), (37, 76), (34, 68), (36, 61), (43, 58), (44, 54), (44, 49), (45, 42), (45, 34), (41, 27), (37, 20), (34, 10)], 'dirt', 1.5),
         ([(41, 32), (36, 30), (32, 28.5), (29, 28.5), (28.5, 24.5), (24, 24.5)], 'dirt', 1.25),
         ([(36, 64), (29, 63), (23, 67), (18, 65), (14, 65)], 'dirt', 1.25),
         ([(36, 74), (43, 74), (49, 69), (51, 63), (60, 60), (67, 60), (70, 60)], 'cobblestone', 1.5),
         ([(42, 57), (51, 58), (60, 60)], 'dirt', 1.15),
         ([(35, 82), (41, 88), (49, 92), (60, 92), (67, 90), (70, 90)], 'dirt', 1.15),
         ([(38, 21), (43, 16), (44, 8)], 'dirt', 1.0),
         ([(34, 68), (27, 75), (25, 84), (30, 88)], 'dirt', 1.0)]
for points, material, radius in roads:
    road(points, material, radius)
# Vineyard service lanes intentionally wind between the planted blocks.
for points in [[(43, 74), (43, 81), (49, 84)], [(43, 78), (51, 78)], [(30, 12), (29, 18), (34, 22)], [(27, 75), (29, 80)]]:
    road(points, radius=.75, reserve=1.2)
road([(67, 3), (67, 93)], 'cobblestone', .8, 1.2)

# Reserve the starting court and its working approaches.
for y in range(76, 84):
    for x in range(31, 40):
        reserved.add((x, y))
occupied.update((x, y) for y in range(77, 79) for x in range(33, 36))
stamp('cobblestone', 35, 79, 3.8, .92, .68)

# Villas, their courtyards, vineyards, orchard rows and estate props.
for key, x, y in [('vineyard.winery', 38, 68), ('vineyard.cellar', 47, 73), ('vineyard.winery', 27, 8),
                  ('vineyard.cellar', 24, 77), ('vineyard.well', 40, 76), ('vineyard.press', 49, 81),
                  ('vineyard.cart', 40, 84), ('vineyard.barrels', 49, 76), ('vineyard.crate', 40, 71),
                  ('vineyard.hay', 27, 82), ('vineyard.arch', 38, 65)]:
    place(key, x, y)
for x in [45, 48, 51]:
    for y in [81, 85, 88]:
        place('vineyard.vine_a' if (x + y) % 2 else 'vineyard.vine_b', x, y)
for x in [26, 29, 32]:
    for y in [14, 17, 20]:
        place('vineyard.vine_b', x, y)
for y in range(70, 87, 3):
    for x in [19, 22, 25, 28]:
        place('tree.' + RNG.choice(['olive', 'fig', 'orange', 'lemon', 'pomegranate']) + '_' + str(RNG.randint(1, 5)), x, y)
for x, y in [(32, 75), (38, 75), (31, 69), (36, 67), (42, 70), (45, 70), (26, 7), (32, 7), (25, 12), (35, 13)]:
    place('tree.cypress_' + str(RNG.choice([1, 2, 5])), x, y)

# The coastal sanctuary sits above two terraces; the ruined town has broken streets.
for key, x, y in [('ruin.temple', 19, 20), ('ruin.rotunda', 23, 20), ('ruin.monuments.statue', 19, 25),
                  ('ruin.masonry.column', 25, 26), ('ruin.pottery.amphora', 24, 28), ('ruin.ivy_gate', 17, 28),
                  ('ruin.theatre', 39, 37), ('ruin.villa', 49, 39), ('ruin.fountain', 47, 46),
                  ('ruin.gateway', 38, 48), ('ruin.arcade', 51, 49), ('ruin.dais', 46, 38),
                  ('ruin.portico', 40, 44), ('ruin.stairway', 50, 35)]:
    place(key, x, y)
ruin_small = [k for k, v in objects.items() if k.startswith('sunny_hills.ruin.') and '.' in k[len('sunny_hills.ruin.'):]]
for _ in range(95):
    x, y = RNG.uniform(36, 55), RNG.uniform(34, 54)
    if surfaces[int(y)][int(x)] == 'L' and heights[int(y)][int(x)] == 1:
        place(RNG.choice(ruin_small), x, y)

# Boulders and thick groves make real barriers rather than invisible painted walls.
for _ in range(120):
    x, y = RNG.randint(49, 62), RNG.randint(3, 89)
    if heights[y][x] > 0 and not inside(x, y, forum):
        place('rock.' + RNG.choice(['medium_', 'large_']) + str(RNG.randint(1, 6)), x, y)
for cx, cy, rx, ry in [(26, 43, 6, 8), (23, 90, 6, 4), (46, 24, 5, 7), (59, 44, 5, 10), (34, 3, 10, 3)]:
    for y in range(max(1, cy - ry), min(94, cy + ry + 1)):
        for x in range(max(1, cx - rx), min(64, cx + rx + 1)):
            distance = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if distance < .8 or distance < 1.1 and RNG.random() < .65:
                if RNG.random() < .7:
                    place('tree.' + RNG.choice(['oak', 'stone_pine', 'olive']) + '_' + str(RNG.randint(1, 5)), x, y)
                else:
                    place('rock.medium_' + str(RNG.randint(1, 6)), x, y)
for _ in range(140):
    x, y = RNG.randint(18, 63), RNG.randint(2, 93)
    if RNG.random() < .55:
        place('tree.' + RNG.choice(['olive', 'stone_pine', 'cypress']) + '_' + str(RNG.randint(1, 5)), x, y)
    else:
        place('rock.medium_' + str(RNG.randint(1, 6)), x, y)

# Fishermen's beach and pale coastal rocks; tiny marine motifs remain in the gallery.
for key, x, y in [('coast.boat', 14, 59), ('coast.mooring', 15, 60), ('coast.cargo', 14, 69),
                  ('coast.fishing_nets', 14, 71), ('coast.driftwood', 14.5, 63.5), ('coast.broken_amphora', 16.4, 69.8)]:
    place(key, x, y)
for y in range(2, 94, 4):
    x = coast[y] + RNG.randint(1, 3)
    place('rock.medium_' + str(RNG.randint(1, 6)), x, y)
    place('coast.dune_grass', x + 1.4, y + 1.3, scale=RNG.uniform(.65, 1.05))

# Soft undergrowth stays cosmetic and does not alter walkability or line of sight.
shrubs = [k for k in objects if '.shrub.' in k]
groundcover = ['vineyard.grass_a', 'vineyard.grass_b', 'vineyard.lavender', 'vineyard.daisies', 'vineyard.poppies']
for _ in range(440):
    x, y = RNG.uniform(17, 64), RNG.uniform(1, 94)
    c = int(x), int(y)
    if surfaces[c[1]][c[0]] == 'L' and c not in reserved:
        place(RNG.choice(shrubs if RNG.random() < .52 else groundcover), x, y, scale=RNG.uniform(.65, 1.1))
for _ in range(70):
    x, y = RNG.uniform(18, 63), RNG.uniform(3, 93)
    if (int(x), int(y)) not in reserved:
        place('rock.small_' + str(RNG.randint(1, 15)), x, y)

# The eastern specimen strip contains every material and all 185 object variants once.
# Shelf spacing uses actual sprite sizes, so crowns and tall ruins remain individually readable.
for i, material in enumerate(read(PACK / 'materials.json')):
    cx, cy = 74 + (i % 3) * 8, 5 + (i // 3) * 7
    for oy in [-1.6, 0, 1.6]:
        for ox in [-1.6, 0, 1.6]:
            stamp(material['id'].split('.')[-1], cx + ox, cy + oy, 2.15, 1, 1)
gallery = []
cursor_x, cursor_y, row_height = 70.0, 17.0, 0.0
for category in ['trees', 'shrubs', 'coast', 'rocks', 'ruins', 'vineyards']:
    if cursor_x > 70:
        cursor_y += row_height + .5
        cursor_x, row_height = 70.0, 0.0
    for d in read(PACK / (category + '.json')):
        w, h = d['footprint']
        sw, sh = d['sprite']['size']
        ax, ay = d['sprite']['anchor']
        slot_width = max(w + .65, sw / 64 + .65, 2.0)
        above = max(h - .5, sh / 64 * ay)
        below = max(.5, sh / 64 * (1 - ay))
        slot_height = above + below + .65
        if cursor_x + slot_width > 95.5:
            cursor_y += row_height
            cursor_x, row_height = 70.0, 0.0
        anchor_x = cursor_x + slot_width / 2
        anchor_y = cursor_y + above
        if d['gameplay']:
            x, y = round(anchor_x - w / 2), math.ceil(anchor_y - h + .5)
        else:
            x, y = anchor_x, anchor_y
        assert y + h < SIZE, ('Gallery overflow', category, d['id'], y)
        assert place(d['id'], x, y, force=True), ('Gallery placement', d['id'], x, y)
        gallery.append({'asset': d['id'], 'position': [x, y]})
        cursor_x += slot_width
        row_height = max(row_height, slot_height + .4)

# A few controllable units support movement and scale comparisons without a hostile camp.
scenario['units'] = [{'asset': 'human.peacemaker', 'owner': 0, 'cell': [37, 82]},
                     {'asset': 'human.peacemaker', 'owner': 0, 'cell': [38, 82]},
                     {'asset': 'human.flying_soldier', 'owner': 0, 'cell': [36, 82]}]
for x, y in [(30, 84), (30, 85), (31, 85)]:
    assert (x, y) not in occupied
    scenario['resources'].append({'asset': 'crystal.small', 'cell': [x, y], 'remaining': 1000})
scenario['terrain'] = {'base': 'sunny_hills.meadow',
                       'heights': [''.join('-' if h == -1 else str(h) for h in row) for row in heights],
                       'surfaces': [''.join(row) for row in surfaces], 'blocked': [''.join(row) for row in blocked],
                       'ramps': [[x, y, *d] for (x, y), d in ramps.items()]}
write(ROOT / 'assets/maps/sunny-hills.rtsmap', scenario)
review = ROOT / 'out/sunny-hills-review'
review.mkdir(parents=True, exist_ok=True)
write(review / 'gallery.json', gallery)
print('Sunny Hills:', len(scenario['environment']), 'solid objects,', len(scenario['decorations']), 'cosmetics,', len(scenario['paint']), 'paint stamps; gallery ends at row', round(cursor_y + row_height, 1))
