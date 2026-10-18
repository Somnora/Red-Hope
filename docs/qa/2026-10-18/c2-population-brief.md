# C2, population at scale: design and three questions (2026-10-18)

C1 makes the sim fast enough for thousands without changing anything a player sees. C2 is where the game itself changes so a city of thousands *plays* right. It builds on Q1a: everyone stays a named individual.

Three things change: how Hope is counted, what happens in a shortage, and how people arrive. Every number below is a placeholder chosen so that **today's colonies score within a point or two of now**. The battery suites that pin Hope are re-derived on purpose, as D3a's were, and every moved line is explained in the commit. **Answered 2026-10-18: Q5a Q6a Q7a** (all three recommendations).

New player-facing wording (alerts, panels) is Gate-D placeholder copy.

---

## 1. Hope that means the same at 10 people and 5,000

Today two terms count people, not shares, and one counts rooms per floor:
- each unsupported colonist costs a flat 10 points;
- each filled job seat adds 1.5 points, with no cap;
- each room type pays its bonus again on every floor and habitat network.

At city size the first sinks Hope to zero and the second pins it at 100, whatever the city is like. **Every term becomes a share or a rate:**

| Term | Today | C2 |
|---|---|---|
| Unsupported | -10 per person | up to -30, by the share of people unsupported (steep: one in three unsupported costs the full 30). One person out of 7 still costs about 13 |
| Jobs | +1.5 per filled seat, uncapped | up to +10, by the employment rate (filled seats per person). Today's 4 seats for 7 people still give about 6 |
| Rooms | +2.5 × weight per room type, per floor or network | the same weights, counted once colony-wide, by how well each room type covers the people who need it (one room of a type per so many residents) |
| Room adjacency | -8 per offended pair | by the share of homes next to an offended pair |
| Furnishings, Views | capped sums | per resident, capped |
| Housing, Comforts, Food, Water, Milestones | already shares or colony-wide | unchanged |

## 2. Shortages shared fairly, one alert per event

Today, when food, water or air runs short, each colonist takes their whole ration or nothing, in roster order. So the shortage falls on whoever is last in the list, and each of them starts a 2-sol countdown to being sent back to orbit. At city size that is arbitrary, and a failing floor fires one alert per person.

**C2:**
- A shortage is shared: everyone in the affected home (a floor, a surface habitat or the whole colony, by resource) gets the same fraction of their ration.
- The colony goes on short rations, which the Hope line shows.
- If it lasts, people leave **gradually, in proportion to how deep and how long the shortage is** (Q6).
- One alert per event, not per person: e.g. "Floor -2 is losing air: 340 residents on emergency reserve" (placeholder wording).

## 3. People arriving at scale

Today the only arrivals are a 4-person Crew Pod per ship and one birth per 20 sols. C2 adds:
- **The Colonist Transport,** a manifest item carrying 20 people. Like the pod, it needs free beds, or they stay aboard.
- **Births per head,** the same threshold but a rate per 100 people, so a city grows faster than a crew.
- **Immigration (Q5).** A thriving city attracts people from Earth on the supply line. The rate rises with Hope above THRIVING and with free homes, and stops at full housing. This is also the hook for the politics step, where rivals compete for the same people.
- **The Big Arrival (Q7),** from the brief's Phase 2 exit.
- **Moving home.** People can move to free beds in better homes, which C6's commuting will also use.

---

## Q5. Immigration

| Option | What it is |
|---|---|
| **a (recommended)** | **Yes.** A thriving city draws people on every supply ship, more the higher Hope and the more free homes. Building a good city *is* how you grow it. |
| b | No. Growth only from transports you order and from births. |

## Q6. Who leaves in a shortage

| Option | What it is |
|---|---|
| **a (recommended)** | **Shared rationing; people leave gradually, in proportion to the shortage.** No one is singled out by roster order. A short, shallow shortage costs Hope; a long, deep one shrinks the colony a little at a time. |
| b | Keep today's per-person countdown, but pick who goes short fairly (e.g. by rotation). |

## Q7. The Big Arrival

| Option | What it is |
|---|---|
| **a (recommended)** | **A one-off wave of about 100 colonists, once the colony proves it can hold a city:** about 20 sols at THRIVING with 100 free beds. The CEO announces he is coming with the next wave; his landing belongs to the politics step. |
| b | Skip it. Growth comes from transports, births and immigration only. |

---

## How C2 is checked

- The battery's Hope-gated suites are re-derived, and every moved line is explained.
- New fails-loudly checks:
  - Hope at N = 10, 100, 1,000 and 5,000 for the same proportions of support, jobs and rooms;
  - a shortage shared evenly;
  - departures in proportion to the shortage;
  - one alert per event;
  - transport, immigration and Big Arrival arrivals.
- The -scale suite stays within its C1 budgets.
- Save format: bumped; the hand-play checkpoints re-make themselves.
- The 110-sol playtests are re-run to show the crew phase still plays as the D-package left it.
