# cpp_result

A single-header `Result<T, E>` for C++20 — Rust's shape, C++'s cost model.

- **No hidden tag byte where one isn't needed.** `Result<Status, void>` is one byte; `Result<int *, void>` is eight. The
  discriminator is packed into a representation the payload cannot produce.
- **`void` and lvalue references on either side.** `Result<void, E>`, `Result<T, void>`, `Result<T &, E>`,
  `Result<T, E &>`, `Result<void, void>`.
- **Works at compile time**, and in freestanding builds with exceptions off.
- **Error conversions must be written down.** Nothing widens, narrows, or promotes an error type behind your back.

Requires GCC or Clang (the niche packing relies on their union and `__PRETTY_FUNCTION__` behaviour).

## Install

```cmake
include(FetchContent)
FetchContent_Declare(result
        GIT_REPOSITORY https://github.com/LuisRuisinger/cpp_result.git
        GIT_TAG        main)     # or a release tag
FetchContent_MakeAvailable(result)
target_link_libraries(your_target PRIVATE lsr::result)
```

Or just drop `include/result/result.hpp` in your tree.

## Example

```cpp
#define RESULT_DEFINE_SHORT_TRY          // opt in to the bare `TRY` spelling
#include <result/result.hpp>

using lsr::Err, lsr::Ok, lsr::Result;

enum class BusError    : std::uint8_t { Timeout, Parity, NoDevice };
enum class DriverError : std::uint8_t { Bus, WrongDevice };

// Crossing an error boundary is spelled out once, here. Without this, no TRY in the
// driver layer compiles across it.
RESULT_ERROR_CONVERSION(DriverError, BusError, { return DriverError::Bus; });

Result<std::uint32_t, BusError> read_reg(const Device &, std::uint16_t addr);
Result<void, BusError>          write_reg(Device &, std::uint16_t addr, std::uint32_t);

Result<Caps, DriverError> probe(Device &dev)
{
    TRY(const std::uint32_t id, read_reg(dev, REG_ID));   // declaration form: binds
    if (id != DEVICE_ID)
        return Err(DriverError::WrongDevice);

    const std::uint32_t caps = TRY(read_reg(dev, REG_CAPS));  // expression form: yields
    TRY(write_reg(dev, REG_CTRL, CTRL_ENABLE));               // statement form:  discards

    return Ok(Caps{caps});
}
```

Or as a pipeline, when the steps genuinely chain:

```cpp
Result<bool, DriverError> dma_available(Device &dev)
{
    return read_reg(dev, REG_CAPS)
        .map_err([](BusError) { return DriverError::Bus; })
        .and_then([](std::uint32_t caps) -> Result<bool, DriverError> {
            if (caps == 0U)
                return Err(DriverError::WrongDevice);
            return Ok((caps & CAP_DMA) != 0U);
        });
}
```

## How the layout works

With one side `void` there is only one bit of state to record, so it is hidden in a value the payload cannot hold — a
spare enumerator, a non-canonical pointer, a signalling NaN, `nullptr` for a reference. When neither side is `void`
there are two live payloads and no room, so a `union` plus an explicit `bool` is used.

| type                                 | bytes | payload |
|--------------------------------------|-------|---------|
| `Result<Status, void>` (1-byte enum) | 1     | 1       |
| `Result<bool, void>`                 | 1     | 1       |
| `Result<double, void>`               | 8     | 8       |
| `Result<int *, void>`                | 8     | 8       |
| `Result<int &, void>`                | 8     | 8       |
| `Result<void, Status>`               | 1     | 1       |
| `Result<void, void>`                 | 1     | —       |
| `Result<Plain, void>` (no niche)     | 8     | 4       |
| `Result<std::uint32_t, Status>`      | 8     | —       |

Niches are found automatically for `bool`, pointers, `float`, `double`, and scoped enums (by searching for an unnamed
enumerator value). Everything else spends a flag byte. Detection never fails loudly — it falls back — so if you depend
on the packed layout, assert it:

```cpp
static_assert(lsr::use_automatic_sentinel<MyStatus>);
```

Two consequences worth knowing:

- **A packed payload has one forbidden value.** Storing it throws `lsr::bad_sentinel_value` (or traps, with exceptions
  off). Opt a type out with `template <> struct lsr::niche_opt_out<T> : std::true_type {};`, or globally with
  `RESULT_DISABLE_NICHE`.
- **Pointer and `bool` payloads are not usable in constant evaluation** when packed, because `std::bit_cast` on a
  pointer isn't a constant expression and not every bit pattern is a valid `bool`. Opting out restores constexpr at the
  cost of the byte. Reference payloads are unaffected — `nullptr` is a real value, not a bit pattern.

To teach `Result` a niche for a scalar type of your own, specialize `lsr::automatic_sentinel<T>`.

## What that costs in codegen

Measured with GCC 13 at `-O2` on x86-64, `-fno-exceptions`. The listings below are abridged — prologue
and the cold error path are cut. These are observations, not guarantees, but they are the shape to
expect.

**A packed Result travels in a register, branchlessly.** `Result<Status, void>` is one byte, so it
comes back in `al`:

```asm
;; return ok ? Ok(Status::Busy) : Err();
test    dil, dil
sete    al
add     eax, eax        ; 0 = Ok(Busy), 2 = the reserved Err value
ret
```

The two-payload form cannot do that. `Result<uint32_t, Status>` has to assemble `union + bool` in
memory and load it back, which is where its extra instructions come from:

```asm
mov     BYTE PTR  -4[rsp], 1
mov     DWORD PTR -8[rsp], 7
mov     rax, QWORD PTR -8[rsp]
```

**Testing the branch is one compare.** `is_ok()` on a packed Result is `cmp dil, 2` + `setne al`.

**A constant payload folds completely.** `return Ok(Status::Busy);` becomes `xor eax, eax; ret` — the
reserved-value check disappears along with everything else.

**The cost you do pay is that check, when the value is not a constant.** Packing has to reject a
payload that collides with the reserved representation, so construction from a runtime value keeps a
compare and a cold branch to the panic path:

| payload | guard emitted per construction |
|---------|--------------------------------|
| 1-byte enum, `bool` | `cmp dil, 2` + cold branch |
| pointer | `movabs rdx, 0x7fff'ffff'ffff'ffff` + `cmp` + cold branch |

That is the whole bill for the byte you saved. `niche_opt_out<T>` or `RESULT_DISABLE_NICHE` removes
it, at the cost of the byte. The unpacked two-payload form never has it, because a `bool`
discriminator cannot collide with anything.

**Pipelines keep the branch structure of hand-written checks.** A two-step `RESULT_TRY` chain and the
equivalent hand-rolled `if`-ladder produce the same branching — one null test, both loads, no
redundant re-check — and the optimizer folds the intermediate Results away. What is left over is the
return-value packing above, not extra logic. `RESULT_TRY` came out two instructions tighter than the
same thing written as `and_then` + `map`.

**Against `std::expected`,** where the niche applies:

| | this | `std::expected` |
|---|---|---|
| `Status` + no error | **1** | 2 |
| `int *` + no error | **8** | 16 |
| `void` + `Status` | **1** | 2 |
| `uint32_t` + `Status` | 8 | 8 |

The `int *` row is the one that matters most: 16 bytes stops fitting in a single register. Where
neither side can be packed, the two are the same size and the same code.

## API

**State** — `is_ok`, `is_err`, `is_ok_and`, `is_err_and`

**Borrowing** (lvalue only, so they cannot dangle) — `unwrap_ref`, `unwrap_err_ref`

**Consuming** (`&&`-qualified) — `unwrap`, `unwrap_err`, `unwrap_unchecked`, `unwrap_err_unchecked`, `unwrap_or`,
`unwrap_or_else`, `unwrap_or_default`, `unwrap_or_throw`, `expect`, `expect_err`

**Combinators** (`&&`-qualified) — `map`, `map_err`, `map_or`, `map_or_else`, `and_then`, `or_else`, `inspect`,
`inspect_err`, `flatten`, `transpose`

**Comparison** — `==` / `!=` against another `Result`, or against `Ok(x)` / `Err(e)`

Consuming methods require an rvalue, so name the `Result` and move it:

```cpp
auto value = std::move(result).unwrap();
```

`unwrap_ref()` on a temporary is a compile error rather than a dangling reference. `is_ok_and` / `is_err_and` are the
only combinators that borrow instead of consuming.

## Propagating a branch never converts it

`and_then` and `or_else` forward the branch they did not touch. That payload was never named at the call site, so it is
moved across unchanged — the only adjustments allowed are the identity and walking a pointer or reference to a base:

```cpp
// rejected: propagation would silently narrow int64_t -> int8_t
std::move(r).and_then([](int v) { return Result<double, std::int8_t>(Ok(v * 1.0)); });

// say it instead
std::move(r).map_err([](std::int64_t e) { return narrow(e); }).and_then(/* ... */);
```

This is the same rule `RESULT_TRY` enforces via `error_from`, applied consistently. It also rules out a class of
dangling: propagating a `std::string` error into a `std::string_view` error would have bound the view to a temporary.

## RESULT_TRY

Evaluates a fallible expression; on error, returns from the **enclosing** function with that error converted through
`error_from`. Three forms:

```cpp
RESULT_TRY(const auto v, read(addr));   // declaration: binds v
const int v = RESULT_TRY(read(addr));   // expression:  yields the payload
RESULT_TRY(enable(dev));                // statement:   discards it
```

Prefer the **declaration** form in `constexpr` code: the expression form is a statement expression, and GCC will not
constant-evaluate a return that jumps out of one, so at compile time it only works on the success path.

Macro arity is counted in commas, so a template argument list needs an extra pair of parentheses —
`RESULT_TRY((make<A, B>()))`. Three or more arguments are diagnosed with that advice; two are indistinguishable from the
declaration form.

`RESULT_DEFINE_SHORT_TRY` also defines `TRY`. Off by default, since `TRY` is a popular name.

## Freestanding

Nothing to configure — the header detects `__STDC_HOSTED__` and whether exceptions are enabled, and uses its own
`memcpy`, `construct_at`, `index_sequence`, `string_view`, and so on when hosted facilities are absent.
`unwrap_or_throw` exists only in hosted builds with exceptions.

Point the failure hooks at your own panic path if you have one:

```cpp
#define RESULT_ERROR(msg)   my_panic(msg)
#define RESULT_ASSERT(cond) my_assert(cond)
#include <result/result.hpp>
```

They default to a `stderr` report plus `std::terminate`, or `__builtin_trap()` when freestanding.

**Known limitation:** with `__STDC_HOSTED__ == 0` the fallback `construct_at` uses placement `new`, which is never a
constant expression, so `Result` works at run time but not at compile time in freestanding builds.

## Configuration

| macro                                       | effect                                                                       |
|---------------------------------------------|------------------------------------------------------------------------------|
| `RESULT_NAMESPACE`                          | namespace to define the library in (default `lsr`); stays defined afterwards |
| `RESULT_ERROR(msg)` / `RESULT_ASSERT(cond)` | override the failure hooks                                                   |
| `RESULT_DISABLE_NICHE`                      | never pack; always spend a flag byte                                         |
| `RESULT_DEFINE_SHORT_TRY`                   | also define `TRY`                                                            |
| `RESULT_ENUM_SENTINEL_PROBE_SEQUENCE`       | hashed niche candidates per enum (default 8)                                 |
| `RESULT_SMALL_ENUM_SENTINEL_PROBE_SEQUENCE` | exhaustive candidates for one-byte enums (default 256)                       |

Per-type escape hatch: `template <> struct lsr::niche_opt_out<T> : std::true_type {};`

## Not supported

- `Result<const T, E>` and `Result<volatile T, E>` — the payload could not be moved out. Qualify the `Result` itself, or
  borrow with `Result<const T &, E>`.
- `Result<T &&, E>`.
- Niche packing when neither side is `void`.
- Destructive move. C++ moves leave the source alive, so a `Result` you moved from still reports `is_ok()` and hands
  back a hollowed-out payload. `.clang-tidy` here enables `bugprone-use-after-move`, which catches the local cases.

## Tests

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

- `layout_tests` pins the sizes above.
- `behavior_tests` covers semantics, constant evaluation, and a payload-move budget.
- `reject_tests` asserts that a list of mistakes **fails to compile** — a case that quietly starts compiling is a
  reopened hole.
- `freestanding_tests` is compiled, never linked, in both `-ffreestanding` and `-fno-exceptions` configurations.

CI runs all of it on GCC and Clang, C++20 and C++23, with and without sanitizers. The two-compiler matrix is not
ceremony: niche packing depends on reading the inactive member of a union and on the shape of `__PRETTY_FUNCTION__`,
neither of which is standard C++, and both fail *silently* by falling back to a flag byte. `layout_tests` is the
tripwire.

## License

MIT. See [LICENSE](LICENSE).
