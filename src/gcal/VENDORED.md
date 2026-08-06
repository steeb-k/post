# Vendored GNOME Calendar

This directory contains a vendored copy of GNOME Calendar's `src/` tree,
built into Post as a static library (`libgcal`) to provide the calendar
view, event editor, and calendar management.

- Upstream: https://gitlab.gnome.org/GNOME/gnome-calendar
- Vendored commit: `a7f7733eb1f84aceaaa3d99b6ce4cb72e7a9db2e` (51.rc, 2026-08-05)
- Vendored on: 2026-08-06

## Not vendored

- `src/main.c`, `src/meson.build` (top-level) — replaced by our own
  `meson.build` in this directory
- `src/appdata/` — GNOME Calendar's metainfo
- `src/gui/gcal-application.{c,h}` — application shell (Post has its own)
- `src/gui/gcal-window.{c,h,blp}` — window shell; its glue is re-implemented
  in `src/views/calendar/`
- `src/gui/gcal-tests.c`
- `src/core/gcal-shell-search-provider.{c,h}` — GNOME Shell D-Bus search
  provider

## Local changes

The vendor import commit contains pristine upstream files. Every local
modification is made in separate commits on top, so `git log -- src/gcal`
shows the exact patch set to re-apply when re-vendoring a newer snapshot.
