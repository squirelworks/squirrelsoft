# S6 CI stubs

Suggested `.github/workflows/ci.yml` bodies for each future per-app repository.
Copy into the new repo after `git filter-repo`; do not enable them in the
umbrella. Path layout assumes filter-repo kept the original directory names
(`squirrelsuica/`, `libs/sqfelica/`, …) at the new repo root.

Debug builds only. No AABs, no signing secrets, no real card dumps.
