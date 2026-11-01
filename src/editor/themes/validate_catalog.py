"""Validate bundled theme data without Qt or network access."""
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parent
catalog = json.loads((root / "catalog.json").read_text(encoding="utf-8"))
roles = ("background", "foreground", "comment", "accent", "keyword", "string",
         "number", "function", "type", "error")
ids = set()
names = set()
resources = {node.attrib.get("alias", node.text): node.text
             for node in ET.parse(root / "themes.qrc").iter("file")}


def luminance(color):
    rgb = [int(color[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    linear = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
              for c in rgb]
    return sum(c * weight for c, weight in zip(linear, (0.2126, 0.7152, 0.0722)))


for theme in catalog:
    assert theme["id"] not in ids, f"duplicate ID: {theme['id']}"
    assert theme["name"] not in names, f"duplicate name: {theme['name']}"
    ids.add(theme["id"])
    names.add(theme["name"])
    assert re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", theme["id"])
    assert type(theme["dark"]) is bool
    for role in roles:
        assert re.fullmatch(r"#[0-9a-fA-F]{6}", theme[role]), (theme["id"], role)
    low, high = sorted(map(luminance, (theme["foreground"], theme["background"])))
    contrast = (high + 0.05) / (low + 0.05)
    assert contrast >= 4.5, f"body-text contrast {contrast:.2f}: {theme['id']}"
    if theme["source"] != "8bitpad":
        license_path = f"licenses/{theme['source']}.txt"
        text = (root / license_path).read_text(encoding="utf-8")
        assert "Permission is hereby granted" in text, license_path
        assert "Copyright" in text, license_path
        assert license_path in resources, f"notice not embedded: {license_path}"

assert len(catalog) == 25
assert "ayu-mirage" in ids
assert sum(theme["dark"] for theme in catalog) == 18
for alias, path in resources.items():
    assert (root / path).is_file(), f"missing resource: {alias}"
print(f"PASS: {len(catalog)} unique themes, all body-text contrast >= 4.5:1, "
      "all upstream license notices embedded, all resource paths valid.")
