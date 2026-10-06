#!/usr/bin/env python3
"""
Tjek Lune Touch-siden efter flytningen til Hjem / ark / System (DESIGN.md 15).
Efter lune-design-system/examples/touch/check_fields.py (LDS 2.3.0).

    python web/touch-ui/check_fields.py [web/touch-ui/dist/da/index.html …]

1. Hvert felt og hver handling i conf_fields.txt (produktets gamle Dashboard/
   Konfiguration) findes i den nye markup. Gem-nøgler sammenlignes pr. ressource:
   "heat-source/x" er opfyldt af et felt x i form[data-save^="heat_source"]
   (fx heat_source.connection eller heat_source.behavior). "*" = ét pr. rum.
2. Ingen grupperet liste har mere end 6 rækker; ingen fane, System-kategori eller
   underside viser mere end 5 grupper ad gangen (typeafhængige felter tælles som
   det største af alternativerne, ikke summen).
3. Tekstfelter i grupperede lister har en bredde fra 15.6 (.w-*).
4. Gamle visninger og tilstande (v-dash-*, v-conf-*, m-dash, m-conf) og den
   hierarkiske strimmel (strip--tiers, substrip, tile-manifold) er væk.

Produktforskel: rum- og styringsark er skabeloner (<template id="tpl-room"/"tpl-mani">),
som binderen kloner pr. rum/styring. HTMLParser læser skabelonens indhold som almindelige
noder, så rum-felterne (room:r__N__:include …) og deres grupper tjekkes i skabelonen.
Rækker, som binderen tegner ved runtime (styringstabellen, fundne
styringer, skemarækker), findes ikke i den statiske side. Deres data-action-navne
tælles derfor også med fra binder.js.
"""
import pathlib, re, sys
from html.parser import HTMLParser

ROOT = pathlib.Path(__file__).resolve().parent
MAX_ROWS, MAX_GROUPS = 6, 5
VOID = {"input", "br", "img", "meta", "link", "hr", "source"}


class Node:
    __slots__ = ("tag", "attrs", "cls", "kids", "parent")

    def __init__(self, tag, attrs, parent):
        self.tag, self.attrs, self.parent = tag, attrs, parent
        self.cls = set((attrs.get("class") or "").split())
        self.kids = []


class Tree(HTMLParser):
    def __init__(self):
        super().__init__()
        self.root = Node("root", {}, None)
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        n = Node(tag, dict(attrs), self.cur)
        self.cur.kids.append(n)
        if tag not in VOID:
            self.cur = n

    def handle_startendtag(self, tag, attrs):
        self.cur.kids.append(Node(tag, dict(attrs), self.cur))

    def handle_endtag(self, tag):
        n = self.cur
        while n is not self.root and n.tag != tag:
            n = n.parent
        if n is not self.root:
            self.cur = n.parent


def walk(n):
    yield n
    for k in n.kids:
        yield from walk(k)


def label(n):
    """Arkets id eller System-kategorien; fanen tilføjes, når den findes."""
    tab, p = None, n
    while p is not None:
        a = p.attrs
        if a.get("data-tab") and not tab:
            tab = a["data-tab"]
        if a.get("data-cat"):
            return a["data-cat"]
        if (a.get("id") or "").startswith(("sheet-", "v-")):
            return a["id"] + (f" › {tab}" if tab else "")
        p = p.parent
    return tab or "?"


def rows_in(lst):
    """Rækker i en .setting-list: direkte .setting/details, også inde i .gated-body."""
    c = 0
    for k in lst.kids:
        if "setting" in k.cls or k.tag == "details":
            c += 1
        elif "gated-body" in k.cls:
            c += rows_in(k)
    return c


def visible_groups(n, host=True):
    """Grupper der vises samtidig i n: direkte grupper + største typevalg; stop ved undersider."""
    if not host and ("subpage-body" in n.cls or n.cls & {"tab-panel", "sys-cat"}):
        return 0
    direct, alts = 0, []
    for k in n.kids:
        if "setting-group" in k.cls:
            direct += 1
        elif "typed-fields" in k.cls:
            alts.append(visible_groups(k, host=False))
        elif "subpage-body" in k.cls:
            continue
        else:
            direct += visible_groups(k, host=False)
    return direct + (max(alts) if alts else 0)


def main():
    pages = [pathlib.Path(a) for a in sys.argv[1:]] or [ROOT / "dist" / l / "index.html" for l in ("da", "en")]
    bad = 0
    for page in pages:
        bad += check(page)
    sys.exit(1 if bad else 0)


def check(page):
    html = page.read_text(encoding="utf-8")
    want = [l.strip() for l in (ROOT / "conf_fields.txt").read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith("#")]
    tree = Tree()
    tree.feed(html)
    nodes = list(walk(tree.root))
    errors = []

    norm = lambda key: key.split(".")[0].replace("-", "_")
    have, actions, data_actions = set(), set(), set()
    binder = (ROOT / "binder.js").read_text(encoding="utf-8")
    data_actions.update(re.findall(r'data-action="([a-z-]+)"', binder))
    for n in nodes:
        if n.attrs.get("data-action"):
            data_actions.add(n.attrs["data-action"])
        if n.tag in ("input", "select", "textarea", "button") and n.attrs.get("name"):
            f = n.parent
            while f is not None and not (f.tag == "form" and f.attrs.get("data-save")):
                f = f.parent
            res = norm(f.attrs["data-save"]) if f else "-"
            if n.attrs["name"] == "action":
                actions.add(f"{res}/{n.attrs.get('value')}")
            else:
                have.add(f"{res}/{n.attrs['name']}")
    for w in want:
        if w.startswith("data-action:"):
            ok = w.split(":", 1)[1] in data_actions
        else:
            res, _, name = w.partition("/")
            res = norm(res)
            if name.startswith("action:"):
                ok = f"{res}/{name[7:]}" in actions
            elif "*" in name:
                rx = re.compile("^" + re.escape(f"{res}/{name}").replace(r"\*", "[^:]+") + "$")
                ok = any(rx.match(h) for h in have)
            else:
                ok = f"{res}/{name}" in have
        if not ok:
            errors.append(f"mangler: {w}")

    for n in nodes:
        if "setting-list" in n.cls and rows_in(n) > MAX_ROWS:
            errors.append(f"gruppe med {rows_in(n)} rækker (maks. {MAX_ROWS}) i {label(n)}")
        if n.cls & {"tab-panel", "sys-cat", "subpage-body"}:
            g = visible_groups(n)
            if g > MAX_GROUPS:
                errors.append(f"{label(n)} viser {g} grupper (maks. {MAX_GROUPS})")
        if n.tag == "input" and "input" in n.cls and not any(c.startswith("w-") for c in n.cls):
            p = n.parent
            while p is not None and "setting-control" not in p.cls:
                p = p.parent
            if p is not None:
                errors.append(f'tekstfelt uden .w-* i grupperet liste: id="{n.attrs.get("id")}"')

    for old in ('id="v-dash-', 'id="v-conf-', 'id="m-dash"', 'id="m-conf"', 'strip--tiers', 'class="substrip"', 'tile-manifold'):
        if old in html:
            errors.append(f"gammel markup findes stadig: {old}")

    for e in errors:
        print("FEJL ", e)
    print(f"{page}: {len(want)} felter/handlinger tjekket, {len(errors)} fejl")
    return len(errors)


if __name__ == "__main__":
    main()
