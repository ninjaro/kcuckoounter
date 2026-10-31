# Kcuckoounter runtime contracts

Kcuckoounter is a Qt/KDE card-training application. Presentation and data ownership
remain independent: changing desktop appearance does not change a drill, counting
strategy or active session.

The nonvisual gameplay engine, strategy catalogue, asset lookup and physical
layout projection live in `core/` (`gameplay:core`). Their tests use
QCoreApplication without constructing Widgets/KDE. QtCore and Packing are genuine
core dependencies. Windows, settings surfaces, rendering and platform integration
remain in `app/` (`trainer_app:ui`); the public executable remains
`trainer_app:app`. Persistence and gameplay policy remain owned by Kcuckoounter.

All existing visual choices remain supported and switchable: Classic/Thin card
frames, Numeric/Chips input, Classic/Count-stamp feedback, Classic/rail/pill
actions, Classic/card/drawer/sill settings, Classic/compact toolbar and
Classic/Instrument HUD. These choices project the same gameplay state; selecting
a presentation does not replace a session, change its rules or rewrite scores.

## Material ownership

Appearance supplies Felt/Slate tokens, typography and scoped window/control
styling. The host selects table colour, artwork and component variants. Window
and preview contexts remain independent; material changes do not change a
session. Native mode retains the platform Qt/KDE style. Provider fonts and
control assets are installed under `share/shared_ui/assets`, separately from
host artwork; a custom SDK prefix must expose its shared data to Qt.

## Desktop HUD ownership

Both Classic and Instrument HUDs use Appearance's `status_strip`. The host
projects the owned session into explicit captions, values, score
continuations and status indicators, alongside the unchanged readable status
text. No localized status string is split to recover fields. Errors explicitly
replace the fields with plain text.

Appearance measures and places the status, progress, pickup readout/slider and
clock before committing geometry. Narrow layouts and long fields wrap; hidden
readouts take no space. The host retains pickup limits, accepted speed changes,
phase restrictions, clock/score/progress values and disclosure decisions.
Switching materials or HUD variants changes no game state. Desktop Qt and KDE
have regression coverage; this extraction does not establish Android acceptance.

## Secondary settings presentation

Staged card, drawer and sill editors use Appearance's `editor_panel` for the
header, close button, native OK/Cancel footer, Escape and measured promotion
into a separate window. Promotion retains the same draft widget and focus.
The host supplies card/legend clearance, alignment, labels and stable object
names. It decides eligibility and alone applies or rejects edits.

Classic and the decorated staged editors use the same owned deck draft.
Strategy data, exact signed answer values and legal field ranges remain host-owned. The shared panel introduces no second
transaction model and changes no commit boundary or gameplay phase rule.

## Appearance preference ownership

The host stores presentation in the existing `desktop_ui/preset` and
`desktop_ui/overrides/*` keys. Its typed preferences adapt to the shared
`appearance_state`; unknown preset, component and option IDs are retained while
the current UI uses its established fallback. An explicit override remains
distinct from inheritance even when it equals the preset default.

The settings form displays unsupported saved choices as unavailable. Unrelated
edits preserve them, including retained storage types. Choosing a known preset
clears recognized component selections while keeping unsupported choices;
explicitly replacing a setting or resetting components removes the corresponding
retained choices. Appearance Apply updates only changed trainer palette/orientation
keys and never replaces the trainer preference group, session or window state.

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

Single-SVG markings, standalone cards and visible table cards use one
application-owned image service. Clients load immutable
SVG bytes, decode SVGZ containers and replace lifetime-owned subscriptions.
Rasterflow owns asynchronous preparation and shared image masters; widgets retain
display pixmaps, include device pixel ratio in physical demand and fit artwork
without stretching. Removing a display withdraws only its own demand.
Selected card pixmaps retain the complete master; final sizing and arbitrary
display rotation use one smooth painter transform. There is no intermediate
display-sized downscale or angle-specific raster identity. Presentation still
fits the original aspect ratio and preserves the requested orientation.

Source discovery enumerates readable bundled and installed files without parsing
SVG on the GUI thread. Selected-source validity, element bounds and aliases use
Rasterflow's asynchronously prepared metadata. Document readiness does not
require a complete element catalogue. Alias and base-frame queries inspect only
the candidates needed by current image demands, preserving their existing order.
Layouts find the first valid card geometry incrementally and retain usable
base/last geometry until the current selection is ready, then update through one geometry
notification; superseded source completion has no authority.

Source acquisition reads regular files/QRC inputs, decodes SVGZ and constructs
immutable Rasterflow content on one application-owned file worker. Queued path
replacements occupy only one slot per current subscription; canceled or retired
completion cannot register or publish obsolete content. Source layouts, cards
and standalone SVG images await acquisition before submitting metadata/image
demand. No file read, decoding, full-byte copy or hash runs in their GUI callbacks.
Only document selection and card semantics belong to this acquisition layer;
all SVG inspection, raster scheduling and image storage remain in Rasterflow.
Rasterflow derives content identity once; the game keeps no path/hash aliases.
Equal decoded SVG/SVGZ contents share a snapshot, and changed bytes at the same
path create a distinct snapshot without invalidating existing holders.

Card aliases, fallback selection and base-frame trimming belong to the client.
Standalone widgets request only their displayed face or back; empty and hidden
card presentations withdraw that demand. State, visibility, geometry and DPR
events update intent; painting does not submit image work. Different displays
share compatible Rasterflow masters. Displayed handles can survive a source
transition, independently of backend cache ownership. The table owns neither a
raster cache nor image workers. It combines the current visible face/back demands
by logical ID and physical size, publishes first useful faces independently, and
retains only the visible presentation subset. A source change can retain that
subset until its replacement is ready; unrelated deck elements are never a
completion barrier. Each slot borrows its own currently useful image, with no
upscaling across geometry changes.
Settings previews use that same service. Visible faces are required; at most two
adjacent candidates per carousel are speculative. A hidden carousel withdraws its
intent. Initial faces publish incrementally; source or physical-size changes
replace the visible window coherently without waiting for speculative neighbors
or their fallback preparation. Presentation rejects insufficient physical
resolution. Weighted labels decorate separate GUI pixels and never modify shared
masters. Source, raster and display
lifetimes remain distinct;
visibility does not turn a valid prepared session into a different game.
Retaining artwork is not redistribution approval: licensing/provenance uncertainty
remains and this contract grants no additional rights.

## One gameplay implementation

Qt, KDE and Android use `gameplay::session` for New, Start, Pause, Resume and
Finish. Saved drills describe fresh setup; active recovery retains exact prepared
continuation and always restores Paused. Pending terminal results are admitted
once to current history, never reopened as playable sessions. Old trainer runtime,
checkpoints, scores and conversion routes are removed. Presentation variants do
not change these rules. Android kit/emulator acceptance is recorded separately
from desktop/native tests; this contract makes no physical-device guarantee.
