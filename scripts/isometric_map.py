"""Author a rectangular isometric map from a screen-space composition.

This is an authoring helper, not a runtime file-format migration. The exported
map stores ordinary logical cells, footprints and ramps, editable in Forge.
"""
import math


def rectangular_composition(scenario, objects, reserved=()):
    width, height = scenario['size']
    assert width % 32 == height % 32 == 0
    side = width // 2 + height
    old = scenario['terrain']

    def project(p):
        x, y = p
        return [x / 2 + y, y - x / 2 + width / 2]

    def source(p):
        u, v = p
        return [u - v + width / 2, (u + v - width / 2) / 2]

    def complete(x, y):
        return (0 <= x < side and 0 <= y < side and
                -width / 2 + 1 <= x - y <= width / 2 - 1 and
                width / 2 <= x + y <= width / 2 + height * 2 - 2)

    def sample(rows, x, y):
        ox, oy = source((x + .5, y + .5))
        return rows[min(height - 1, max(0, math.floor(oy)))][min(width - 1, max(0, math.floor(ox)))]

    levels = [[sample(old['heights'], x, y) for x in range(side)] for y in range(side)]
    surfaces = [[sample(old['surfaces'], x, y) for x in range(side)] for y in range(side)]
    blocked = [[sample(old['blocked'], x, y) for x in range(side)] for y in range(side)]
    ramps = {}
    # Re-author each connected ramp strip on the new logical grid. A rotated
    # staircase of single cells would sever paths and create illegal side entry.
    seen = set()
    old_ramps = {(x, y): (dx, dy) for x, y, dx, dy in old['ramps']}
    for (x, y), (dx, dy) in old_ramps.items():
        if (x, y) in seen:
            continue
        group = [(x, y)]
        for sign in [-1, 1]:
            step = 1
            while old_ramps.get((x - dy * step * sign, y + dx * step * sign)) == (dx, dy):
                group.append((x - dy * step * sign, y + dx * step * sign))
                step += 1
        seen.update(group)
        cx = sum(p[0] + .5 for p in group) / len(group)
        cy = sum(p[1] + .5 for p in group) / len(group)
        u, v = map(math.floor, project((cx, cy)))
        low = -1 if old['heights'][y][x] == '-' else int(old['heights'][y][x])
        for lane in [-1, 0, 1]:
            a, b = u - dy * lane, v + dx * lane
            for step in range(-2, 3):
                px, py = a + dx * step, b + dy * step
                h = low + (step > 0)
                levels[py][px] = '-' if h == -1 else str(h)
                surfaces[py][px] = 'L'
                blocked[py][px] = '0'
            ramps[a, b] = (dx, dy)

    occupied = set()
    reserve = set(reserved)

    def candidates(target, distance=12):
        x, y = map(round, target)
        return sorted(((x + dx, y + dy) for dy in range(-distance, distance + 1)
                       for dx in range(-distance, distance + 1)),
                      key=lambda p: ((p[0] - target[0]) ** 2 + (p[1] - target[1]) ** 2, p))

    def place(old_cell, w, h, road_guard=False):
        # Preserve the screen-space foot anchor of each prop.
        anchor = project((old_cell[0] + w / 2, old_cell[1] + h - .5))
        target = (anchor[0] - w + .5, anchor[1] - h + .5)
        for x, y in candidates(target):
            cells = [(x + dx, y + dy) for dy in range(h) for dx in range(w)]
            if any(not complete(a, b) or (a, b) in occupied or (a, b) in ramps or
                   surfaces[b][a] != 'L' or levels[b][a] != levels[y][x] or blocked[b][a] != '0'
                   for a, b in cells):
                continue
            if road_guard and any(tuple(map(math.floor, source((a + .5, b + .5)))) in reserve for a, b in cells):
                continue
            occupied.update(cells)
            return [x, y]
        raise ValueError(('No flat placement', old_cell, w, h))

    scenario['start']['hall'] = place(scenario['start']['hall'], 3, 2)
    # Keep starting points free before arranging props nearby.
    spawns = [scenario['start']['workers'], [scenario['start']['hero']]]
    for group in spawns:
        for p in group:
            if p is None:
                continue
            target = project((p[0] + .5, p[1] + .5))
            found = next((a, b) for a, b in candidates((target[0] - .5, target[1] - .5))
                         if complete(a, b) and (a, b) not in occupied and (a, b) not in ramps
                         and surfaces[b][a] != 'D')
            p[:] = found
            occupied.add(found)
    for obj in scenario['environment']:
        w, h = objects[obj['asset']]['footprint']
        obj['cell'] = place(obj['cell'], w, h, road_guard=True)
    for obj in scenario['resources']:
        obj['cell'] = place(obj['cell'], 1, 1)
    for obj in scenario['units']:
        x, y = obj['cell']
        desired_surface = old['surfaces'][y][x]
        target = project((x + .5, y + .5))
        options = [(a, b) for a, b in candidates((target[0] - .5, target[1] - .5))
                   if complete(a, b) and (a, b) not in occupied and (a, b) not in ramps
                   and surfaces[b][a] == desired_surface]
        # Put wading examples far enough from the dry bank to show knee-depth water.
        full_depth = [(a, b) for a, b in options if desired_surface == 'S' and
                      all(0 <= a+dx < side and 0 <= b+dy < side and surfaces[b+dy][a+dx] != 'L'
                          for dy in [-1, 0, 1] for dx in [-1, 0, 1])]
        found = (full_depth or options)[0]
        obj['cell'] = list(found)
        occupied.add(found)
    for obj in scenario['decorations'] + scenario['paint']:
        obj['position'] = [round(v, 3) for v in project(obj['position'])]
    scenario['size'] = [side, side]
    scenario['layoutSize'] = [width, height]
    scenario['terrain'] = {'base': old['base'], 'heights': [''.join(row) for row in levels],
                           'surfaces': [''.join(row) for row in surfaces],
                           'blocked': [''.join(row) for row in blocked],
                           'ramps': [[x, y, *d] for (x, y), d in ramps.items()]}
    return scenario
