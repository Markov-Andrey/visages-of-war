"""Build the compact Sunny Hills village and asset bench (standard-library Python)."""
import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / 'assets/world/tilesets/sunny-hills'
WIDTH, HEIGHT = 96, 64
RNG = random.Random(7102026)
CATEGORIES = ['trees', 'shrubs', 'coast', 'rocks', 'ruins', 'vineyards']


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


catalogs = {category: read(PACK / (category + '.json')) for category in CATEGORIES}
objects = {d['id']: d for definitions in catalogs.values() for d in definitions}
INK_PACK = ROOT / 'assets/world/tilesets/sunny-hills-ink'
ink_objects = read(INK_PACK / 'objects.json')
objects.update({d['id']: d for d in ink_objects})

def village_asset(key):
    if key.startswith('sunny_hills.tree.'):
        species, variant = key.removeprefix('sunny_hills.tree.').rsplit('_', 1)
        species = {'fig': 'lemon', 'orange': 'lemon', 'pomegranate': 'olive', 'oak': 'stone_pine'}.get(species, species)
        return 'sunny_hills_ink.tree.' + species + '_' + variant
    if key in ['sunny_hills.vineyard.winery', 'sunny_hills.vineyard.cellar']:
        return 'sunny_hills_ink.winery'
    if key in ['sunny_hills.vineyard.vine_a', 'sunny_hills.vineyard.vine_b']:
        return 'sunny_hills_ink.grape_trellis'
    return key
heights = [[0] * WIDTH for _ in range(HEIGHT)]
surfaces = [['L'] * WIDTH for _ in range(HEIGHT)]
ramps, occupied, reserved = {}, set(), set()
scenario = {'format': 'rts-world', 'name': 'Sunny Hills - Vineyard Village & Asset Bench',
            'size': [WIDTH, HEIGHT],
            'start': {'hall': [16, 28], 'workers': [[18, 33], [19, 33], [20, 33]],
                      'hero': [21, 34], 'crystals': 1500},
            'terrain': {}, 'paint': [], 'environment': [], 'decorations': [], 'resources': [], 'units': []}
next_id = 1000


def stamp(material, x, y, radius, opacity=1, hardness=.5):
    if not material.startswith('sunny_hills'):
        material = ('sunny_hills_ink.' if x < 40 and material != 'ancient_paving' else 'sunny_hills.') + material
    scenario['paint'].append({'material': material,
                              'position': [round(x, 3), round(y, 3)], 'radius': round(radius, 3),
                              'opacity': opacity, 'hardness': hardness, 'erase': False})


def road(points, material='dirt', radius=1.05, reserve=1.7):
    for a, b in zip(points, points[1:]):
        count = max(1, math.ceil(math.dist(a, b) / .65))
        for step in range(count + 1):
            t = step / count
            x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            stamp(material, x, y, radius, .86, .64)
            for cy in range(max(0, int(y - reserve)), min(HEIGHT, math.ceil(y + reserve))):
                for cx in range(max(0, int(x - reserve)), min(WIDTH, math.ceil(x + reserve))):
                    if math.hypot(cx + .5 - x, cy + .5 - y) < reserve:
                        reserved.add((cx, cy))


def place(key, x, y, *, scale=1, force=False):
    global next_id
    key = key if key.startswith('sunny_hills') else 'sunny_hills.' + key
    if x < 40:
        key = village_asset(key)
    d = objects[key]
    if not (0 <= x < WIDTH and 0 <= y < HEIGHT):
        return False
    if not d['gameplay']:
        if surfaces[int(y)][int(x)] != 'L':
            return False
        scenario['decorations'].append({'id': next_id, 'asset': key,
                                       'position': [round(x, 3), round(y, 3)],
                                       'scale': round(scale, 3), 'rotation': 0})
    else:
        x, y = int(x), int(y)
        w, h = d['footprint']
        cells = [(x + dx, y + dy) for dy in range(h) for dx in range(w)]
        if any(cx >= WIDTH or cy >= HEIGHT or surfaces[cy][cx] != 'L' or
               heights[cy][cx] != heights[y][x] or (cx, cy) in occupied or
               (cx, cy) in ramps or (not force and (cx, cy) in reserved) for cx, cy in cells):
            return False
        scenario['environment'].append({'id': next_id, 'asset': key, 'cell': [x, y], 'health': d['health']})
        occupied.update(c for c, collision in zip(cells, d['collision']) if collision)
    next_id += 1
    return True


def required(key, x, y):
    assert place(key, x, y, force=True), ('Authored object overlaps', key, x, y)


def ramp_lane(x, y, dx, dy):
    for offset in [-1, 0, 1]:
        cx, cy = x - dy * offset, y + dx * offset
        assert surfaces[cy][cx] == surfaces[cy + dy][cx + dx] == 'L'
        assert heights[cy + dy][cx + dx] == heights[cy][cx] + 1
        ramps[cx, cy] = [dx, dy]
        for step in [-1, 0, 1]:
            reserved.add((cx + dx * step, cy + dy * step))


# A small western cove frames the village; the shore has one broad dry descent.
coast = []
for y in range(HEIGHT):
    edge = 6 if 27 <= y <= 37 else round(5 + 1.3 * math.sin(y / 6))
    coast.append(edge)
    for x in range(edge):
        surfaces[y][x] = 'D' if x < edge - 2 else 'S'
        heights[y][x] = -1
    if 27 <= y <= 37:
        heights[y][6] = heights[y][7] = -1
    if y % 2 == 0:
        stamp('sand', edge + .65, y + .5, 1.7, .65, .45)
ramp_lane(7, 32, 1, 0)
# Authored sand remains editable beneath the procedural water column.
for y in range(0, HEIGHT, 2):
    for x in range(0, coast[y] + 1, 2):
        stamp('sand', x + .5, y + .5, 1.65, 1, .85)

# Low hills and a planted boundary enclose the designed western half.
for y in range(HEIGHT):
    for x in range(7, 38):
        if y < 9 + int(2 * math.sin(x / 4)) or y > 54 + int(2 * math.sin(x / 5)):
            heights[y][x] = 1
for x in range(17, 20):
    heights[9][x] = 1
    heights[10][x] = heights[11][x] = 0
ramp_lane(18, 10, 0, -1)

# The water bench has a dry bank, wadeable shallows and a deep-water pocket.
for y in range(3, 11):
    for x in range(65, 77):
        heights[y][x] = -1
        surfaces[y][x] = 'L' if x == 65 else ('S' if x < 70 else 'D')
ramp_lane(65, 7, -1, 0)
for y in range(3, 11, 2):
    for x in range(65, 77, 2):
        stamp('sand', x + .5, y + .5, 1.65, 1, .85)
for y in [3.5, 5.5, 7.5, 9.5]:
    stamp('sand', 64.8, y, .8, 1, .8)

# Subtle warm meadow variation leaves the village readable at normal zoom.
for x, y, radius in [(12, 22, 3), (28, 27, 3), (13, 40, 4), (24, 47, 4), (33, 17, 3), (9, 13, 3)]:
    stamp('dry_grass', x, y, radius, .28, .2)
for x, y in [(17.5, 29), (26.5, 27), (12.5, 29), (13.5, 36), (24.5, 40)]:
    stamp('dirt', x, y, 2.1, .7, .5)
stamp('cobblestone', 19.1, 30.7, 2.6, .92, .72)

# The main route is broad and unobstructed from the court to the bench spine.
road([(19, 32), (23, 32), (28, 29), (34, 28), (40, 28)], radius=1.05, reserve=2)
road([(19, 32), (14, 32), (10, 33), (7, 32)], radius=.95)
road([(20, 30), (21, 26), (19, 23), (17, 20), (17, 14), (18.5, 10.5)], radius=.8, reserve=1.1)
road([(21, 33), (22, 37), (23, 42), (29, 46), (34, 44)], radius=.9, reserve=1.4)
road([(14, 33), (15, 38), (17, 43), (23, 42)], radius=.75, reserve=1.2)
road([(40, 12), (40, 61)], radius=.8, reserve=1.3)
road([(40, 12), (62.5, 12), (63.5, 7)], radius=.7, reserve=1.1)
for y in range(27, 36):
    for x in range(15, 23):
        reserved.add((x, y))
occupied.update((x, y) for y in range(28, 30) for x in range(16, 19))

# Three small farmsteads form a courtyard instead of a scattered prop exhibition.
for key, x, y in [('winery', 25, 26), ('cellar', 11, 28), ('cellar', 23, 39), ('cellar', 12, 35),
                  ('well', 22, 28), ('barrels', 28, 27), ('barrel', 28, 26),
                  ('crate', 26, 28), ('cart', 11, 30), ('hay', 9, 29), ('barrels', 11, 36),
                  ('press', 26, 42), ('barrels', 26, 40), ('crate', 24, 42),
                  ('fence', 10, 26), ('fence', 12, 26), ('fence', 28, 44), ('fence', 31, 44)]:
    required('vineyard.' + key, x, y)

# Narrow bare strips under trellises make the vineyards read as cultivated plots.
for x in [20, 23, 26]:
    road([(x + 1, 15), (x + 1, 21.5)], radius=.62, reserve=0)
    for y in [15, 18, 21]:
        required('vineyard.vine_a' if x % 2 else 'vineyard.vine_b', x, y)
for x in [28, 31, 34]:
    road([(x + 1, 34), (x + 1, 41.5)], radius=.65, reserve=0)
    for y in [34, 37, 40]:
        required('vineyard.vine_b' if y % 2 else 'vineyard.vine_a', x, y)
for x, y in [(19, 15), (28, 21), (27, 34), (36, 40)]:
    required('vineyard.vine_post', x, y)

# Orchard rows, taller accents behind houses, and a few shade trees around the court.
for row, y in enumerate([39, 42, 45]):
    for column, x in enumerate([8, 11, 14]):
        species = ['olive', 'lemon', 'fig'][(row + column) % 3]
        place('tree.' + species + '_' + str(1 + (row * 2 + column) % 5), x, y)
for key, x, y in [('cypress_1', 10, 27), ('cypress_2', 14, 28), ('cypress_5', 24, 25),
                  ('cypress_2', 29, 26), ('olive_2', 9, 31), ('stone_pine_3', 30, 25),
                  ('olive_4', 21, 40), ('cypress_1', 27, 39), ('stone_pine_1', 17, 36),
                  ('fig_3', 25, 44), ('olive_5', 11, 44)]:
    place('tree.' + key, x, y)

# Grouped perimeter groves frame open meadows; the central road remains a clear gap.
for cx, cy, rx, ry in [(12, 13, 4, 4), (31, 11, 5, 5), (33, 51, 4, 4),
                      (12, 50, 5, 4), (35, 21, 2, 4), (36, 37, 1, 5)]:
    for y in range(cy - ry, cy + ry + 1, 2):
        for x in range(cx - rx, cx + rx + 1, 2):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 < 1 and RNG.random() < .88:
                place('tree.' + RNG.choice(['olive', 'stone_pine', 'cypress']) + '_' + str(RNG.randint(1, 5)), x, y)
for y in list(range(4, 25, 2)) + list(range(32, 61, 2)):
    place('tree.cypress_' + str(1 + y % 5), 37, y)
for cx, cy, rx, ry in [(15, 5, 7, 3), (29, 5, 6, 3), (14, 59, 6, 3), (29, 58, 6, 3)]:
    for y in range(cy - ry, min(HEIGHT - 1, cy + ry + 1), 2):
        for x in range(cx - rx, cx + rx + 1, 2):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 < 1:
                place('tree.' + RNG.choice(['olive', 'stone_pine']) + '_' + str(RNG.randint(1, 5)), x, y)
for x, y in [(9, 8), (13, 7), (28, 6), (32, 7), (10, 55), (17, 58), (30, 56), (34, 54)]:
    place('rock.medium_' + str(RNG.randint(1, 6)), x, y)

# A working cove, not a second monumental scene.
for key, x, y in [('coast.boat', 6, 28), ('coast.cargo', 6, 35),
                  ('coast.driftwood', 6.3, 37.4), ('coast.dune_grass', 8.6, 36.8)]:
    place(key, x, y)
for y in [16, 20, 24, 41, 45, 49]:
    place('rock.medium_' + str(1 + y % 6), coast[y] + 1, y)

# Small, editable planting groups tie bases to the ground, leaving routes unpainted by props.
flowers = ['vineyard.grass_a', 'vineyard.grass_b', 'vineyard.lavender', 'vineyard.daisies']
for cx, cy, count in [(12, 29, 18), (26, 27, 20), (13, 36, 15), (24, 40, 20), (12, 42, 22),
                      (24, 19, 18), (32, 38, 20), (12, 14, 18), (31, 12, 20),
                      (12, 50, 18), (32, 51, 18), (36, 20, 12)]:
    for _ in range(count):
        x, y = cx + RNG.uniform(-2.6, 2.6), cy + RNG.uniform(-1.6, 2.2)
        if (int(x), int(y)) not in reserved and (int(x), int(y)) not in occupied:
            place(RNG.choice(flowers), x, y, scale=RNG.uniform(.8, 1.2))
for x, y in [(10.6, 28.9), (14.3, 29.7), (12.2, 27.5), (24.7, 27.8), (27.8, 28.4),
             (12, 37.1), (14.7, 36.8), (23, 41.3), (25.8, 41.5), (27.8, 44.9)]:
    place('vineyard.shrub', x, y, scale=.9)

# Six clean material samples, adjacent to the independent shallow/deep-water test.
for i, material in enumerate(read(PACK / 'materials.json')):
    cx, cy = 44 + (i % 3) * 6, 4 + (i // 3) * 5
    for ox in [-1, 1]:
        for oy in [-1, 1]:
            stamp(material['id'].split('.')[-1], cx + ox, cy + oy, 1.45, 1, 1)

# Every object variant remains available once on the eastern half, at its native size.
# Slots account for art bounds as well as collision; the western spine stays clear.
gallery = []
cursor_x, cursor_y, row_height = 42.0, 14.0, 0.0
for category in CATEGORIES:
    if cursor_x > 42:
        cursor_y += row_height + .5
        cursor_x, row_height = 42.0, 0.0
    for d in catalogs[category]:
        w, h = d['footprint']
        sw, sh = d['sprite']['size']
        ax, ay = d['sprite']['anchor']
        slot_width = max(w + .6, sw / 64 + .6, 1.6)
        above = max(h - .5, sh / 64 * ay)
        below = max(.5, sh / 64 * (1 - ay))
        slot_height = above + below + .8
        if cursor_x + slot_width > 79:
            cursor_y += row_height
            cursor_x, row_height = 42.0, 0.0
        anchor_x, anchor_y = cursor_x + slot_width / 2, cursor_y + above
        x, y = (round(anchor_x - w / 2), math.ceil(anchor_y - h + .5)) if d['gameplay'] else (anchor_x, anchor_y)
        assert y + h < HEIGHT, ('Gallery overflow', d['id'], y)
        assert place(d['id'], x, y, force=True), ('Gallery overlap', d['id'], x, y)
        gallery.append({'asset': d['id'], 'position': [x, y]})
        cursor_x += slot_width
        row_height = max(row_height, slot_height)

# A separate eastern strip presents every Ink variant without crowding the old bench.
road([(40, 12), (80, 12), (80, 60)], material='sunny_hills_ink.dirt', radius=.7, reserve=1.1)
for i, material in enumerate(read(INK_PACK / 'materials.json')):
    cx, cy = 84 + (i % 2) * 7, 3 + (i // 2) * 5
    for ox in [-1, 1]:
        for oy in [-.7, .7]:
            stamp(material['id'], cx + ox, cy + oy, 1.4, 1, 1)
for i, d in enumerate(ink_objects[:20]):
    x, y = 82 + (i % 5) * 3, 20 + (i // 5) * 8
    required(d['id'], x, y)
    gallery.append({'asset': d['id'], 'position': [x, y]})
for key, x, y in [('winery', 84, 54), ('grape_trellis', 90, 55)]:
    required('sunny_hills_ink.' + key, x, y)
    gallery.append({'asset': 'sunny_hills_ink.' + key, 'position': [x, y]})

scenario['units'] = [{'asset': 'human.peacemaker', 'owner': 0, 'cell': [80, 33]},{'asset': 'human.peacemaker', 'owner': 0, 'cell': [20, 35]},
                     {'asset': 'human.peacemaker', 'owner': 0, 'cell': [5, 32]},
                     {'asset': 'human.peacemaker', 'owner': 0, 'cell': [68, 7]},
                     {'asset': 'human.flying_soldier', 'owner': 0, 'cell': [19, 35]}]
# A nearby sparring group, outside starting vision so the player chooses when to engage.
scenario['units'] += [{'asset': 'human.slinger', 'owner': 0, 'cell': [21, 35]},
                      {'asset': 'human.slinger', 'owner': 0, 'cell': [22, 36]},
                      {'asset': 'human.peacemaker', 'owner': 1, 'cell': [32, 31]},
                      {'asset': 'human.peacemaker', 'owner': 1, 'cell': [32, 32]},
                      {'asset': 'human.slinger', 'owner': 1, 'cell': [34, 30]},
                      {'asset': 'human.slinger', 'owner': 1, 'cell': [34, 31]}]
for x, y in [(17, 39), (18, 39), (18, 40)]:
    assert (x, y) not in occupied
    scenario['resources'].append({'asset': 'crystal.small', 'cell': [x, y], 'remaining': 1000})
for x, y in scenario['start']['workers'] + [scenario['start']['hero']] + [u['cell'] for u in scenario['units']]:
    assert (x, y) not in occupied and surfaces[y][x] != 'D' and (x, y) not in ramps
scenario['terrain'] = {'base': 'sunny_hills_ink.meadow',
                       'heights': [''.join('-' if h == -1 else str(h) for h in row) for row in heights],
                       'surfaces': [''.join(row) for row in surfaces], 'blocked': ['0' * WIDTH] * HEIGHT,
                       'ramps': [[x, y, *direction] for (x, y), direction in ramps.items()]}
import sys
sys.dont_write_bytecode = True
from isometric_map import rectangular_composition
rectangular_composition(scenario, objects, reserved)
write(ROOT / 'assets/maps/sunny-hills.rtsmap', scenario)
review = ROOT / 'out/sunny-hills-review'
review.mkdir(parents=True, exist_ok=True)
write(review / 'gallery.json', gallery)
print('Sunny Hills', WIDTH, 'x', HEIGHT, ':', len(scenario['environment']), 'solid objects,',
      len(scenario['decorations']), 'cosmetics,', len(scenario['paint']), 'paint stamps;',
      len(gallery), 'gallery objects, last row', round(cursor_y + row_height, 1))
