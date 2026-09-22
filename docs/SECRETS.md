# Everything hidden in the tank

Written for whoever picks this up next — including me, three weeks from now,
having forgotten all of it. Nothing here is discoverable from the code
without knowing it exists, which is the point of most of it, and the problem
with all of it.

Two kinds of thing live here:

- **Secrets** — meant to be found by a keeper, eventually. Taps on things,
  a combo, a rare event.
- **Workbench** — meant for us. Dev pages, overrides, the serial director.
  Shipped on purpose: a gifted device with no debugger is a device you
  cannot help over the phone.

---

## The way in: the reef builder combo

The builder is not on any menu until it is found. On the **live tank**, in
order, with no more than **3 seconds** between steps
(`REEF_COMBO_GAP_S`, `common/reef.h`):

1. swipe **left**
2. swipe **right**
3. swipe **up**
4. swipe **down**
5. tap the **right third** of the glass
6. tap the **left third**
7. tap the **middle**

A swipe is any drag past 30 px; whichever axis moved furthest decides it.

Everything after the first step is **swallowed** — the taps raise no fright,
the swipe up does not open the overview, the tank is not disturbed while you
are doing it (`reef_combo_busy`). A wrong move starts the run over, and if
that wrong move happens to be a swipe left, it counts as step 1.

Four thousand random gestures opened it zero times, which is the test that
matters: this must not happen by accident while somebody plays with their
fish.

When it lands, the device **shows you the way rather than telling you**: the
overview opens with UPGRADES flashing three times, then the shop opens with
a small coral in its top right corner flashing three times. That corner icon
is the permanent door from then on (`reef_icon_hit`). The flag is saved
(`tank_t.reef_open`), so it stays found.

There is also a **REEF BUILDER** switch on the dev page, for when you need it
without the ceremony.

## The workbench

| How | What |
| --- | --- |
| **7 taps** on the `FW ...` line, bottom left of Settings | opens the **dev page** (`SET_DEV_TAPS`). From the third tap the line counts down, the way a phone does |
| **5 taps** on the sand-dollar coin at the top of the shop | **+1000 sand dollars** (`SHP_DEV_TAPS`, `SD_DEV_GRANT`) — comfortably the whole shelf, with room to remove and re-buy while looking at it |
| the dev page | battery override (steps the bolt through every colour), force a bass party, drop a milestone, the reef-builder switch, and the rest |
| USB serial | the **director** — `help` lists it. `dollars 1000`, `buy <key>`, `place <key> <x>`, `touch bias <px>`, and friends. Item keys are the `key` column of `SD_ITEMS` in `common/progression.c`: `plant snail castle laser bass glow totem disco chest diver` |

## Things you can tap

The tank is more alive than it looks. Almost everything in it answers.

| Tap | What happens |
| --- | --- |
| a **fish** | its card, and the view eases in to 2× and follows it. A ticker along the foot says what it is doing and what last happened to it |
| the **card** | that fish's own page — levels, badges, DOING and LAST. Long lines scroll by themselves; swipe to take over |
| a **bar** on that page | what the level actually means |
| the **glass**, twice | the light on or off (only in MANUAL; the bulb beside the LIGHTS OUT row says so) |
| the **pile of glow sticks** | they scatter, bouncing off the walls. A single settled stick hops instead |
| the **totem** | the nearest calm fish goes and fetches it — ignoring the social bar, the cooldown and the hour (`TOTEM_INVITE_S`, 26 s to get there) |
| the **disco ball** | it lowers and spins for 30 s. Tap again to stop it |
| the **treasure chest** | it opens now, and **stays** open until you tap it again |
| the **diver** | he turns round and plods the other way |
| **drag down from the top edge** | feed |
| **stroke** the glass | wipe algae off it |
| **slash** horizontally through a kelp canopy | trim it |

## Things that happen on their own

- **The bass party.** With the speaker *and* the totem in the tank, a really
  sociable fish eventually lifts the totem, marches to the stack, plants it,
  and the whole school dances for 40 seconds before it is carried home. The
  lights drop for it. About 75 seconds all in. A first-timer needs to be
  very sociable (`TOTEM_SOCIAL_MIN`); every party a fish attends lowers that
  bar for it, down to `TOTEM_SOCIAL_FLOOR`.
- **The club from outside.** While that party runs, the device plays a
  muffled four-on-the-floor — everything above a few hundred hertz gone, the
  way it sounds through a wall — at the same 140 BPM the rig thumps at.
  There is a **MUSIC** row in Settings for it, and that row only exists when
  the tank holds both pieces.
- **After hours.** Turn the light out with any of the glowy gear in the tank
  and nobody goes to bed for **five minutes** (`AFTERHOURS_S`). A resting
  fish is steered to the glow sticks instead of the reef, they want one three
  times as much, and they are not drawn asleep.
- **Catch.** A fish carrying a glow stick high enough will throw it to
  another. Three passes in one rally is the rare one and fires its own event
  (`GLOW_RALLY_GEM`, `TEV_GLOW_RALLY`).
- **Curiosity.** Put something new in and the fish come and look it over.
- **The snail** grazes the glass clean, even while the tank sleeps.
- **The screen flips** when you turn the device over — the IMU drives it, and
  the touch map flips with it. No IMU, always upright.

## In the reef builder

- **Swipe up** from the canvas for the coral catalogue; **DONE** leaves.
- The rail is three big squares: the **colour** (opens a panel of fifteen),
  **OUT** and **RESET** (which asks first).
- **OUT mode has a magnifier.** The first tap does not remove anything — it
  zooms 2× on that spot so a finger can pick one coral out of a crowd. Drag
  to move the view, `2X` steps to 3×, BACK leaves. The *second* tap removes.
- A piece lands where your finger **lifts**, not where it went down, so you
  can aim from the clear water at the top where you can see. The same coral
  stays in hand afterwards, resting on what you just placed.
- Coral only goes **on the floor or touching something already built**.
  Nothing floats.
- **Settings → REEF → HIDE** takes the whole thing out of sight without
  deleting it. It is still saved, and the builder still shows it.

## Fork discipline, because it will bite you

This is a fork. Upstream owns the low bits of everything and keeps taking
the next one.

- **Shop unlock bits** start at `SD_LOCAL_BIT0` (16). Never `1u << index` —
  read `SD_ITEMS[i].bit`.
- **Milestone bits** start at `MS_LOCAL_BIT0` (20).
- **The save's local tail must stay last**, and
  `progression_save_tail_is_last()` asserts it. It has caught a bad append
  three times.
- **Append to that tail, never insert.** `decor_x` in the save is eight slots
  wide and cannot grow: the chest and the diver (items 9 and 10) have their
  spots appended at the very end instead. Inserting would have moved every
  byte after it and an existing tank would have read its reef and its
  settings out of the wrong place. There is a test that truncates a save to
  the old length and loads it.
- New UI belongs in `common/ui_ext.c` and `common/reef.c`, which upstream
  does not have, drawn with `render.h`'s public primitives.

## Testing any of this

There is no test runner in the repo. The suites are host programs built
against `common/` with a stub port layer — they render real frames and drive
real gestures, and several of the bugs in this document were found by them
rather than on the device. Build one like this:

```bash
cc -O1 -Icommon -o /tmp/t /tmp/t.c /tmp/ports.c \
   common/tank.c common/render.c common/progression.c common/advisor.c \
   common/reef.c common/icons.c common/ui_ext.c common/setup.c common/notice.c -lm
```

`ports.c` is four stubs: `persist_port_load/save/erase`, `clock_port_now_unix`,
`version_port_string`. For anything that touches saving, link
`sim/persist_port_sim.c` instead and get a real file.

**Turn the scene cache on when you test rendering.** `render_set_scene_cache`
and `render_set_dirty_mask` are what the device uses, and a test without them
takes a different path through `render_tank`. Hiding the reef passed for
exactly this reason while being broken on hardware.
