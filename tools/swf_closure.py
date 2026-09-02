# -*- coding: utf-8 -*-
"""Dependency closure of one SWF symbol, from a `ffdec-cli -dumpSWF` dump.

Reads the tag dump, follows PlaceObject references out of DefineSprite bodies, and
prints the character ids that are NOT reachable from the root symbol -- i.e. the ones
safe to delete. Only DefineXxx tags are listed; ImportAssets stubs can't be removed with
-removeCharacter and are handled separately by strip_icon_swf.ps1.

Usage:  python swf_closure.py <tags.txt> <rootCharacterId>
Prints: space-separated ids to remove (stdout), a short summary (stderr).
"""
import re
import sys

line_re = re.compile(r'^([0-9a-fA-F]+):(\s+)(\d+)\.\s+(\S+)(.*)$')
chid_re = re.compile(r'\(chid:\s*(\d+)')


def main(tags_path, root):
    recs = []
    with open(tags_path, encoding='utf-8', errors='ignore') as f:
        for ln in f:
            m = line_re.match(ln.rstrip('\n'))
            if m:
                cm = chid_re.search(m.group(5))
                recs.append((len(m.group(2)), m.group(4),
                             int(cm.group(1)) if cm else None))

    defs, edges, stack_sprites = {}, {}, []
    for indent, tag, chid in recs:
        while stack_sprites and indent <= stack_sprites[-1][0]:
            stack_sprites.pop()
        if (tag.startswith('Define') or tag.startswith('ImportAssets')) and chid is not None:
            defs[chid] = tag
            if tag.startswith('DefineSprite'):
                edges.setdefault(chid, set())
                stack_sprites.append((indent, chid))
        elif tag.startswith('PlaceObject') and chid is not None and stack_sprites:
            edges[stack_sprites[-1][1]].add(chid)

    keep, stack = set(), [root]
    while stack:
        c = stack.pop()
        if c in keep:
            continue
        keep.add(c)
        stack.extend(n for n in edges.get(c, ()) if n not in keep)

    remove = sorted(c for c in (set(defs) - keep) if defs[c].startswith('Define'))
    kinds = {}
    for c in keep:
        kinds[defs.get(c, '?')] = kinds.get(defs.get(c, '?'), 0) + 1
    sys.stderr.write("closure of %d: %d chars %s | removing %d\n"
                     % (root, len(keep), kinds, len(remove)))
    sys.stdout.write(" ".join(str(c) for c in remove))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], int(sys.argv[2])))
