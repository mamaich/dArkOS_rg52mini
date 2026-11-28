#!/usr/bin/env python3
"""Which of RetroArch's slang presets a new set of shaders changes.

    slangdeps.py OLD NEW [-o list.txt]

OLD and NEW are two copies of libretro's slang-shaders (the image's
shaders_slang folder, from the previous release's image and the new one).
A preset is what RetroArch reads when it loads it: the .slangp itself, the
presets it names with #reference and #include, the passes' .slang files with
every #include in them, and its textures (LUTs). Each preset of NEW whose
files are not all the same in OLD, or that OLD lacks, is listed (paths
relative to NEW, one per line), for slangcheck.py to load on the console
with both sets. Presets of OLD that NEW lacks are counted on stderr: a
user's setting that names one loses it.

The owner, 2026-11-28: the image takes upstream's newest shaders, checked
before each release (docs/OTA.md, "Release procedure").
"""
import argparse
import hashlib
import os
import re
import sys

REF = re.compile(r'^\s*#\s*(reference|include)\s+"([^"]+)"')
INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
# key = "value" or key = value (RetroArch's config files end an unquoted value at a space)
KV = re.compile(r'^\s*([A-Za-z0-9_]+)\s*=\s*(?:"([^"]*)"|(\S*))')


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def lines(data):
    return data.decode("utf-8", "replace").splitlines()


class Tree:
    def __init__(self, root):
        self.root = os.path.realpath(root)
        self.sha = {}

    def key(self, path):
        """A file's name in the closure: relative to the root, or marked as
        outside it (a preset may name ../ past the folder)."""
        r = os.path.relpath(os.path.realpath(path), self.root)
        return r if not r.startswith("..") else "<outside>/" + os.path.abspath(path)

    def digest(self, path):
        p = os.path.realpath(path)
        if p not in self.sha:
            d = read(p)
            self.sha[p] = hashlib.sha256(d).hexdigest() if d is not None else "missing"
        return self.sha[p]

    def shader(self, path, files, seen):
        p = os.path.normpath(path)
        if p in seen:
            return
        seen.add(p)
        files[self.key(p)] = self.digest(p)
        d = read(p)
        if d is None:
            return
        for ln in lines(d):
            m = INC.match(ln)
            if m:
                self.shader(os.path.join(os.path.dirname(p), m.group(1)), files, seen)

    def preset(self, path, files, seen):
        p = os.path.normpath(path)
        if p in seen:
            return
        seen.add(p)
        files[self.key(p)] = self.digest(p)
        d = read(p)
        if d is None:
            return
        base = os.path.dirname(p)
        kv = {}
        for ln in lines(d):
            m = REF.match(ln)
            if m:
                self.preset(os.path.join(base, m.group(2)), files, seen)
                continue
            if ln.lstrip().startswith(("#", "//")):
                continue
            m = KV.match(ln)
            if m:
                kv[m.group(1)] = (m.group(2) if m.group(2) is not None else m.group(3)).strip()
        for k, v in kv.items():
            if re.fullmatch(r"shader\d+", k) and v:
                self.shader(os.path.join(base, v), files, seen)
        for t in (x.strip() for x in kv.get("textures", "").split(";")):
            if t and kv.get(t):
                files[self.key(os.path.join(base, kv[t]))] = self.digest(os.path.join(base, kv[t]))

    def closure(self, rel):
        files = {}
        self.preset(os.path.join(self.root, rel), files, set())
        h = hashlib.sha256()
        for k in sorted(files):
            h.update(("%s %s\n" % (k, files[k])).encode())
        return h.hexdigest()

    def presets(self):
        out = []
        for d, dirs, names in os.walk(self.root):
            dirs.sort()
            for n in sorted(names):
                if n.endswith(".slangp"):
                    out.append(os.path.relpath(os.path.join(d, n), self.root))
        return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("-o", "--out", help="write the list here (default: stdout)")
    a = ap.parse_args()
    old, new = Tree(a.old), Tree(a.new)
    po, pn = set(old.presets()), new.presets()
    changed, added = [], []
    for rel in pn:
        if rel not in po:
            added.append(rel)
        elif old.closure(rel) != new.closure(rel):
            changed.append(rel)
    gone = sorted(po - set(pn))
    out = open(a.out, "w") if a.out else sys.stdout
    for rel in changed + added:
        print(rel, file=out)
    if a.out:
        out.close()
    print("presets: %d in the new set, %d in the old; %d changed, %d new, %d gone%s"
          % (len(pn), len(po), len(changed), len(added), len(gone),
             (" (" + ", ".join(gone[:5]) + (", ..." if len(gone) > 5 else "") + ")") if gone else ""),
          file=sys.stderr)


if __name__ == "__main__":
    main()
