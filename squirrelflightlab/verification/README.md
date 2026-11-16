# Flight Lab verification (Soft #172)

Analytic cases run via:

```bash
make -C squirrelflightlab verify
# or
./build/squirrelflightlab verify
```

Optional cross-check (skipped when files are absent):

```bash
./build/squirrelflightlab verify --avl path/to/avl-out.txt --openvsp path/to/vsp.txt
```

All reports are labelled **provisional, unvalidated against tunnel/flight test**.
See `docs/credibility.md`. NESC CSV runners and F-16 TP-1538 benchmark are
follow-ups (licence/notice check before vendoring).
