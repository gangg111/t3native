### Agreed interfaces (phase A review)

Where a header could not follow RULES.md mechanically, this is the form every `.cpp` uses.
All headers in `src/` compile together (`g++ -fsyntax-only` over every header) and keep every
GDScript name (`tools/check_names.py`: 0 missing).

| GDScript | C++ |
|---|---|
| `FighterState.move` (property: get, set writes pose_move and root_move) | `f->get_move()`, `f->set_move(row)` |
| `FighterState.anchor` (property) | `f->get_anchor()`, `f->set_anchor(v)` |
| `FighterState._copied(v)` (reflection) | overloads of `_copied` + `copy_from` over the member list |
| `FightState.mode`, `frame_counter`, `rounds_option`, ... (properties over `region` / `progress`) | `IntProperty` slots: `fight->mode = v;`, `gd::Int v = fight->mode;`; write `gd::Int(fight->mode)` in `?:` and template arguments |
| `ByteBlock` subclass properties (`ModeRegion`, `GameProgress`, ...) | `ByteField` slots, same syntax as above |
| `CpuOpponent.ai_self = v` (setter also writes ModeRegion) | `ai->set_ai_self(v)`; reading is `ai->ai_self` |
| `FightSimulation.ball` (getter) | `sim->ball()` |
| `TekkenBall.fight`, `TekkenForce.fight` (WeakRef) | `ball->fight()` (a `weak_ptr` lock); constructors take `const gd::Ref<FightState>&` |
| `FlowPart.g` (WeakRef to GameFlow) | `GameFlow* g`; `g.x` is `g->x`; constructors take `GameFlow*` (GameFlow passes `this`) |
| `Array[bool]` | `gd::Arr<uint8_t>` (`std::vector<bool>` has no `bool&`) |
| `EffectObjects.Model`, `.UvTable`, `PracticeMode.GuideState`, `.Item` (enum names clash) | `struct Name { enum : gd::Int {...}; }`: `EffectObjects::Model::CLOUD` |
| unnamed enums (`BankType`, `Character`, `MoveFlag`, `Tracking`, `Transition`, `AttackWord`, `RoundState`, `CameraPhase`, `GameMode`, ...) | `enum : gd::Int {...}` in the class: `BankType::OGRE` |
| `CameraDirector._look_point` -> `[r, look]` | `struct LookPoint { gd::Int r; gd::PI32 look; }` |
| `CpuOpponent._walk_to` -> `[found, after, terminator]` | `struct WalkTo { bool found; gd::PI32 after; gd::PI32 terminator; }` |
| `Combat.WEAPON_POINTS` rows | `struct Combat::WeaponPoint` |
| AI callables (`mark_candidates`, `_gather`, `_gather_ordered`) | `std::function`, argument order as declared in `ai_decision.h` / `cpu_opponent.h` |
| `FightState.practice_pads` (Callable) | `std::function<void(gd::Int, gd::Int)>`; empty means null |
| JSON-shaped Dictionaries in content (`FightTables::characters`, stage tiles, ...) | `gd::Json`, read with `JsonFile::number(rec["key"])`, `rec["key"].str()` |
| `segments` locals (`attack_segs` or `effects.segments[i]`) | `gd::Arr<gd::PI32>` |
| `MotionBank.link` passes `self` | `MotionBank` derives `enable_shared_from_this`; banks are always created by `gd::make` |

Game-flow stage additions:

| GDScript | C++ |
|---|---|
| `ModeStart.*`, `Opponents.*` static funcs taking `flow: GameFlow` | take `GameFlow*`: from a FlowPart pass `g`, from GameFlow pass `this`, from tests `flow.get()` |
| FlowPart subclasses (`ModeRules`, `MatchFlow`, `CharacterSelect`, screens) | constructors take `GameFlow*` |
| `RankingScreen.TableMode`/`.Sub`, `TheaterScreen.Focus`/`.Choice` (enum names clash) | `struct Name { enum : gd::Int {...}; }` |
| locals or members named like C++ keywords (`new`, `short`, `long`, `throw`) | trailing underscore: `new_` |
| `MatchFlow.Sub` | `enum Sub : gd::Int` in MatchFlow |
| `OptionsScreen.save_writer`, `GameFlow.performance_factory`, `card_loader` (Callables) | `std::function`, empty means not valid |
| `EnbuAssets.performance` as `GameFlow.performance_factory` | a lambda over a `gd::Ref<EnbuAssets>` |

Out of the fight slice: `PracticeMode` needs `GameFlow` (not yet translated) for its `.cpp`;
its header compiles and `FightSimulation` uses it through `gd::Ref<PracticeMode>` only.
