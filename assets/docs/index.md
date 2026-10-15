# Kcuckoounter runtime contracts

Kcuckoounter is a Qt/KDE card-training application. Presentation and data ownership
remain independent: changing desktop appearance does not change a drill, counting
strategy or active session.

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
Current internal formats alone are supported. Malformed/current-format recovery
and failed-write protection remain required; historical notes do not authorize
old-version decoders or silent reinterpretation of missing gameplay state.

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
