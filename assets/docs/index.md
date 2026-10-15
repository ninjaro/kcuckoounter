# Kcuckoounter runtime contracts

Kcuckoounter is a Qt/KDE card-training application. Presentation and data ownership
remain independent: changing desktop appearance does not change a drill, counting
strategy or active session.

All existing visual choices remain supported and switchable: Classic/Thin card
frames, Numeric/Chips input, Classic/Count-stamp feedback, Classic/rail/pill
actions, Classic/card/drawer/sill settings, Classic/compact toolbar and
Classic/Instrument HUD. These choices project the same gameplay state; selecting
a presentation does not replace a session, change its rules or rewrite scores.

## Diagnostic boundary

Kcuckoounter does not publish application telemetry or a JSON metrics catalog.
In Debug Linux builds it opens the shared fixed-size watchdog state used by
Monitor.

Published progress/context is deliberately small:

- `gui` heartbeat from the optional Qt event-loop adapter.

There are no cache/game probes, resize histories, geometry timelines,
snapshots, process reports, launch flags, environment endpoints, reconnect
queues, or Monitor UI actions. Cache experiments use Monitor's external RSS
sampling while ordinary application resize and slot-count workflows drive the
cache.

The producer and external watcher must agree on the current fixed shared-state
layout and exact process identity. The observer owns CPU/RSS sampling and health
interpretation; freshness is not proof of per-job progress.

## Game and presentation ownership

The default trainer and the session-owned gameplay route are both active.
Their runtime cutover is not established by the gameplay User Stories; neither
is retired by the current-format policy. Exact session restoration, paused
recovery and result history remain product requirements in either route.

`gameplay::session` owns session-local deck IDs, configuration, prepared stream
state, running counts, quiz batches, active-time accounting and results.
Physical slots are separate IDs; inverse deck/slot mappings preserve deck state
when placement changes. Rejected domain operations are atomic. Widgets project
the owned state rather than storing another shoe, quiz or penalty model.

Packing owns geometry. The table applies the physical-index traversal to its
session mapping; it does not turn a placement permutation into deck identity.
Animation and clock delivery do not own scoring or persistence. Settings/storage
and the native launcher own durable admission, checkpoint/result I/O and recovery;
a rendering callback is not a storage-service owner.

Drills describe fresh-session inputs; checkpoints describe exact continuation;
results retain verified counters/context. These categories remain separate.
Current internal formats alone are supported. Named drills use one JSON object
containing `drills`; each entry carries the nine global choices, name, pickup
interval and deck configurations. No old trainer drill codec, conversion dialog
or cross-model drill adapter exists.

Identified recovery contains `phase`, `sequence` and `payload`: either an active
checkpoint or an exact pending result. Checkpoints are nested payloads, not an
independent persistence service. Progress contains completed-session count, raw
challenge/timing counters and recent verified results. Sequences and random-stream
positions describe current session state, not schema or algorithm versions.

These formats contain no numeric schema, continuation or cadence revisions. Exact
current fields and domain invariants are validated; old version-tagged records,
missing state and unexpected fields are rejected without conversion. Unsupported
records require an explicit reset, while current-format failed-write protection,
reservation identity, retries and admission remain required. Strategy preferences
use the current slug alone; no stored numeric ID repairs a renamed slug. Qt's own
opaque layout validation uses its standard API without an application version tag.

## Strategy catalogue

The bundled catalogue contains 20 strategies with preserved identifiers and tags.
Uston SS is ID 12: its complete 52-card tag sum is +4 and its initial count is
`-4 * decks` (−4/−8/−24/−32 for one/two/six/eight decks). The executable expression
contains no explanatory prose. Counting/reset logic uses the actual configured
number of decks; unknown strategies or invalid formulae are errors.

## Cards and rendering

The retained card-set IDs are `0, 4, 9, 11, 23, 28, 40, 42`. Set 0 is the default;
an unavailable retired selection resolves to that default. Canonical logical
face/resource IDs are untranslated machine identifiers. Physical and embedded
resource lookup preserve the same artwork and metadata; generated caches and
retired downloader output are not application inputs.

Parsed SVG contexts are job-owned, with lazy fallback loading. Raster, source
and displayed pixmap lifetimes are distinct. Generation acceptance, cache cutover
and request coalescing preserve bounded state and reject superseded results;
visibility does not turn a valid prepared session into a different game.
Retaining artwork is not redistribution approval: licensing/provenance uncertainty
remains and this contract grants no additional rights.
