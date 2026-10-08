# Networked chase polish design

## Goal

Make a two-player pursuit feel responsive and readable for both the host and the joined player. Preserve the authoritative round rules while giving the runner enough map-based chances to create distance.

## Networked driving and presentation

The server remains authoritative for vehicle state, rams, health, objectives, and results. The owning client immediately simulates its submitted steering, throttle, brake, and boost inputs, then smooths to authoritative vehicle snapshots. Remote players continue to interpolate server snapshots. Replicated cosmetic vehicle state drives brake smoke and boost flames on every client.

The server owns a replicated day-time value that advances through a complete day/night loop in 180 seconds. Each local time-of-day actor renders that value rather than advancing an independent clock.

## Roles, vehicles, and feedback

The reveal lasts 10 seconds for every reveal. The tracker cone points toward the runner; its ring behavior is unchanged. A large role banner appears at round start and the persistent role label remains strongly color-coded.

The chaser vehicle has no chase damage limit and has a 30 percent higher maximum speed. The runner vehicle has 150 percent more engine force than the chaser (2.5x), quicker steering response, and the existing maximum speed. The runner's vehicle-health bar is larger and highly visible. Accepted rams remain server validated, remove one quarter of runner vehicle health, and replicate a burst/flash to both players.

The existing pistol prop and tracer are reused only for the on-foot chaser. Server hitscan has a 50m range and 0.8s cooldown. Hits slow an unprotected on-foot runner by 50% for 0.6s; capture remains the win condition. Runner and driving fire requests are denied.

## Minimal UI direction

The screen has one focal instruction at a time. Role appears as a short, high-contrast start banner and compact persistent role chip. Vehicle health is only large and persistent for the runner; the chaser does not display a meaningless vehicle-health bar. Objective or escape direction uses the existing world cue and a short contextual line, not a second map panel. Reveal countdown, interaction progress, and hit feedback are transient center-screen events. Secondary status text fades after a few seconds. The HUD must not add a permanent element merely to expose an internal value.

## Breakaway stations

Two road-validated breakaway stations are placed from the city road graph. The runner must be on foot and hold interact for three seconds. A station is consumed for that round and grants the runner a one-time eight-second acceleration and boost-recharge advantage. The server validates role, range, phase, and station availability. Its active/consumed state replicates and the HUD shows the runner's remaining breakaway duration. This supplements relays and does not replace them.

## Validation

Build Development and Shipping, package the game, and run the existing two-process regression. Add narrow checks for role modifiers, ten-second reveal, synchronized clock state, one-time breakaway consumption, accepted-hit feedback, and chaser-only on-foot pistol gating. A rendered two-player run checks guest responsiveness, visible smoke/flames, common time-of-day, HUD hierarchy, hit burst, and breakaway behavior.
