# Citix — Project Goal

## Vision

Citix is a large open-world **driving and exploration** game built in Unreal Engine 5,
with server-authoritative multiplayer (2–4 drivers in one shared city).
It is structurally similar to GTA (seamless city, free driving, traffic, day/night,
weather). Driving and exploration remain the core loop:

> Drive → explore → discover districts → enjoy the scale and atmosphere of a
> believable modern East Asian megacity.

Around that core, each driver has independent progression (deliveries, activities,
photo contracts, weapon arsenal, wanted level) plus optional PvP combat
(ramming, gunfights, police pursuit) and head-to-head road races. Progress is
session-only by design.

Primary inspirations: Shanghai (Pudong skyline, elevated roads, layered
infrastructure), Tokyo, Hong Kong, Seoul.

## Design pillars

1. **Scale that feels enormous** — a city that is genuinely large, dense and varied,
   not a handful of blocks.
2. **Driving first** — the vehicle and camera are the primary interface to the world.
3. **A living city** — traffic, pedestrians, lights, ambience; the city works without
   the player.
4. **Systems over content** — the city is *generated*, not hand-placed. We build tools
   and data, not thousands of objects.
5. **Performance is a feature** — simulation and detail scale with distance from the
   player; the frame budget is protected everywhere.

## Non-goals (for now)

- Story/missions with authored narrative (contracts, deliveries, tours and races
  are systemic, not scripted).
- 1:1 reproduction of Shanghai. The city is a **fictional, compressed** megacity.
- Interiors until the driving/city core is solid.
- Persistent accounts/progression (session-only by explicit design).

## Target scale

Development starts with a **1 km × 1 km vertical slice** proving the generation
architecture. Scale only increases once performance and generation are stable:

```
1 km²  →  2 km²  →  4 km²  →  8 km²  →  10–15 km wide final city
```

## Districts

The city is divided into regions, each with its own generation rules:

| District | Inspiration | Character |
|---|---|---|
| Financial | Pudong | Supertall towers, glass, wide avenues, plazas, LED |
| Downtown | Lujiazui / Jing'an | Office towers, commercial streets, dense grid |
| Old Town | Old Shanghai / shikumen | Narrow streets, low-rise, alleys, signage |
| Residential | Shanghai suburbs | Apartment towers, compounds, parks, local shops |
| Industrial / Port | Yangshan / Waigaoqiao | Warehouses, containers, cranes, wide open lots |
| Outer City | Ring roads | Low density, highways, logistics, transport |

## Success criteria for the first milestone

A player can load the game, spawn in a car, and drive around a ~1 km² city that has
connected roads, intersections, sidewalks, streetlights, varied procedural buildings
from several districts, traffic infrastructure, basic moving traffic, acceptable
performance, and a structure that can be scaled up without rewrites.
