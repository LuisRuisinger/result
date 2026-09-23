// Compiled, not linked. This exists so the freestanding path keeps building: under
// -ffreestanding __STDC_HOSTED__ is 0, which selects the header's own memcpy, construct_at,
// index_sequence, string_view, invoke and swap instead of the hosted ones, and with
// -fno-exceptions it also removes unwrap_or_throw and the exception types.
//
// Note the absence of constexpr construction below. With __STDC_HOSTED__ == 0 the fallback
// construct_at uses placement new, which is never a constant expression, so Result works at
// run time but not at compile time in a freestanding build. sizeof is fine.

#include <cstdint>
#include <result/result.hpp>

namespace {

enum class Fault : std::uint8_t {
    Bus,
    Parity,
    Timeout
};
enum class Fatal : std::uint8_t {
    Unrecoverable
};

}  // namespace

RESULT_ERROR_CONVERSION(Fatal, Fault, { return Fatal::Unrecoverable; });

namespace {

using lsr::Err;
using lsr::Ok;
using lsr::Result;

// The packed layouts must survive the freestanding path unchanged.
static_assert(sizeof(Result<Fault, void>) == sizeof(Fault));
static_assert(sizeof(Result<void, Fault>) == sizeof(Fault));
static_assert(sizeof(Result<void, void>) == sizeof(bool));
static_assert(sizeof(Result<std::uint32_t *, void>) == sizeof(std::uint32_t *));
static_assert(sizeof(Result<std::uint32_t &, void>) == sizeof(std::uint32_t *));
static_assert(sizeof(Result<double, void>) == sizeof(double));

// The header's own utilities are the ones in play here.
static_assert(!lsr::use_automatic_sentinel<std::uint32_t>);
static_assert(lsr::use_automatic_sentinel<Fault>);

Result<std::uint32_t, Fault> read_word(const std::uint32_t *base, std::uint32_t index)
{
    if (base == nullptr)
        return Err(Fault::Bus);

    if (index >= 4U)
        return Err(Fault::Timeout);

    return Ok(base[index]);
}

Result<void, Fault> write_word(std::uint32_t *base, std::uint32_t index, std::uint32_t value)
{
    if (base == nullptr)
        return Err(Fault::Bus);

    base[index] = value;
    return Ok();
}

// RESULT_TRY across an error boundary, going through error_from.
Result<std::uint32_t, Fatal> accumulate(const std::uint32_t *base)
{
    RESULT_TRY(const std::uint32_t first, read_word(base, 0U));
    RESULT_TRY(const std::uint32_t second, read_word(base, 1U));
    return Ok(first + second);
}

// The statement form, and identity propagation of an untouched branch.
Result<void, Fault> configure(std::uint32_t *base)
{
    RESULT_TRY(write_word(base, 2U, 1U));
    return Ok();
}

}  // namespace

// Non-static so nothing is dropped as unused. Exercises every combinator on the freestanding
// path; the return value is only there to keep the calls from being elided.
std::uint32_t result_freestanding_smoke(std::uint32_t *base);

std::uint32_t result_freestanding_smoke(std::uint32_t *base)
{
    std::uint32_t observed = 0U;

    observed += read_word(base, 0U).map([](std::uint32_t v) { return v + 1U; }).unwrap_or(0U);
    observed +=
        read_word(base, 9U).map_err([](Fault) { return Fatal::Unrecoverable; }).unwrap_or(1U);
    observed += read_word(base, 0U).map_or([](std::uint32_t v) { return v; }, 0U);
    observed += read_word(base, 0U).map_or_else([](std::uint32_t v) { return v; },
                                                [](Fault) { return 0U; });
    observed += read_word(base, 0U)
                    .and_then([](std::uint32_t v) { return Result<std::uint32_t, Fault>(Ok(v)); })
                    .unwrap_or(0U);
    observed += read_word(base, 9U)
                    .or_else([](Fault) { return Result<std::uint32_t, Fault>(Ok(7U)); })
                    .unwrap_or(0U);
    observed += read_word(base, 0U).unwrap_or_else([](Fault) { return 3U; });
    observed += read_word(base, 0U)
                    .inspect([&observed](const std::uint32_t &v) { observed ^= v; })
                    .unwrap_or(0U);
    observed += read_word(base, 9U).inspect_err([](const Fault &) {}).unwrap_or(0U);
    observed += static_cast<std::uint32_t>(
        read_word(base, 0U).is_ok_and([](const std::uint32_t &v) { return v != 0U; }));
    observed += static_cast<std::uint32_t>(
        read_word(base, 9U).is_err_and([](const Fault &f) { return f == Fault::Timeout; }));
    observed += accumulate(base).unwrap_or(0U);
    observed += static_cast<std::uint32_t>(configure(base).is_ok());

    // Reference payload, and a borrowing accessor on a named Result.
    Result<std::uint32_t &, void> slot = Ok(base[3]);
    if (slot.is_ok())
        slot.unwrap_ref() += 1U;

    // flatten. Left as a prvalue on purpose: <utility> is not guaranteed freestanding in
    // C++20, so this file never reaches for std::move.
    observed += Result<Result<std::uint32_t, Fault>, Fault>(Ok(read_word(base, 0U)))
                    .flatten()
                    .unwrap_or(0U);

    return observed;
}
