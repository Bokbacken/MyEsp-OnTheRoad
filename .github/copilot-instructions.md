# Repository and documentation conventions

Keep changes focused and preserve ESP32-S3 and ESP32-P4 support. Follow the
existing localization and test conventions. Do not claim physical verification
from a successful build or host test.

## Documentation ownership

- `README.md`: the current project description, supported hardware, user
  features, setup/build/update instructions and links to detailed references.
  Update descriptions when behavior changes. Do not prepend release notes,
  previous-version paragraphs, commit/CI histories or session reports.
  The firmware version source is `PROJECT_VER` in `CMakeLists.txt`; avoid a
  second manually maintained version banner in README.
- `CHANGELOG.md`: versioned changes, compatibility/migration notes and concise
  release-specific verification caveats, newest first. Add to an existing
  version entry when appropriate rather than duplicating it. Historical
  entries describe their original context; do not rewrite them as current
  instructions. Do not treat a merge as publication of a GitHub Release.
- `docs/ADVENTURE_TODO.md` and `docs/MULTI_BOARD_TODO.md`: actionable work,
  delivery status, unresolved issues and pending hardware checks. Read the
  relevant checklist before implementation and update it in the same PR.
  Record owner-confirmed hardware results precisely; do not infer unreported
  devices, scenarios or measurements. Keep historical log entries distinct
  from the current status.
- Topic references under `docs/`: detailed current behavior, authoring syntax,
  design decisions, file management and hardware facts. Update the relevant
  reference when its behavior changes; link it from README instead of copying
  long explanations into README.
- Adventure bundle READMEs: installation and playing instructions. Keep
  spoilers/solution traces separate from player-facing hints.

For each behavioral change, update the relevant reference and checklist; add
a concise changelog entry for a versioned delivery. Change README only when
its current feature/setup description needs to change. Documentation-only
reorganization does not require a firmware version bump.

Before creating a PR, check for a PR template and follow it if present.
Include this trailer on commits:

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
