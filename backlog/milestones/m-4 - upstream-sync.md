---
id: m-4
title: "Upstream Sync"
---

## Description

Keeping this modernized depot current with its parent, `returnoftheshadow/RotS_Live`
(`upstream` remote), whose `release-frodo` branch is where live-game fixes land. Full
narrative: `backlog/docs/arc-upstream-sync.md`.

**What it buys:** the live game's bug fixes and gameplay changes reach the modernized
tree instead of stranding on the legacy layout — without them, the two depots drift until
merging becomes a rewrite. Each sync also stress-tests the modernization: upstream logic
must be re-expressed through the Placement API, hooks, and per-library layout, proving
those seams carry real change traffic.

**Provenance:** standing relationship declared in AGENTS.md ("This depot is a child of
`RotS_Live`"); three prior syncs merged 2026-07-06..10 (`91bc44ae`, `a637ece0`,
`10536a93`, the last under the upstream-sync-validation design). Reactivated by the owner
(David / drelidan7) on 2026-08-18 in conversation: "We need to add a backlog task of
medium to high priority for merging release-frodo into this branch. Merge-conflicting
changes went in there a day or two ago, and bringing the logic over will have some
challenges."
