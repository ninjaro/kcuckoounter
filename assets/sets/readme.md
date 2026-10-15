# Card artwork and provenance

The card inventory is `cards/cards.json`; each referenced `cards_N.json` keeps
its source URLs, captured author/license claims and original input hashes.
See [the complete curation audit](../../docs/card_sets_audit.md) for visual
classifications, runtime compatibility, license conflicts and the final
selection. The eight retained IDs are **0, 4, 9, 11, 23, 28, 40 and 42**.
The appearance settings discover bundled themes from this catalog; numeric IDs
are preserved, and retired bundled paths fall back to set 0.
KEEP describes asset quality; it is not publication approval.

Schema 1 has optional `description`, `composition`, `runtime`, `curation`,
`license_audit` and `source_selection` fields. `card_count` counts authored
card-shaped graphics, including backs/extras, not the game's deck size.
`composition` separates faces, jokers, backs and extras. `runtime` records actual
Qt loading and fallback; `group_sprite` identifies repaired addressable groups.
`license_audit` qualifies the older captured `licenses`/`source.license` claims.
`source_selection` preserves historical import filters, source names and
unverified overrides; it is documentation, not a generation instruction.
The original `generated_at` dates remain import history; `curation.reviewed_at`
and the catalog's `curated_at` identify this review.

The reserved default `cards_0.svg` by David Bellot remains byte-for-byte unchanged.
Its local LGPL header and the [Commons page](https://commons.wikimedia.org/wiki/File:Svg-cards-2.0.svg)
claim different minimum versions. The exact license is NOT VERIFIED; the former
unqualified LGPL-2.1-or-later declaration has been withdrawn pending resolution.
Do not treat unknown, partial or conflicting license records as permission to
redistribute. No set has been approved for public redistribution by this audit.

The obsolete bulk downloader has been removed. The temporary
`cards/.card_set_cache/` was permanently retired in A3.4.2 with explicit developer
authorization. Retained SVGs use internal references and embedded images; runtime
loading needs no downloaded inputs. No backup, replacement cache or downloads
were created. Original source URLs/hashes remain provenance records in the JSON.
The shared generator was not changed; fresh resource manifests contain only
the remaining authored runtime assets.
