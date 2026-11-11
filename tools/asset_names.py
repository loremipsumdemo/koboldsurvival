"""Which file each numbered picture is written to: the id in src/game_tables.cpp (EMOTE_KEYS, ICON_KEYS) gives a key, and
the table in data/ (emotes.txt, icons.txt) gives that key's file. The art scripts use this so a rename is made in the
table alone."""
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def numbered_files(table, keys_const):
    """{id: file name} for data/<table>, with the ids of src/game_tables.cpp's <keys_const>."""
    src = open(os.path.join(REPO, 'src', 'game_tables.cpp'), encoding='utf-8').read()
    block = re.search(re.escape(keys_const) + r' = \{(.*?)\n\};', src, re.S).group(1)
    ids = {key: int(n) for key, n in re.findall(r'\{\s*"(\w+)",\s*(\d+)\s*\}', block)}
    files = {}
    for line in open(os.path.join(REPO, 'data', table), encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        cols = line.rstrip('\r\n').split('\t')
        files[ids[cols[0]]] = cols[1]
    return files
