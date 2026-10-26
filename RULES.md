### Translation rules: GDScript (remake/) to C++ (t3native/)

The GDScript in `Tekken3Remake/remake` (next to this repository) is proven bit-exact against the original game.
The C++ must compute exactly the same thing. **Translate, never reinterpret.** Same order of
statements, same names, same comments (`##` becomes `//`). If something cannot be translated
mechanically, keep the closest literal form and list it in your report.

The runtime is `src/gd/gd.h` (read it first), plus `src/gd/json.h` and `src/gd/io.h`. Finished
examples: `src/core/math/*`, `src/core/fighter/pose_solver.*`, `src/content/motion_bank.*`.

#### Files and names

- `remake/core/a/b.gd` becomes `src/core/a/b.h` + `src/core/a/b.cpp` (same for `content/`, `platform/`).
  Every file starts with `// remake/<path>.gd` and the class doc comment.
- Everything is in `namespace t3`. The C++ class name is the GDScript `class_name`.
- Member, method, constant, enum and parameter names are **identical** to the GDScript, leading
  underscores included (`_solve_limb` stays `_solve_limb`). Never rename. Other agents write code
  against your class from its GDScript alone, so the mapping must be predictable.
- All members are `public` (use `class X { public: ... };`).

#### Types

| GDScript | C++ |
|---|---|
| `int` | `gd::Int` (int64_t). Never `int`, `long`, `auto` or `size_t` for game values or loop counters. |
| `bool` | `bool` |
| `String`, `StringName` | `std::string` |
| `PackedInt32Array` / `PackedInt64Array` / `PackedByteArray` | `gd::PI32` / `gd::PI64` / `gd::PBytes` |
| `PackedStringArray` | `gd::Arr<std::string>` |
| `Array[T]` | `gd::Arr<T'>` (T' = translated T; `Array[int]` is `gd::Arr<gd::Int>`) |
| `Dictionary` | `gd::Dict<K, V>` with the concrete key/value types its uses show |
| `Vector2i`, `Vector3i` | `gd::Vector2i`, `gd::Vector3i` (value types, 32-bit components) |
| a RefCounted class `Foo` | `gd::Ref<Foo>` (nullable, shared); `Foo.new(a)` is `gd::make<Foo>(a)` |
| inner `class Bar` of `Foo` | nested `Foo::Bar`, used as `gd::Ref<Foo::Bar>` |
| `Callable` / lambda | `std::function<R(Args...)>`; lambdas capture `[=, this]` (GDScript captures locals by value) |
| `Variant` | avoid: use the concrete type the code actually stores; if it truly varies, `std::variant` and report it |

Untyped `Array` used as a fixed record (`[slot, x, y]`, `LIMBS`, return tuples): a small `struct`
with named fields, or `gd::Arr<gd::Int>` when every element is an int and it is indexed. Choose
from how the code uses it, and say which in a comment.

All containers are **handles with reference semantics**, like in Godot: copying `gd::PI32 a = b;`
shares the data, `b.duplicate()` copies. Mutating methods are `const`, so pass them as
`const gd::PI32&` and still write through them, exactly like GDScript. Pass `gd::Ref<T>` as
`const gd::Ref<T>&`. If a function assigns to a parameter (`x = Fx.s16(x)`), take that parameter
by value.

Packed-array element access returns a slot (`gd::ElemRef`): `gd::Int v = a[i];` reads, `a[i] = v;`
stores with truncation. Never write `auto v = a[i];`. In ternaries or template calls mixing a slot
and an int, wrap the slot: `gd::Int(a[i])`.

#### Arithmetic (where translations go wrong)

- Every intermediate is 64-bit, like GDScript. A `gd::I32` / `ElemRef` read widens to `gd::Int`
  automatically. Never let two 32-bit C++ values (`int32_t`, `int`, `bool`, enum) multiply or add
  directly: convert one to `gd::Int` first.
- Integer literals: hex or decimal literals >= 0x80000000 get the `LL` suffix (`0xFFFFFFFFLL`,
  `0x80000000LL`). Otherwise C++ makes them `unsigned int` and `-0x80000000` becomes positive.
  An expression made only of literals (`0x10000 * 0x10000`) gets `LL` on its first literal.
- `/` and `%` with a variable divisor: `gd::div(a, b)`, `gd::mod(a, b)` (throw on zero; both truncate
  toward zero, like GDScript and C). With a nonzero literal divisor, plain `/` and `%`.
- `<<`, `>>` by a variable amount: `gd::shl(a, n)`, `gd::shr(a, n)`; by a literal, plain `<<` / `>>`.
- `absi mini maxi clampi signi posmod` are in `gd::`. `abs()` on ints is `gd::absi`.
- `int(x)` of an int is the value; of a bool `(x ? 1 : 0)`; `str(x)` is `gd::str(x)`.
- The build uses `-fwrapv`: signed overflow wraps like int64 in Godot.

#### Operator precedence (differs from C++)

GDScript binds `&`, `^`, `|` tighter than the comparisons `== != < > <= >=`; C++ binds the
comparisons tighter. Parenthesise every bitwise operand of a comparison:
`if a & MASK == 0:` is `if ((a & MASK) == 0)`, `x | y != 0` is `(x | y) != 0`.
`not` binds looser than comparisons: `not a == b` is `!(a == b)`, `not x in arr` is `!arr.has(x)`.
`<<` / `>>` bind looser than `+ -` in both languages (no change). When in doubt, parenthesise.

#### Evaluation order

GDScript evaluates left to right. C++ does not guarantee the order of function arguments or of the
operands of `+`, `*`, `==`, etc. When two or more parts of one expression have side effects (calls
that change state: `rng.next()`, `_take()`, `pop_back()`, anything that writes a member), hoist
them into locals in left-to-right order before the expression.

#### Control flow and expressions

- `match` becomes `switch` on integer constants (one `case` per pattern, `break` always, `_` is
  `default`) or an `if`/`else if` chain for anything else. No fall-through.
- A loop's range is evaluated ONCE in GDScript: `for i in f(x)` / `for i in range(a, g())` hoists
  the bound into a local before the loop (`gd::Int n = f(x); for (... i < n ...)`), never a call in
  the C++ loop condition.
- `for i in n` is `for (gd::Int i = 0; i < n; ++i)`; `for i in range(a, b, s)` likewise; `for x in arr`
  is a range-for (`for (gd::Int x : packed)`, `for (const auto& r : arr)` is fine for Ref/handle
  elements).
- Truthiness: an int `if x:` is `if (x != 0)`; a Ref `if obj:` is `if (obj)`; an array or string
  `if arr:` is `if (!arr.is_empty())` / `!s.empty()`.
- `a if c else b` is `c ? a : b`; `x in arr` is `arr.has(x)`; `x in dict` is `dict.has(x)`.
- `obj is Foo` is `std::dynamic_pointer_cast<Foo>(obj) != nullptr`; `obj as Foo` is
  `std::dynamic_pointer_cast<Foo>(obj)` (a class used as a base gets `virtual ~Foo() = default;`).
- `== null` is `== nullptr` for Refs.

#### Classes

- `extends RefCounted` / no `extends`: a plain class. `extends Other`: `class X : public Other`.
  A method that a subclass redefines is `virtual` in the base (grep the subclasses).
- `var x := 0` is `gd::Int x = 0;`. Packed/Array/Dictionary members default to empty (the handle
  constructor allocates a fresh one, like GDScript).
- **Members whose default is an object (`var events := SimEvents.new()`) are created in the
  constructor in the `.cpp`, in declaration order**, and declared `gd::Ref<SimEvents> events;` in
  the header. Every class with such members, or with an `_init`, declares its constructor in the
  header and defines it in the `.cpp`. `_init(args)` maps to that constructor; GDScript default
  arguments become C++ default arguments.
- `const N := 5` is `static constexpr gd::Int N = 5;`. Constant arrays and dictionaries are
  `static inline const gd::Arr<...> NAME{...};` (or a lambda initialiser for a Dict).
- `enum Kind { A, B = 5 }` is `enum Kind : gd::Int { A, B = 5 };` inside the class (unscoped, so
  both `X::A` and `X::Kind::A` work). A variable of enum type is `gd::Int`.
- `static var` is `static inline`. `static func` is `static`.
- Reflection (`get_property_list`, `get(name)`, `set(name)`, generic `_copied`) becomes explicit code
  over the member list, in declaration order. Report every such place.
- Signals: a `std::vector<std::function<...>>` member and explicit calls; report them.

#### Headers

- Headers declare; `.cpp` files define. Put only trivial one-line bodies in headers and only when
  they touch no other class.
- In headers, forward-declare classes you use through `gd::Ref<T>` or `gd::Arr<gd::Ref<T>>`
  (`class FightState;`). `#include` only what a header needs complete: a nested class you name
  (`FighterInput::PadWords`), a base class, `gd/gd.h`, `core/math/fx.h`.
- `.cpp` files include every header they use.

#### Logging and errors

`Log.error(msg)` is `Log::error(msg)` (`platform/log.h`). `push_error` is `Log::error`. String
formatting `"%d" % x` becomes concatenation with `gd::str`.

#### Not yours

Do not edit files outside your assignment, `src/gd/*`, `RULES.md` or `CMakeLists.txt`. If the
runtime lacks something you need, write it locally in your `.cpp` and report it. Do not build
the project; check your files with
`g++ -std=c++20 -fwrapv -fsyntax-only -Isrc <file.cpp>` once the headers they
include exist.
