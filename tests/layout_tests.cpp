#include <cstddef>
#include <cstdint>
#include <result/result.hpp>
#include <type_traits>

namespace {

namespace result = lsr;
namespace storage = result::detail::storage;

// -------------------------------------------------------------------------------------------------
// Test types
// -------------------------------------------------------------------------------------------------

struct no_niche_byte {
    std::uint8_t value;
};

struct alignas(8) no_niche_word {
    std::uint64_t value;
};

static_assert(sizeof(no_niche_byte) == 1U);
static_assert(alignof(no_niche_byte) == 1U);

static_assert(sizeof(no_niche_word) == 8U);
static_assert(alignof(no_niche_word) == 8U);

// -------------------------------------------------------------------------------------------------
// Layout helpers
// -------------------------------------------------------------------------------------------------

constexpr std::size_t align_up(std::size_t size, std::size_t alignment) noexcept
{
    return ((size + alignment - 1U) / alignment) * alignment;
}

template <typename T>
consteval std::size_t separate_optional_size()
{
    return align_up(sizeof(T) + sizeof(bool), alignof(T));
}

template <typename Left, typename Right>
consteval std::size_t union_with_bool_size()
{
    constexpr std::size_t union_alignment =
        alignof(Left) < alignof(Right) ? alignof(Right) : alignof(Left);

    constexpr std::size_t largest_member =
        sizeof(Left) < sizeof(Right) ? sizeof(Right) : sizeof(Left);

    constexpr std::size_t union_size = align_up(largest_member, union_alignment);

    return align_up(union_size + sizeof(bool), union_alignment);
}

// -------------------------------------------------------------------------------------------------
// optional<T>: compressed storage
// -------------------------------------------------------------------------------------------------

static_assert(storage::optional<bool>::USES_COMPRESSED_STORAGE);
static_assert(sizeof(storage::optional<bool>) == sizeof(bool));

static_assert(storage::optional<float>::USES_COMPRESSED_STORAGE);
static_assert(sizeof(storage::optional<float>) == sizeof(float));

static_assert(storage::optional<double>::USES_COMPRESSED_STORAGE);
static_assert(sizeof(storage::optional<double>) == sizeof(double));

static_assert(storage::optional<int *>::USES_COMPRESSED_STORAGE);
static_assert(sizeof(storage::optional<int *>) == sizeof(int *));

// -------------------------------------------------------------------------------------------------
// optional<T>: separate flag storage
// -------------------------------------------------------------------------------------------------

static_assert(!storage::optional<no_niche_byte>::USES_COMPRESSED_STORAGE);

static_assert(sizeof(storage::optional<no_niche_byte>) == separate_optional_size<no_niche_byte>());

static_assert(sizeof(storage::optional<no_niche_byte>) == 2U);

static_assert(!storage::optional<no_niche_word>::USES_COMPRESSED_STORAGE);

static_assert(sizeof(storage::optional<no_niche_word>) == separate_optional_size<no_niche_word>());

static_assert(sizeof(storage::optional<no_niche_word>) == 16U);
static_assert(alignof(storage::optional<no_niche_word>) == 8U);

// -------------------------------------------------------------------------------------------------
// Result<T, void>: optional-backed state
// -------------------------------------------------------------------------------------------------

static_assert(sizeof(result::Result<bool, void>) == sizeof(storage::optional<bool>));

static_assert(sizeof(result::Result<bool, void>) == sizeof(bool));

static_assert(sizeof(result::Result<int *, void>) == sizeof(storage::optional<int *>));

static_assert(sizeof(result::Result<int *, void>) == sizeof(int *));

static_assert(sizeof(result::Result<int &, void>) == sizeof(storage::optional<int *>));

static_assert(sizeof(result::Result<int &, void>) == sizeof(int *));

static_assert(sizeof(result::Result<no_niche_byte, void>) ==
              sizeof(storage::optional<no_niche_byte>));

static_assert(sizeof(result::Result<no_niche_byte, void>) == 2U);

static_assert(sizeof(result::Result<no_niche_word, void>) ==
              sizeof(storage::optional<no_niche_word>));

static_assert(sizeof(result::Result<no_niche_word, void>) == 16U);

// -------------------------------------------------------------------------------------------------
// Result<void, E>: optional-backed state
// -------------------------------------------------------------------------------------------------

static_assert(sizeof(result::Result<void, bool>) == sizeof(storage::optional<bool>));

static_assert(sizeof(result::Result<void, bool>) == sizeof(bool));

static_assert(sizeof(result::Result<void, int *>) == sizeof(storage::optional<int *>));

static_assert(sizeof(result::Result<void, int *>) == sizeof(int *));

static_assert(sizeof(result::Result<void, int &>) == sizeof(storage::optional<int *>));

static_assert(sizeof(result::Result<void, int &>) == sizeof(int *));

static_assert(sizeof(result::Result<void, no_niche_byte>) ==
              sizeof(storage::optional<no_niche_byte>));

static_assert(sizeof(result::Result<void, no_niche_byte>) == 2U);

// -------------------------------------------------------------------------------------------------
// Result<void, void>
// -------------------------------------------------------------------------------------------------

static_assert(sizeof(result::Result<void, void>) == sizeof(bool));
static_assert(alignof(result::Result<void, void>) == alignof(bool));

// -------------------------------------------------------------------------------------------------
// Result<T, E>: union plus a separate branch discriminator
// -------------------------------------------------------------------------------------------------

using byte_ok = result::wrapper::Ok<std::uint8_t>;
using byte_err = result::wrapper::Err<std::uint8_t>;

static_assert(sizeof(result::Result<std::uint8_t, std::uint8_t>) ==
              union_with_bool_size<byte_ok, byte_err>());

static_assert(sizeof(result::Result<std::uint8_t, std::uint8_t>) == 2U);

using word_ok = result::wrapper::Ok<std::uint64_t>;
using byte_error = result::wrapper::Err<std::uint8_t>;

static_assert(sizeof(result::Result<std::uint64_t, std::uint8_t>) ==
              union_with_bool_size<word_ok, byte_error>());

static_assert(sizeof(result::Result<std::uint64_t, std::uint8_t>) == 16U);

static_assert(alignof(result::Result<std::uint64_t, std::uint8_t>) == 8U);

// The primary two-sided Result currently always carries an explicit bool.
static_assert(sizeof(result::Result<bool, bool>) > sizeof(bool));

static_assert(sizeof(result::Result<bool, bool>) == 2U);

// References are represented internally as pointers, plus the branch flag.
using ref_ok = result::wrapper::Ok<int &>;
using ref_err = result::wrapper::Err<int &>;

static_assert(sizeof(result::Result<int &, int &>) == union_with_bool_size<ref_ok, ref_err>());

static_assert(sizeof(result::Result<int &, int &>) > sizeof(int *));

}  // namespace

int main()
{
    return 0;
}
