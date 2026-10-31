import sys
rc = 0
for p in sys.argv[sys.argv.index('--') + 1:]:
    if 'BAD' in open(p, encoding='utf-8', errors='replace').read():
        print(p + ': syntax error', file=sys.stderr); rc = 1
sys.exit(rc)
