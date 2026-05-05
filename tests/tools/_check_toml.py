import sys
try:
    import tomllib
except ImportError:
    import tomli as tomllib
for path in sys.argv[1:]:
    with open(path, 'rb') as f:
        d = tomllib.load(f)
    extras = (d.get('functions') or {}).get('extra') or []
    jt     = d.get('jump_table') or []
    print(f'{path}: extras={len(extras)} jump_tables={len(jt)} (parsed OK)')
