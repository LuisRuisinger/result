// SPDX-License-Identifier: MIT

#ifndef LRUSINGER_RESULT_INCLUDE_RESULT_RESULT_HPP_
#define LRUSINGER_RESULT_INCLUDE_RESULT_RESULT_HPP_

#if !defined(__clang__) && !defined(__GNUC__)
#    error "Result<T, E> requires GCC or Clang."
#endif

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <type_traits>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
#    include <cassert>
#    include <cstdio>
#    include <cstring>
#    include <functional>
#    include <memory>
#    include <optional>
#    include <stdexcept>
#endif

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ && \
    (defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND))
#    define RESULT_HAS_HOSTED_EXCEPTIONS 1
#else
#    define RESULT_HAS_HOSTED_EXCEPTIONS 0
#endif

#if !defined(__cplusplus) || __cplusplus < 202002L
#    error "Result<T, E> implementation requires C++20 or later."
#endif

// =================================================================================================
// Configuration
//
//   RESULT_NAMESPACE                          Namespace to define the library in. Defaults to
//                                             `lsr`. Stays defined after this header so generic
//                                             code can keep spelling RESULT_NAMESPACE::Result.
//   RESULT_ERROR(msg) / RESULT_ASSERT(cond)   Override the failure hooks. Both default to a
//                                             hosted report plus std::terminate, or
//                                             __builtin_trap() when freestanding.
//   RESULT_DISABLE_NICHE                      Never pack the discriminator into a payload's
//                                             unused representation; always spend a flag byte.
//   RESULT_ENUM_SENTINEL_PROBE_SEQUENCE       Hashed niche candidates tried per enum. Default 8.
//   RESULT_SMALL_ENUM_SENTINEL_PROBE_SEQUENCE Exhaustive candidates for one-byte enums. 256.
//
// Per-type escape hatch: specialize RESULT_NAMESPACE::niche_opt_out<T> as std::true_type to
// force flag-backed storage for a single payload type. Useful when a type legitimately
// produces the reserved representation - an enum built by casting a hardware status word, a
// pointer holding a poison value - and you would rather spend the flag byte than risk
// bad_sentinel_value at run time. To go the other way and teach Result a niche for a scalar
// type of your own, specialize RESULT_NAMESPACE::automatic_sentinel<T>.
//
// Constant evaluation: Result is a literal type whenever its payloads are, and niche-packed
// storage participates too. The exceptions are pointer and bool payloads: std::bit_cast on a
// pointer is not a constant expression, and not every bit pattern is a valid bool. Both still
// work at run time, and RESULT_DISABLE_NICHE / niche_opt_out makes them constexpr at the cost
// of a flag byte.
// =================================================================================================

// =================================================================================================
// Version
// =================================================================================================

#define CPP_RESULT_VERSION_MAJOR 2
#define CPP_RESULT_VERSION_MINOR 0
#define CPP_RESULT_VERSION_PATCH 1

#define CPP_RESULT_VERSION_STRING "2.0.1"

#define CPP_RESULT_VERSION_ENCODE(major__, minor__, patch__) \
    (((major__) * 10000) + ((minor__) * 100) + (patch__))

#define CPP_RESULT_VERSION                                                        \
    CPP_RESULT_VERSION_ENCODE(CPP_RESULT_VERSION_MAJOR, CPP_RESULT_VERSION_MINOR, \
                              CPP_RESULT_VERSION_PATCH)

// The library lives in RESULT_NAMESPACE. Define it before including this header to
// choose your own name; otherwise it defaults to `lsr`. RESULT_USE_NAMESPACE is kept
// as a spelling of "give me the default" for backwards compatibility.
#ifndef RESULT_NAMESPACE
#    define RESULT_NAMESPACE lsr
#endif

namespace RESULT_NAMESPACE {

template <typename T, typename E>
class Result;

#if RESULT_HAS_HOSTED_EXCEPTIONS
class bad_result_access : public std::exception {
public:
    [[nodiscard]] const char *what() const noexcept override
    {
        return "Bad Result access";
    }
};

class bad_optional_access : public std::exception {
public:
    [[nodiscard]] const char *what() const noexcept override
    {
        return "Storage optional has no value";
    }
};

/* Thrown when a payload's object representation collides with the niche a sentinel
   policy reserved for the empty state. See RESULT_DISABLE_NICHE / niche_opt_out. */
class bad_sentinel_value : public std::exception {
public:
    [[nodiscard]] const char *what() const noexcept override
    {
        return "Payload equals the reserved empty representation of its niche";
    }
};
#endif

namespace detail {

template <typename...>
inline constexpr bool result_always_false_v = false;

template <typename Message>
[[noreturn]] inline void panic(const Message &message) noexcept
{
#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
    (void)sizeof(message);
    __builtin_trap();
#else
    const char *text = nullptr;

    if constexpr (std::is_convertible_v<const Message &, const char *>) {
        text = static_cast<const char *>(message);
    } else if constexpr (requires { message.c_str(); }) {
        if constexpr (std::is_convertible_v<decltype(message.c_str()), const char *>)
            text = message.c_str();
    }

    if (text != nullptr) {
        std::fputs("Result error: ", stderr);
        std::fputs(text, stderr);
        std::fputc('\n', stderr);
    }

    std::terminate();
#endif
}

}  // namespace detail

#ifndef RESULT_ERROR
#    define RESULT_DETAIL_DEFINED_RESULT_ERROR 1
#    define RESULT_ERROR(_m)                   RESULT_NAMESPACE::detail::panic((_m))
#endif

#ifndef RESULT_ASSERT
#    define RESULT_DETAIL_DEFINED_RESULT_ASSERT 1
#    if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
#        define RESULT_ASSERT(_condition)             \
            do {                                      \
                if (!(_condition)) {                  \
                    RESULT_ERROR("assertion failed"); \
                }                                     \
            } while (0)
#    else
#        define RESULT_ASSERT(_condition) assert(_condition)
#    endif
#endif

namespace detail {

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename T>
[[nodiscard]] constexpr T *addressof(T &value) noexcept
{
    return __builtin_addressof(value);
}
#else
using std::addressof;
#endif

template <typename T>
constexpr std::remove_reference_t<T> &&move_value(T &&value) noexcept
{
    return static_cast<std::remove_reference_t<T> &&>(value);
}

template <typename T>
constexpr T &&forward_value(std::remove_reference_t<T> &value) noexcept
{
    return static_cast<T &&>(value);
}

template <typename T>
constexpr T &&forward_value(std::remove_reference_t<T> &&value) noexcept
{
    static_assert(!std::is_lvalue_reference_v<T>,
                  "forward cannot convert an rvalue into an lvalue");
    return static_cast<T &&>(value);
}

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename T>
std::add_rvalue_reference_t<T> declval() noexcept;
#else
using std::declval;
#endif

template <typename T, std::size_t N>
struct fixed_array {
    T elements[N == 0U ? 1U : N]{};

    [[nodiscard]] constexpr T *data() noexcept
    {
        return elements;
    }

    [[nodiscard]] constexpr const T *data() const noexcept
    {
        return elements;
    }

    [[nodiscard]] static constexpr std::size_t size() noexcept
    {
        return N;
    }

    [[nodiscard]] constexpr T &operator[](std::size_t index) noexcept
    {
        return elements[index];
    }

    [[nodiscard]] constexpr const T &operator[](std::size_t index) const noexcept
    {
        return elements[index];
    }

    [[nodiscard]] constexpr bool operator==(const fixed_array &other) const noexcept
    {
        for (std::size_t index = 0; index < N; ++index) {
            if (!(elements[index] == other.elements[index]))
                return false;
        }

        return true;
    }

    [[nodiscard]] constexpr bool operator!=(const fixed_array &other) const noexcept
    {
        return !(*this == other);
    }
};

using byte = unsigned char;

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <std::size_t... Is>
struct index_sequence {
    using type = index_sequence;
};

template <std::size_t N, std::size_t... Is>
struct make_index_sequence_impl : make_index_sequence_impl<N - 1U, N - 1U, Is...> {};

template <std::size_t... Is>
struct make_index_sequence_impl<0U, Is...> {
    using type = index_sequence<Is...>;
};

template <std::size_t N>
using make_index_sequence = typename make_index_sequence_impl<N>::type;
#else
using std::index_sequence;
using std::make_index_sequence;
#endif

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
inline void *memcpy(void *destination, const void *source, std::size_t size) noexcept
{
#    if __has_builtin(__builtin_memcpy)
    return __builtin_memcpy(destination, source, size);
#    else
#        error "__builtin_memcpy not available!"
#    endif
}
#else
using std::memcpy;
#endif

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename T, typename... Args>
    requires std::constructible_from<T, Args...>
constexpr T *construct_at(T *location,
                          Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return ::new (static_cast<void *>(location)) T(forward_value<Args>(args)...);
}
#else
using std::construct_at;
#endif

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename T>
constexpr void destroy_at(T *location) noexcept
{
    location->~T();
}
#else
using std::destroy_at;
#endif

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename>
struct member_pointer_class;

template <typename Class, typename Member>
struct member_pointer_class<Member Class::*> {
    using type = Class;
};

template <typename MemberPtr, typename Obj, typename... Args>
constexpr decltype(auto) invoke_member_function(MemberPtr pointer, Obj &&object, Args &&...args)
{
    using class_type = typename member_pointer_class<MemberPtr>::type;

    if constexpr (std::is_base_of_v<class_type, std::remove_reference_t<Obj>>) {
        return (forward_value<Obj>(object).*pointer)(forward_value<Args>(args)...);
    } else {
        return ((*forward_value<Obj>(object)).*pointer)(forward_value<Args>(args)...);
    }
}

template <typename MemberPtr, typename Obj>
constexpr decltype(auto) invoke_member_object(MemberPtr pointer, Obj &&object)
{
    using class_type = typename member_pointer_class<MemberPtr>::type;

    if constexpr (std::is_base_of_v<class_type, std::remove_reference_t<Obj>>) {
        return forward_value<Obj>(object).*pointer;
    } else {
        return (*forward_value<Obj>(object)).*pointer;
    }
}
#endif

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename Fn, typename... Args>
constexpr decltype(auto) invoke(Fn &&fn, Args &&...args)
{
    using callable = std::remove_cvref_t<Fn>;

    if constexpr (std::is_member_function_pointer_v<callable>) {
        return invoke_member_function(forward_value<Fn>(fn), forward_value<Args>(args)...);
    } else if constexpr (std::is_member_object_pointer_v<callable>) {
        return invoke_member_object(forward_value<Fn>(fn), forward_value<Args>(args)...);
    } else {
        return forward_value<Fn>(fn)(forward_value<Args>(args)...);
    }
}
#else
using std::invoke;
#endif

struct string_view {
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    const char *m_data{};
    std::size_t m_size{};

    constexpr string_view() noexcept = default;

    constexpr string_view(const char *text, std::size_t size) noexcept
        : m_data(text),
          m_size(size)
    {
    }

    template <std::size_t N>
    constexpr string_view(const char (&text)[N]) noexcept
        : m_data(text),
          m_size(N - 1U)
    {
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return m_size == 0U;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] constexpr const char *begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr const char *end() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr char operator[](std::size_t index) const noexcept
    {
        return m_data[index];
    }

    [[nodiscard]] constexpr string_view substr(std::size_t position,
                                               std::size_t count = npos) const noexcept
    {
        if (position > m_size)
            return {};

        const std::size_t available = m_size - position;
        const std::size_t length = count < available ? count : available;
        return {m_data + position, length};
    }

    [[nodiscard]] constexpr std::size_t find(char needle, std::size_t position = 0U) const noexcept
    {
        for (std::size_t index = position; index < m_size; ++index) {
            if (m_data[index] == needle)
                return index;
        }

        return npos;
    }

    [[nodiscard]] constexpr std::size_t find(string_view needle,
                                             std::size_t position = 0U) const noexcept
    {
        if (needle.m_size == 0U)
            return position <= m_size ? position : npos;

        if (needle.m_size > m_size || position > m_size - needle.m_size)
            return npos;

        for (std::size_t index = position; index <= m_size - needle.m_size; ++index) {
            bool match = true;
            for (std::size_t offset = 0; offset < needle.m_size; ++offset) {
                if (m_data[index + offset] != needle.m_data[offset]) {
                    match = false;
                    break;
                }
            }

            if (match)
                return index;
        }

        return npos;
    }

    [[nodiscard]] constexpr std::size_t rfind(char needle) const noexcept
    {
        for (std::size_t index = m_size; index > 0U; --index) {
            if (m_data[index - 1U] == needle)
                return index - 1U;
        }

        return npos;
    }

    [[nodiscard]] constexpr std::size_t rfind(string_view needle) const noexcept
    {
        if (needle.m_size == 0U)
            return m_size;

        if (needle.m_size > m_size)
            return npos;

        for (std::size_t index = m_size - needle.m_size + 1U; index > 0U; --index) {
            const std::size_t start = index - 1U;
            bool              match = true;
            for (std::size_t offset = 0; offset < needle.m_size; ++offset) {
                if (m_data[start + offset] != needle.m_data[offset]) {
                    match = false;
                    break;
                }
            }

            if (match)
                return start;
        }

        return npos;
    }
};

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
struct nullopt_t {
    explicit constexpr nullopt_t(int) noexcept
    {
    }
};

inline constexpr nullopt_t nullopt{0};

struct in_place_t {
    explicit constexpr in_place_t() noexcept = default;
};

inline constexpr in_place_t in_place{};

#else
using std::nullopt_t;
inline constexpr auto nullopt = std::nullopt;

using std::in_place_t;
inline constexpr auto in_place = std::in_place;

#endif

[[noreturn]] inline void optional_empty_access()
{
#if RESULT_HAS_HOSTED_EXCEPTIONS
    throw bad_optional_access{};
#else
    RESULT_ERROR("optional has no value");
#endif
}

[[noreturn]] inline void bad_sentinel_value_access()
{
#if RESULT_HAS_HOSTED_EXCEPTIONS
    throw bad_sentinel_value{};
#else
    RESULT_ERROR("reserved empty representation");
#endif
}

template <typename T>
using unqualified_t = std::remove_cv_t<T>;

// -------------------------------------------------------------------------------------------
// Enum reflection, used to find a value no enumerator of a scoped enum names. This is generic
// compile-time machinery rather than storage machinery, so it lives here, where the niche
// configuration below can reach it without spelling a nested detail namespace.
// -------------------------------------------------------------------------------------------

constexpr bool contains(string_view text, string_view token) noexcept
{
    return text.find(token) != string_view::npos;
}

constexpr string_view enum_argument_fragment(string_view signature) noexcept
{
    constexpr string_view marker = "Value = ";
    const std::size_t     marker_position = signature.find(marker);

    if (marker_position == string_view::npos)
        return {};

    const std::size_t begin = marker_position + marker.size();
    std::size_t       end = signature.size();

    constexpr char delimiters[] = {';', ',', ']'};
    for (const auto del : delimiters) {
        const std::size_t position = signature.find(del, begin);
        if (position != string_view::npos && position < end)
            end = position;
    }

    return signature.substr(begin, end - begin);
}

template <typename E, E Value>
constexpr string_view enum_value_signature()
{
    return __PRETTY_FUNCTION__;
}

template <typename E, E Value>
constexpr bool is_named_enum_value()
{
    static_assert(std::is_enum_v<E>);

    constexpr string_view fragment = enum_argument_fragment(enum_value_signature<E, Value>());

    if (fragment.empty())
        return false;

    const std::size_t last_scope = fragment.rfind("::");
    const std::size_t last_close_paren = fragment.rfind(')');

    return last_scope != string_view::npos &&
           (last_close_paren == string_view::npos || last_scope > last_close_paren) &&
           !contains(fragment, "{") && !contains(fragment, "static_cast");
}

template <typename T>
constexpr string_view type_signature()
{
    return __PRETTY_FUNCTION__;
}

constexpr std::uint64_t fnv1a_64(string_view text) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;

    for (const auto c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }

    return hash;
}

constexpr std::uint64_t splitmix64(std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;

    return value ^ (value >> 31U);
}

template <typename T>
inline constexpr std::uint64_t enum_type_hash = fnv1a_64(type_signature<T>());

template <typename T, std::size_t I>
inline constexpr std::underlying_type_t<T> hashed_enum_candidate =
    static_cast<std::underlying_type_t<T>>(splitmix64(enum_type_hash<T> + I));

template <typename Underlying>
struct enum_sentinel_search_result {
    bool       found;
    Underlying value;
};

template <typename T, std::size_t... Is>
constexpr auto find_hashed_enum_sentinel(index_sequence<Is...>)
{
    using underlying_type = std::underlying_type_t<T>;

    constexpr fixed_array<underlying_type, sizeof...(Is)> candidates = {
        hashed_enum_candidate<T, Is>...};
    constexpr fixed_array<bool, sizeof...(Is)> named = {
        is_named_enum_value<T, static_cast<T>(hashed_enum_candidate<T, Is>)>()...};

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!named[i])
            return enum_sentinel_search_result<underlying_type>{true, candidates[i]};
    }

    return enum_sentinel_search_result<underlying_type>{false, {}};
}

template <typename T, std::size_t... Is>
constexpr auto find_small_enum_sentinel(index_sequence<Is...>)
{
    using underlying_type = std::underlying_type_t<T>;

    constexpr int first = std::is_signed_v<underlying_type>
                              ? static_cast<int>(std::numeric_limits<underlying_type>::min())
                              : 0;

    constexpr fixed_array<underlying_type, sizeof...(Is)> candidates = {
        static_cast<underlying_type>(first + static_cast<int>(Is))...};
    constexpr fixed_array<bool, sizeof...(Is)> named = {is_named_enum_value<
        T, static_cast<T>(static_cast<underlying_type>(first + static_cast<int>(Is)))>()...};

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!named[index])
            return enum_sentinel_search_result<underlying_type>{true, candidates[index]};
    }

    return enum_sentinel_search_result<underlying_type>{false, {}};
}

// Each probe costs one __PRETTY_FUNCTION__ instantiation plus a constexpr parse of it,
// and the cost is paid once per enum per translation unit. A hashed candidate collides
// with a named enumerator only by accident, so the first probe essentially always wins;
// 8 is a generous budget. Raise it only if you hit the "no niche found" fallback.
#ifndef RESULT_ENUM_SENTINEL_PROBE_SEQUENCE
#    define RESULT_ENUM_SENTINEL_PROBE_SEQUENCE 8
#endif

// The exhaustive search used for one-byte enums must be able to cover the whole range.
#ifndef RESULT_SMALL_ENUM_SENTINEL_PROBE_SEQUENCE
#    define RESULT_SMALL_ENUM_SENTINEL_PROBE_SEQUENCE 256
#endif

template <typename E>
constexpr auto find_automatic_enum_sentinel()
{
    static_assert(std::is_enum_v<E>);

    using underlying_type = std::underlying_type_t<E>;

    if constexpr (sizeof(underlying_type) == 1U) {
        // Exhaustive for an 8-bit underlying type: 256 candidates covers every value.
        static_assert(RESULT_SMALL_ENUM_SENTINEL_PROBE_SEQUENCE <= 256,
                      "A one-byte enum has at most 256 candidate values.");
        return find_small_enum_sentinel<E>(
            make_index_sequence<RESULT_SMALL_ENUM_SENTINEL_PROBE_SEQUENCE>{});
    } else {
        // A bounded search for wider enums. Failure is not an error: the storage
        // selector simply falls back to an explicit discriminator.
        constexpr std::size_t probe_count = RESULT_ENUM_SENTINEL_PROBE_SEQUENCE;
        return find_hashed_enum_sentinel<E>(make_index_sequence<probe_count>{});
    }
}

template <typename E, bool = std::is_enum_v<E>>
struct is_scoped_enum : std::false_type {};

template <typename E>
struct is_scoped_enum<E, true>
    : std::bool_constant<!std::is_convertible_v<E, std::underlying_type_t<E>>> {};

template <typename E, bool = is_scoped_enum<E>::value>
struct automatic_enum_sentinel_info {
    // Unscoped enums without a fixed underlying type can reject otherwise representable
    // underlying values during constant evaluation. C++20 has no portable trait that
    // distinguishes those from unscoped enums with an explicitly fixed underlying type,
    // so unscoped enums conservatively use flag-backed storage.
    static constexpr bool found = false;
};

template <typename E>
struct automatic_enum_sentinel_info<E, true> {
    static constexpr auto                      search = find_automatic_enum_sentinel<E>();
    static constexpr bool                      found = search.found;
    static constexpr std::underlying_type_t<E> value = search.value;
};

}  // namespace detail

// =================================================================================================
// Wrapper types
// =================================================================================================

namespace wrapper {

template <typename T>
struct Ok {
    using value_type = T;

    explicit constexpr Ok(const T &input)
        : value(input)
    {
    }

    explicit constexpr Ok(T &&input)
        : value(detail::move_value(input))
    {
    }

    T value;
};

template <typename T>
struct Ok<T &> {
    using value_type = T &;

    explicit constexpr Ok(T &input) noexcept
        : value(detail::addressof(input))
    {
    }

    T *value;
};

template <>
struct Ok<void> {
    using value_type = void;
};

template <typename E>
struct Err {
    using value_type = E;

    explicit constexpr Err(const E &input)
        : value(input)
    {
    }

    explicit constexpr Err(E &&input)
        : value(detail::move_value(input))
    {
    }

    E value;
};

template <typename E>
struct Err<E &> {
    using value_type = E &;

    explicit constexpr Err(E &input) noexcept
        : value(detail::addressof(input))
    {
    }

    E *value;
};

template <>
struct Err<void> {
    using value_type = void;
};

}  // namespace wrapper

// Ok/Err capture lvalues as references and own rvalues.
template <typename T>
[[nodiscard]] constexpr auto Ok(T &&value)
{
    using U = std::conditional_t<std::is_lvalue_reference_v<T>, T, std::decay_t<T>>;
    return wrapper::Ok<U>(detail::forward_value<T>(value));
}

template <typename E>
[[nodiscard]] constexpr auto Err(E &&value)
{
    using U = std::conditional_t<std::is_lvalue_reference_v<E>, E, std::decay_t<E>>;
    return wrapper::Err<U>(detail::forward_value<E>(value));
}

[[nodiscard]] constexpr auto Ok() noexcept
{
    return wrapper::Ok<void>{};
}

[[nodiscard]] constexpr auto Err() noexcept
{
    return wrapper::Err<void>{};
}

// =================================================================================================
// Niche configuration
//
// The extension points a user is expected to reach for, deliberately outside detail:
// specialize automatic_sentinel to teach Result a niche for a scalar type of your own, or
// niche_opt_out to spend a flag byte instead for one payload type.
// =================================================================================================

template <auto Repr>
struct sentinel_bits {
    using representation_type = decltype(Repr);
    static constexpr representation_type value = Repr;
};

/* Selects ordinary flag-backed storage. */
struct separate_flag_policy {};

template <typename T, typename Enable = void>
struct automatic_sentinel;

template <>
struct automatic_sentinel<bool> : sentinel_bits<std::uint8_t{0xFE}> {
    static_assert(sizeof(bool) == sizeof(std::uint8_t),
                  "Automatic bool niche requires a one-byte bool representation");
};

template <typename T>
struct automatic_sentinel<T, std::enable_if_t<std::is_pointer_v<T>>> {
    using representation_type = std::uintptr_t;

#if defined(__x86_64__) || defined(_M_X64)
    static constexpr representation_type value = 0x7fff'ffff'ffff'ffffULL;
#elif defined(__i386__) || defined(_M_IX86)
    static constexpr representation_type value = 0xffff'fff7U;
#elif defined(__aarch64__) || defined(_M_ARM64)
    static constexpr representation_type value = 0x00ff'ffff'ffff'ffffULL;
#elif defined(__arm__) || defined(_M_ARM)
    static constexpr representation_type value = 0xffff'fff7U;
#elif defined(__riscv) && __riscv_xlen == 64
    static constexpr representation_type value = 0x7fff'ffff'ffff'ffffULL;
#elif defined(__riscv) && __riscv_xlen == 32
    static constexpr representation_type value = 0xffff'fff7U;
#else
    static_assert(always_false_v<T>,
                  "No automatic pointer sentinel is configured for this target ABI");
#endif

    static_assert(sizeof(representation_type) == sizeof(T));
};

template <>
struct automatic_sentinel<float> : sentinel_bits<std::uint32_t{0x7fed'cba9U}> {
    static_assert(std::numeric_limits<float>::is_iec559);
};

template <>
struct automatic_sentinel<double> : sentinel_bits<std::uint64_t{0x7ff8'fedc'ba98'7654ULL}> {
    static_assert(std::numeric_limits<double>::is_iec559);
};

template <typename E>
struct automatic_sentinel<E, std::enable_if_t<detail::automatic_enum_sentinel_info<E>::found>>
    : sentinel_bits<detail::automatic_enum_sentinel_info<E>::value> {
    static_assert(std::is_enum_v<E>);
    static_assert(!detail::is_named_enum_value<
                  E, static_cast<E>(detail::automatic_enum_sentinel_info<E>::value)>());
};

template <typename T>
concept has_automatic_sentinel = requires {
    typename automatic_sentinel<detail::unqualified_t<T>>::representation_type;
    automatic_sentinel<detail::unqualified_t<T>>::value;
};

template <typename Policy>
concept sentinel_policy = requires {
    typename Policy::representation_type;
    Policy::value;
};

/* Specialize this to true_type to force flag-backed storage for one payload type. Useful
   when a type legitimately produces the reserved representation - a pointer holding a
   poison value, a NaN with a hand-picked payload, an enum built by casting a hardware
   status word - and you would rather spend the flag byte than risk bad_sentinel_value.
   Defining RESULT_DISABLE_NICHE turns the automatic niche off for every type. */
template <typename T>
struct niche_opt_out : std::false_type {};

template <typename T>
inline constexpr bool use_automatic_sentinel =
#ifdef RESULT_DISABLE_NICHE
    false;
#else
    has_automatic_sentinel<T> && !niche_opt_out<detail::unqualified_t<T>>::value;
#endif

template <typename T, bool = use_automatic_sentinel<T>>
struct default_policy_selector {
    using type = separate_flag_policy;
};

template <typename T>
struct default_policy_selector<T, true> {
    using type = automatic_sentinel<detail::unqualified_t<T>>;
};

template <typename T>
using default_policy_t = default_policy_selector<T>::type;

// =================================================================================================
// Detail machinery
// =================================================================================================

namespace detail {

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename Expr>
using captured_type_t =
    std::conditional_t<std::is_lvalue_reference_v<Expr>, Expr, std::decay_t<Expr>>;

struct invalid_type {};
struct void_value {};

// =================================================================================================
// Result traits
// =================================================================================================

template <typename X>
struct result_traits {
    static constexpr bool is_result = false;
    using ok_type = invalid_type;
    using err_type = invalid_type;
};

template <typename T, typename E>
struct result_traits<Result<T, E>> {
    static constexpr bool is_result = true;
    using ok_type = T;
    using err_type = E;
};

template <typename T>
inline constexpr bool is_result_v = result_traits<remove_cvref_t<T>>::is_result;

template <typename T>
using result_ok_t = result_traits<remove_cvref_t<T>>::ok_type;

template <typename T>
using result_err_t = result_traits<remove_cvref_t<T>>::err_type;

// =================================================================================================
// Stored type resolution
// =================================================================================================

template <typename T>
struct stored_type {
    using type = T;
};

template <typename T>
struct stored_type<T &> {
    using type = T *;
};

template <typename T>
using stored_type_t = stored_type<T>::type;

// =================================================================================================
// Resolve plans
// =================================================================================================

template <typename From>
using source_expr_t =
    std::conditional_t<std::is_lvalue_reference_v<From>, From, std::remove_reference_t<From> &&>;

template <typename Ref>
using reference_pointer_t = std::add_pointer_t<std::remove_reference_t<Ref>>;

// Pointer convertibility admits cv qualification and derived-to-base binding,
// but rejects user-defined conversions that would materialize a temporary
// behind a stored reference.
template <typename To, typename From>
inline constexpr bool safe_reference_binding_v =
    std::is_lvalue_reference_v<To> && std::is_lvalue_reference_v<From> &&
    std::is_convertible_v<reference_pointer_t<From>, reference_pointer_t<To>>;

template <typename To, typename From, bool NonVoid = !std::is_void_v<To> && !std::is_void_v<From>>
struct resolve_plan_impl {
    static constexpr bool legal = std::is_void_v<To> && std::is_void_v<From>;
};

template <typename To, typename From>
struct resolve_plan_impl<To, From, true> {
    using target_type = To;
    using source_type = From;
    using source_expr = source_expr_t<From>;

    static constexpr bool binds_reference = safe_reference_binding_v<To, From>;
    static constexpr bool constructs_value =
        !std::is_reference_v<To> && std::is_constructible_v<To, source_expr>;
    static constexpr bool legal =
        !std::is_rvalue_reference_v<To> && (binds_reference || constructs_value);

    static constexpr decltype(auto) apply(source_expr source)
    {
        static_assert(legal, "Attempted to apply an illegal Result resolve plan.");

        if constexpr (binds_reference) {
            return static_cast<To>(source);
        } else {
            return To(forward_value<source_expr>(source));
        }
    }
};

template <typename To, typename From>
struct resolve_plan : resolve_plan_impl<To, From> {};

// Returns To, never decltype(auto). Callers hand the result straight outward, often through a
// decltype(auto) of their own, and what they resolved is frequently a local - so deducing here
// would let a reference to a dead local escape. Naming the type keeps the one move these paths
// need on the inside. (An earlier decltype(auto) here made map_or return a dangling reference:
// -O2 and ASan caught it, an -O0 test run did not.)
template <typename To, typename Expr>
constexpr To resolve_expression(Expr &&expression)
{
    using From = captured_type_t<Expr &&>;
    using Plan = resolve_plan<To, From>;

    static_assert(Plan::legal, "Expression cannot be safely resolved to the requested type.");

    return Plan::apply(forward_value<Expr>(expression));
}

// =================================================================================================
// Wrapper payload access and conversion
// =================================================================================================

template <typename T>
constexpr decltype(auto) take_payload(wrapper::Ok<T> &&ok)
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return static_cast<T>(*ok.value);
    } else {
        return move_value(ok.value);
    }
}

template <typename E>
constexpr decltype(auto) take_payload(wrapper::Err<E> &&err)
{
    if constexpr (std::is_lvalue_reference_v<E>) {
        return static_cast<E>(*err.value);
    } else {
        return move_value(err.value);
    }
}

template <typename T>
constexpr decltype(auto) payload_ref(const wrapper::Ok<T> &ok)
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return *ok.value;
    } else {
        return (ok.value);
    }
}

template <typename E>
constexpr decltype(auto) payload_ref(const wrapper::Err<E> &err)
{
    if constexpr (std::is_lvalue_reference_v<E>) {
        return *err.value;
    } else {
        return (err.value);
    }
}

// =================================================================================================
// Branch invocation plans
// =================================================================================================

template <bool Legal, typename Payload, typename Storage, typename Fn>
struct ok_invoke_plan_impl {
    static constexpr bool legal = false;
};

template <typename Payload, typename Storage, typename Fn>
struct ok_invoke_plan_impl<true, Payload, Storage, Fn> {
    static constexpr bool legal = true;

    using argument_type = decltype(declval<Storage &&>().take_ok());
    using result_type = std::invoke_result_t<Fn, argument_type>;

    static constexpr decltype(auto) apply(Storage &&storage, Fn fn)
    {
        return invoke(forward_value<Fn>(fn), move_value(storage).take_ok());
    }
};

template <typename Payload, typename Storage, typename Fn>
struct ok_invoke_plan
    : ok_invoke_plan_impl<std::is_invocable_v<Fn, decltype(declval<Storage &&>().take_ok())>,
                          Payload, Storage, Fn> {};

template <bool Legal, typename Storage, typename Fn>
struct ok_invoke_plan_impl<Legal, void, Storage, Fn> {
    static constexpr bool legal = false;
};

template <typename Storage, typename Fn>
struct ok_invoke_plan_impl<true, void, Storage, Fn> {
    static constexpr bool legal = true;

    using result_type = std::invoke_result_t<Fn>;

    static constexpr decltype(auto) apply(Storage &&, Fn fn)
    {
        return invoke(forward_value<Fn>(fn));
    }
};

template <typename Storage, typename Fn>
struct ok_invoke_plan<void, Storage, Fn>
    : ok_invoke_plan_impl<std::is_invocable_v<Fn>, void, Storage, Fn> {};

template <bool Legal, typename Payload, typename Storage, typename Fn>
struct err_invoke_plan_impl {
    static constexpr bool legal = false;
};

template <typename Payload, typename Storage, typename Fn>
struct err_invoke_plan_impl<true, Payload, Storage, Fn> {
    static constexpr bool legal = true;

    using argument_type = decltype(declval<Storage &&>().take_err());
    using result_type = std::invoke_result_t<Fn, argument_type>;

    static constexpr decltype(auto) apply(Storage &&storage, Fn fn)
    {
        return invoke(forward_value<Fn>(fn), move_value(storage).take_err());
    }
};

template <typename Payload, typename Storage, typename Fn>
struct err_invoke_plan
    : err_invoke_plan_impl<std::is_invocable_v<Fn, decltype(declval<Storage &&>().take_err())>,
                           Payload, Storage, Fn> {};

template <bool Legal, typename Storage, typename Fn>
struct err_invoke_plan_impl<Legal, void, Storage, Fn> {
    static constexpr bool legal = false;
};

template <typename Storage, typename Fn>
struct err_invoke_plan_impl<true, void, Storage, Fn> {
    static constexpr bool legal = true;

    using result_type = std::invoke_result_t<Fn>;

    static constexpr decltype(auto) apply(Storage &&, Fn fn)
    {
        return invoke(forward_value<Fn>(fn));
    }
};

template <typename Storage, typename Fn>
struct err_invoke_plan<void, Storage, Fn>
    : err_invoke_plan_impl<std::is_invocable_v<Fn>, void, Storage, Fn> {};

// What a borrowing callback is handed for each branch. void payloads yield void_value, which
// simply fails every is_invocable_v test, so the void case is dispatched separately.
template <typename Storage>
using const_ok_ref_t = decltype(declval<const Storage &>().ok_ref());

template <typename Storage>
using const_err_ref_t = decltype(declval<const Storage &>().err_ref());

// A reference returned from mapping an owned payload may refer into the
// consumed source Result. Preserve callback lvalue references only when the
// source payload itself is borrowed.
template <typename RawReturn, typename SourcePayload>
using map_output_t =
    std::conditional_t<std::is_void_v<RawReturn>, void,
                       std::conditional_t<std::is_lvalue_reference_v<RawReturn> &&
                                              std::is_lvalue_reference_v<SourcePayload>,
                                          RawReturn, std::decay_t<RawReturn>>>;

// =================================================================================================
// Branch propagation
//
// A combinator forwards the branch it did not touch. The payload crossing that boundary was
// never named at the call site, so - exactly like RESULT_TRY - propagation must not convert
// it. `error_from` exists because an implicit conversion here would let a payload widen,
// narrow, change signedness, or decay through an unscoped enum's integral promotion with no
// warning even under -Wconversion; and_then and or_else are propagation sites too, so they
// obey the same rule.
//
// What remains allowed is the set of adjustments that cannot lose information and cannot
// materialize an object: the identity, and walking a pointer or reference to a base or to a
// more qualified type. Pointer convertibility between two pointer types is only ever a
// standard conversion, never a user-defined one, which is what keeps a temporary from being
// bound behind a stored reference or view.
//
// Anything else is a deliberate change of payload type, so it is spelled deliberately with
// map / map_err.
// =================================================================================================

template <typename To, typename From>
inline constexpr bool lossless_pointer_conversion_v =
    std::is_pointer_v<To> && std::is_pointer_v<From> && std::is_convertible_v<From, To>;

template <typename Src, typename Tgt>
inline constexpr bool branch_propagates_v =
    std::is_same_v<Src, Tgt> || safe_reference_binding_v<Tgt, Src> ||
    lossless_pointer_conversion_v<Tgt, Src>;

// =================================================================================================
// Common value output for map_or_else
// =================================================================================================

template <typename A, typename B, typename = void>
struct common_value_plan {
    static constexpr bool legal = false;
    using type = invalid_type;
};

template <typename A, typename B>
struct common_value_plan<A, B, std::void_t<std::common_type_t<std::decay_t<A>, std::decay_t<B>>>> {
    static constexpr bool legal = !std::is_void_v<A> && !std::is_void_v<B>;
    using type = std::common_type_t<std::decay_t<A>, std::decay_t<B>>;
};

template <>
struct common_value_plan<void, void, void> {
    static constexpr bool legal = true;
    using type = void;
};

// =================================================================================================
// Optional storage
// =================================================================================================

namespace storage {

namespace detail {

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename T>
constexpr void swap(T &lhs, T &rhs) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                             std::is_nothrow_move_assignable_v<T>)
{
    T temporary(move_value(lhs));
    lhs = move_value(rhs);
    rhs = move_value(temporary);
}
#else
using std::swap;
#endif

template <typename Policy, bool = sentinel_policy<Policy>>
struct sentinel_metadata {};

template <typename Policy>
struct sentinel_metadata<Policy, true> {
    using sentinel_representation_type = Policy::representation_type;
    static constexpr sentinel_representation_type sentinel_representation = Policy::value;
};

// -------------------------------------------------------------------------------------------
// niche_cell holds exactly sizeof(T) bytes containing either a live T or the reserved
// representation that marks the empty state. Two strategies are selected automatically:
//
//   * bit_cast cell - stores a plain T and reinterprets it with std::bit_cast. Usable in
//     constant evaluation. Requires that every bit pattern of the representation denotes a
//     valid T, which excludes bool (only 0 and 1 are valid values), and requires bit_cast
//     itself to be a constant expression, which excludes pointers.
//
//   * union cell - stores a union of T and the representation. Works for every scalar, but
//     reading the inactive member is a GCC/Clang guarantee rather than a standard one, so it
//     is unavailable during constant evaluation.
//
// Both are exactly sizeof(T) and neither is a character array, so neither causes
// -fstack-protector-strong to insert a canary into every frame holding one.
// -------------------------------------------------------------------------------------------

template <typename T, typename Repr>
inline constexpr bool niche_cell_supports_constant_evaluation =
    std::is_trivially_copyable_v<T> && sizeof(T) == sizeof(Repr) && !std::is_pointer_v<T> &&
    !std::is_member_pointer_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>;

template <typename T, typename Repr, Repr Empty,
          bool = niche_cell_supports_constant_evaluation<T, Repr>>
class niche_cell {
    union {
        Repr m_repr;
        T    m_value;
    };

public:
    static constexpr bool SUPPORTS_CONSTANT_EVALUATION = false;

    constexpr niche_cell() noexcept
        : m_repr(Empty)
    {
    }

    [[nodiscard]] constexpr bool engaged() const noexcept
    {
        return m_repr != Empty;
    }

    constexpr void disengage() noexcept
    {
        m_repr = Empty;
    }

    // T is scalar, hence trivially default constructible, so assigning through the
    // inactive member is what makes it active.
    constexpr void assign(const T &value) noexcept
    {
        m_value = value;
    }

    [[nodiscard]] constexpr T *ptr() noexcept
    {
        return addressof(m_value);
    }

    [[nodiscard]] constexpr const T *ptr() const noexcept
    {
        return addressof(m_value);
    }

    [[nodiscard]] static constexpr bool is_reserved(const T &value) noexcept
    {
        niche_cell probe;
        probe.assign(value);
        return probe.m_repr == Empty;
    }
};

template <typename T, typename Repr, Repr Empty>
class niche_cell<T, Repr, Empty, true> {
    T m_value;

public:
    static constexpr bool SUPPORTS_CONSTANT_EVALUATION = true;

    constexpr niche_cell() noexcept
        : m_value(std::bit_cast<T>(Empty))
    {
    }

    [[nodiscard]] constexpr bool engaged() const noexcept
    {
        return std::bit_cast<Repr>(m_value) != Empty;
    }

    constexpr void disengage() noexcept
    {
        m_value = std::bit_cast<T>(Empty);
    }

    constexpr void assign(const T &value) noexcept
    {
        m_value = value;
    }

    [[nodiscard]] constexpr T *ptr() noexcept
    {
        return addressof(m_value);
    }

    [[nodiscard]] constexpr const T *ptr() const noexcept
    {
        return addressof(m_value);
    }

    [[nodiscard]] static constexpr bool is_reserved(const T &value) noexcept
    {
        return std::bit_cast<Repr>(value) == Empty;
    }
};

template <typename T, typename Policy>
class compressed_storage {
    using representation_type = Policy::representation_type;

    static_assert(std::is_scalar_v<T>,
                  "Compressed storage currently supports scalar payloads only");
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_trivially_copyable_v<representation_type>);
    static_assert(std::has_unique_object_representations_v<representation_type>);
    static_assert(sizeof(representation_type) == sizeof(T),
                  "The sentinel representation must have the same size as T");

    static_assert(alignof(representation_type) <= alignof(T),
                  "The sentinel representation must not over-align T");

    using cell_type = niche_cell<T, representation_type, Policy::value>;

    cell_type m_cell{};

    [[nodiscard]] constexpr T *ptr() noexcept
    {
        return m_cell.ptr();
    }

    [[nodiscard]] constexpr const T *ptr() const noexcept
    {
        return m_cell.ptr();
    }

    constexpr void write_empty() noexcept
    {
        m_cell.disengage();
    }

    [[nodiscard]] static constexpr bool has_reserved_representation(const T &value) noexcept
    {
        return cell_type::is_reserved(value);
    }

    constexpr void start_lifetime_from(const T &value) noexcept
    {
        m_cell.assign(value);
    }

public:
    static constexpr bool IS_COMPRESSED = true;
    static constexpr bool SUPPORTS_CONSTANT_EVALUATION = cell_type::SUPPORTS_CONSTANT_EVALUATION;

    constexpr compressed_storage() noexcept
    {
        write_empty();
    }

    constexpr compressed_storage(nullopt_t) noexcept
        : compressed_storage()
    {
    }

    constexpr compressed_storage(const T &value)
        : compressed_storage()
    {
        emplace(value);
    }

    constexpr compressed_storage(T &&value)
        : compressed_storage()
    {
        emplace(move_value(value));
    }

    template <typename... Args>
    explicit constexpr compressed_storage(in_place_t, Args &&...args)
        : compressed_storage()
    {
        emplace(forward_value<Args>(args)...);
    }

    // The cell is exactly sizeof(T) bytes of scalar data, so copying the representation is
    // always correct. Keeping these trivial is what lets Result<T, void> stay trivially
    // copyable and travel in registers.
    constexpr compressed_storage(const compressed_storage &) = default;
    constexpr compressed_storage(compressed_storage &&) noexcept = default;

    // T is required to be trivially destructible, so nothing has to run here. Keeping the
    // destructor trivial is what lets Result<T, void> be a literal type.
    ~compressed_storage() = default;

    constexpr compressed_storage &operator=(const compressed_storage &) = default;
    constexpr compressed_storage &operator=(compressed_storage &&) noexcept = default;

    constexpr compressed_storage &operator=(nullopt_t) noexcept
    {
        reset();
        return *this;
    }

    constexpr compressed_storage &operator=(const T &value)
    {
        emplace(value);
        return *this;
    }

    constexpr compressed_storage &operator=(T &&value)
    {
        emplace(move_value(value));
        return *this;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return m_cell.engaged();
    }

    explicit constexpr operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] constexpr T &operator*() & noexcept
    {
        RESULT_ASSERT(has_value());
        return *ptr();
    }

    [[nodiscard]] constexpr const T &operator*() const & noexcept
    {
        RESULT_ASSERT(has_value());
        return *ptr();
    }

    [[nodiscard]] constexpr T &&operator*() && noexcept
    {
        RESULT_ASSERT(has_value());
        return move_value(*ptr());
    }

    [[nodiscard]] constexpr const T &&operator*() const && noexcept
    {
        RESULT_ASSERT(has_value());
        return move_value(*ptr());
    }

    [[nodiscard]] constexpr T *operator->() noexcept
    {
        RESULT_ASSERT(has_value());
        return ptr();
    }

    [[nodiscard]] constexpr const T *operator->() const noexcept
    {
        RESULT_ASSERT(has_value());
        return ptr();
    }

    [[nodiscard]] constexpr T &value() &
    {
        if (!has_value())
            optional_empty_access();

        return *ptr();
    }

    [[nodiscard]] constexpr const T &value() const &
    {
        if (!has_value())
            optional_empty_access();

        return *ptr();
    }

    [[nodiscard]] constexpr T &&value() &&
    {
        if (!has_value())
            optional_empty_access();

        return move_value(*ptr());
    }

    [[nodiscard]] constexpr const T &&value() const &&
    {
        if (!has_value())
            optional_empty_access();

        return move_value(*ptr());
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) const &
    {
        return has_value() ? **this : static_cast<T>(forward_value<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) &&
    {
        return has_value() ? move_value(**this) : static_cast<T>(forward_value<U>(default_value));
    }

    template <typename... Args>
    constexpr T &emplace(Args &&...args)
    {
        T candidate(forward_value<Args>(args)...);

        if (has_reserved_representation(candidate))
            bad_sentinel_value_access();

        reset();
        start_lifetime_from(candidate);

        return *ptr();
    }

    constexpr void reset() noexcept
    {
        write_empty();
    }

    constexpr void swap(compressed_storage &other) noexcept(noexcept(detail::swap(**this, *other)))
    {
        if (has_value() && other.has_value()) {
            using detail::swap;
            swap(**this, *other);
        } else if (has_value()) {
            other.emplace(move_value(**this));
            reset();
        } else if (other.has_value()) {
            emplace(move_value(*other));
            other.reset();
        }
    }
};

template <typename T>
class separate_storage {
    union storage_union {
        char inactive;
        T    value;

        constexpr storage_union() noexcept
            : inactive{}
        {
        }

        // Trivial when T allows it, which is what keeps optional<T> a literal type.
        constexpr ~storage_union()
            requires std::is_trivially_destructible_v<T>
        = default;

        constexpr ~storage_union()
        {
        }
    } m_storage;

    bool m_has_value = false;

    // construct_at activates m_storage.value, so naming the member is already correct;
    // std::launder would only stand in the way of constant evaluation.
    [[nodiscard]] constexpr T *ptr() noexcept
    {
        return addressof(m_storage.value);
    }

    [[nodiscard]] constexpr const T *ptr() const noexcept
    {
        return addressof(m_storage.value);
    }

public:
    static constexpr bool IS_COMPRESSED = false;

    constexpr separate_storage() noexcept = default;
    constexpr separate_storage(nullopt_t) noexcept
        : separate_storage()
    {
    }

    template <typename U = T>
        requires std::constructible_from<T, U &&>
    explicit(!std::convertible_to<U &&, T>) constexpr separate_storage(U &&value)
        : separate_storage()
    {
        emplace(forward_value<U>(value));
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit constexpr separate_storage(in_place_t, Args &&...args)
        : separate_storage()
    {
        emplace(forward_value<Args>(args)...);
    }

    constexpr separate_storage(const separate_storage &other)
        requires std::copy_constructible<T>
        : separate_storage()
    {
        if (other.has_value())
            emplace(*other);
    }

    constexpr separate_storage(separate_storage &&other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires std::move_constructible<T>
        : separate_storage()
    {
        if (other.has_value())
            emplace(move_value(*other));
    }

    constexpr ~separate_storage()
        requires std::is_trivially_destructible_v<T>
    = default;

    constexpr ~separate_storage()
    {
        reset();
    }

    constexpr separate_storage &operator=(const separate_storage &other)
        requires std::copy_constructible<T>
    {
        if (this != addressof(other)) {
            if (other.has_value()) {
                emplace(*other);
            } else {
                reset();
            }
        }

        return *this;
    }

    constexpr separate_storage &operator=(separate_storage &&other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires std::move_constructible<T>
    {
        if (this != addressof(other)) {
            if (other.has_value()) {
                emplace(move_value(*other));
            } else {
                reset();
            }
        }

        return *this;
    }

    constexpr separate_storage &operator=(nullopt_t) noexcept
    {
        reset();
        return *this;
    }

    template <typename U = T>
        requires std::constructible_from<T, U &&>
    constexpr separate_storage &operator=(U &&value)
    {
        emplace(forward_value<U>(value));
        return *this;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return m_has_value;
    }

    explicit constexpr operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] constexpr T &operator*() & noexcept
    {
        RESULT_ASSERT(has_value());
        return *ptr();
    }

    [[nodiscard]] constexpr const T &operator*() const & noexcept
    {
        RESULT_ASSERT(has_value());
        return *ptr();
    }

    [[nodiscard]] constexpr T &&operator*() && noexcept
    {
        RESULT_ASSERT(has_value());
        return move_value(*ptr());
    }

    [[nodiscard]] constexpr const T &&operator*() const && noexcept
    {
        RESULT_ASSERT(has_value());
        return move_value(*ptr());
    }

    [[nodiscard]] constexpr T *operator->() noexcept
    {
        RESULT_ASSERT(has_value());
        return ptr();
    }

    [[nodiscard]] constexpr const T *operator->() const noexcept
    {
        RESULT_ASSERT(has_value());
        return ptr();
    }

    [[nodiscard]] constexpr T &value() &
    {
        if (!has_value())
            optional_empty_access();

        return *ptr();
    }

    [[nodiscard]] constexpr const T &value() const &
    {
        if (!has_value())
            optional_empty_access();

        return *ptr();
    }

    [[nodiscard]] constexpr T &&value() &&
    {
        if (!has_value())
            optional_empty_access();

        return move_value(*ptr());
    }

    [[nodiscard]] constexpr const T &&value() const &&
    {
        if (!has_value())
            optional_empty_access();

        return move_value(*ptr());
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) const &
    {
        return has_value() ? **this : static_cast<T>(forward_value<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) &&
    {
        return has_value() ? move_value(**this) : static_cast<T>(forward_value<U>(default_value));
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    constexpr T &emplace(Args &&...args)
    {
        reset();
        construct_at(addressof(m_storage.value), forward_value<Args>(args)...);
        m_has_value = true;

        return *ptr();
    }

    constexpr void reset() noexcept
    {
        if (has_value()) {
            destroy_at(ptr());
            m_has_value = false;
        }
    }

    constexpr void swap(separate_storage &other) noexcept(noexcept(detail::swap(**this, *other)) &&
                                                          std::is_nothrow_move_constructible_v<T>)
    {
        if (has_value() && other.has_value()) {
            detail::swap(**this, *other);
        } else if (has_value()) {
            other.emplace(move_value(**this));
            reset();
        } else if (other.has_value()) {
            emplace(move_value(*other));
            other.reset();
        }
    }
};

template <typename T, typename Policy, bool = sentinel_policy<Policy>>
struct storage_selector;

template <typename T, typename Policy>
struct storage_selector<T, Policy, true> {
    using type = compressed_storage<T, Policy>;
};

template <typename T, typename Policy>
struct storage_selector<T, Policy, false> {
    static_assert(std::is_same_v<Policy, separate_flag_policy>,
                  "Policy must be a sentinel policy or separate_flag_policy");
    using type = separate_storage<T>;
};

}  // namespace detail

template <typename T, typename Policy = default_policy_t<T>>
class optional : public detail::sentinel_metadata<Policy> {
    static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                  "storage::optional requires an unqualified payload type");
    static_assert(std::is_object_v<T> && !std::is_array_v<T>,
                  "storage::optional requires a non-array object type");
    static_assert(std::is_destructible_v<T>);

    using storage_type = detail::storage_selector<T, Policy>::type;
    storage_type m_storage;

public:
    using value_type = T;
    using policy_type = Policy;

    static constexpr bool USES_COMPRESSED_STORAGE = storage_type::IS_COMPRESSED;

    constexpr optional() noexcept(std::is_nothrow_default_constructible_v<storage_type>) = default;

    constexpr optional(nullopt_t) noexcept
        : m_storage(nullopt)
    {
    }

    template <typename U = T>
        requires std::constructible_from<storage_type, U &&> &&
                 (!std::same_as<std::remove_cvref_t<U>, optional>) &&
                 (!std::same_as<std::remove_cvref_t<U>, in_place_t>) &&
                 (!std::same_as<std::remove_cvref_t<U>, nullopt_t>)
    explicit(!std::convertible_to<U &&, T>) constexpr optional(U &&value)
        : m_storage(forward_value<U>(value))
    {
    }

    template <typename... Args>
        requires std::constructible_from<storage_type, in_place_t, Args...>
    explicit constexpr optional(in_place_t, Args &&...args)
        : m_storage(in_place, forward_value<Args>(args)...)
    {
    }

    optional(const optional &) = default;
    optional(optional &&) = default;

    ~optional() = default;

    optional &operator=(const optional &) = default;
    optional &operator=(optional &&) = default;

    constexpr optional &operator=(nullopt_t) noexcept
    {
        m_storage = nullopt;
        return *this;
    }

    template <typename U = T>
        requires requires(storage_type &storage, U &&value) {
            storage = forward_value<U>(value);
        } && (!std::same_as<std::remove_cvref_t<U>, optional>)
    constexpr optional &operator=(U &&value)
    {
        m_storage = forward_value<U>(value);
        return *this;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return m_storage.has_value();
    }

    explicit constexpr operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] constexpr T &operator*() & noexcept
    {
        return *m_storage;
    }

    [[nodiscard]] constexpr const T &operator*() const & noexcept
    {
        return *m_storage;
    }

    [[nodiscard]] constexpr T &&operator*() && noexcept
    {
        return *move_value(m_storage);
    }

    [[nodiscard]] constexpr const T &&operator*() const && noexcept
    {
        return *move_value(m_storage);
    }

    [[nodiscard]] constexpr T *operator->() noexcept
    {
        return m_storage.operator->();
    }

    [[nodiscard]] constexpr const T *operator->() const noexcept
    {
        return m_storage.operator->();
    }

    [[nodiscard]] constexpr T &value() &
    {
        return m_storage.value();
    }

    [[nodiscard]] constexpr const T &value() const &
    {
        return m_storage.value();
    }

    [[nodiscard]] constexpr T &&value() &&
    {
        return move_value(m_storage).value();
    }

    [[nodiscard]] constexpr const T &&value() const &&
    {
        return move_value(m_storage).value();
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) const &
    {
        return m_storage.value_or(forward_value<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) &&
    {
        return move_value(m_storage).value_or(forward_value<U>(default_value));
    }

    template <typename... Args>
        requires requires(storage_type &storage, Args &&...args) {
            storage.emplace(forward_value<Args>(args)...);
        }
    constexpr T &emplace(Args &&...args)
    {
        return m_storage.emplace(forward_value<Args>(args)...);
    }

    constexpr void reset() noexcept
    {
        m_storage.reset();
    }

    constexpr void swap(optional &other) noexcept(noexcept(m_storage.swap(other.m_storage)))
    {
        m_storage.swap(other.m_storage);
    }
};

template <typename T, typename LhsPolicy, typename RhsPolicy>
[[nodiscard]] constexpr bool operator==(const optional<T, LhsPolicy> &lhs,
                                        const optional<T, RhsPolicy> &rhs)
{
    if (lhs.has_value() != rhs.has_value())
        return false;

    return !lhs.has_value() || *lhs == *rhs;
}

template <typename T, typename Policy>
[[nodiscard]] constexpr bool operator==(const optional<T, Policy> &value, nullopt_t) noexcept
{
    return !value.has_value();
}

template <typename T, typename Policy>
[[nodiscard]] constexpr bool operator==(nullopt_t, const optional<T, Policy> &value) noexcept
{
    return !value.has_value();
}

template <typename T, typename Policy>
[[nodiscard]] constexpr bool operator==(const optional<T, Policy> &lhs, const T &rhs)
{
    return lhs.has_value() && *lhs == rhs;
}

template <typename T, typename Policy>
[[nodiscard]] constexpr bool operator==(const T &lhs, const optional<T, Policy> &rhs)
{
    return rhs == lhs;
}

template <typename T, typename Policy>
constexpr void swap(optional<T, Policy> &lhs,
                    optional<T, Policy> &rhs) noexcept(noexcept(lhs.swap(rhs)))
{
    lhs.swap(rhs);
}

// Internal optional representation used for Result<T&, void> and Result<void, E&>.
// A C++ reference cannot denote null, so nullptr is a true semantic niche here.
// This deliberately stores and compares an actual pointer value; it does not assume
// that the null pointer object representation is all-bits-zero.
template <typename Pointer>
class reference_optional {
    static_assert(std::is_pointer_v<Pointer>);

    Pointer m_value = nullptr;

public:
    constexpr reference_optional() noexcept = default;

    constexpr reference_optional(nullopt_t) noexcept
    {
    }

    explicit constexpr reference_optional(Pointer input) noexcept
        : m_value(input)
    {
        RESULT_ASSERT(input != nullptr);
    }

    reference_optional(const reference_optional &) = default;
    reference_optional(reference_optional &&) noexcept = default;
    reference_optional &operator=(const reference_optional &) = default;
    reference_optional &operator=(reference_optional &&) noexcept = default;

    constexpr reference_optional &operator=(nullopt_t) noexcept
    {
        m_value = nullptr;
        return *this;
    }

    constexpr reference_optional &operator=(Pointer input) noexcept
    {
        RESULT_ASSERT(input != nullptr);
        m_value = input;
        return *this;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return m_value != nullptr;
    }

    explicit constexpr operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] constexpr Pointer &operator*() & noexcept
    {
        RESULT_ASSERT(has_value());
        return m_value;
    }

    [[nodiscard]] constexpr const Pointer &operator*() const & noexcept
    {
        RESULT_ASSERT(has_value());
        return m_value;
    }

    [[nodiscard]] constexpr Pointer &&operator*() && noexcept
    {
        RESULT_ASSERT(has_value());
        return static_cast<Pointer &&>(m_value);
    }

    [[nodiscard]] constexpr const Pointer &&operator*() const && noexcept
    {
        RESULT_ASSERT(has_value());
        return static_cast<const Pointer &&>(m_value);
    }

    constexpr Pointer &emplace(Pointer input) noexcept
    {
        RESULT_ASSERT(input != nullptr);
        m_value = input;
        return m_value;
    }

    constexpr void reset() noexcept
    {
        m_value = nullptr;
    }

    constexpr void swap(reference_optional &other) noexcept
    {
        Pointer temporary = m_value;
        m_value = other.m_value;
        other.m_value = temporary;
    }
};

}  // namespace storage

// =================================================================================================
// Specialized storage, uniform interface
// =================================================================================================

template <typename T>
struct optional_traits {
    static constexpr bool is_optional = false;
};

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
template <typename T>
struct optional_traits<std::optional<T>> {
    static constexpr bool is_optional = true;
    using value_type = T;

    template <typename U>
    using rebind = std::optional<U>;
};
#endif

template <typename T, typename Policy>
struct optional_traits<storage::optional<T, Policy>> {
    static constexpr bool is_optional = true;
    using value_type = T;

    template <typename U>
    using rebind = storage::optional<U>;
};

template <typename Payload, bool = std::is_lvalue_reference_v<Payload>>
struct result_optional_selector {
    using type = storage::optional<stored_type_t<Payload>>;
};

template <typename Payload>
struct result_optional_selector<Payload, true> {
    using type = storage::reference_optional<stored_type_t<Payload>>;
};

template <typename Payload>
using result_optional_t = result_optional_selector<Payload>::type;

template <typename T, typename E>
class result_storage {
    static_assert(!std::is_void_v<T> && !std::is_void_v<E>,
                  "Primary result_storage requires non-void T and E.");

    using ok_wrapper = wrapper::Ok<T>;
    using err_wrapper = wrapper::Err<E>;

    union data_union {
        // A dormant member gives the union a constexpr default constructor without
        // activating either payload; one of them is activated by every constructor below.
        char        dormant;
        ok_wrapper  ok;
        err_wrapper err;

        constexpr data_union() noexcept
            : dormant{}
        {
        }

        constexpr ~data_union()
            requires std::is_trivially_destructible_v<ok_wrapper> &&
                         std::is_trivially_destructible_v<err_wrapper>
        = default;

        constexpr ~data_union()
        {
        }
    } m_data;

    bool m_ok;

    // These are spelled as explicit complements below. Type traits in a requires-clause are
    // atomic constraints and do not subsume one another, so a bare trivial / non-trivial
    // pair would be ambiguous rather than ordered. (Destructors are exempt: an unconstrained
    // prospective destructor is always the weaker candidate.)
    static constexpr bool trivially_copy_constructible =
        std::is_trivially_copy_constructible_v<ok_wrapper> &&
        std::is_trivially_copy_constructible_v<err_wrapper>;
    static constexpr bool trivially_move_constructible =
        std::is_trivially_move_constructible_v<ok_wrapper> &&
        std::is_trivially_move_constructible_v<err_wrapper>;
    static constexpr bool trivially_copy_assignable =
        trivially_copy_constructible && std::is_trivially_copy_assignable_v<ok_wrapper> &&
        std::is_trivially_copy_assignable_v<err_wrapper> &&
        std::is_trivially_destructible_v<ok_wrapper> &&
        std::is_trivially_destructible_v<err_wrapper>;
    static constexpr bool trivially_move_assignable =
        trivially_move_constructible && std::is_trivially_move_assignable_v<ok_wrapper> &&
        std::is_trivially_move_assignable_v<err_wrapper> &&
        std::is_trivially_destructible_v<ok_wrapper> &&
        std::is_trivially_destructible_v<err_wrapper>;

    constexpr ok_wrapper &ok_state()
    {
        return m_data.ok;
    }

    constexpr const ok_wrapper &ok_state() const
    {
        return m_data.ok;
    }

    constexpr err_wrapper &err_state()
    {
        return m_data.err;
    }

    constexpr const err_wrapper &err_state() const
    {
        return m_data.err;
    }

    constexpr void destroy_active() noexcept
    {
        m_ok ? destroy_at(addressof(m_data.ok)) : destroy_at(addressof(m_data.err));
    }

    constexpr void copy_err_to_ok(const ok_wrapper &source)
    {
        if constexpr (std::is_nothrow_move_constructible_v<ok_wrapper>) {
            ok_wrapper candidate(source);

            destroy_at(addressof(m_data.err));
            construct_at(addressof(m_data.ok), move_value(candidate));
            m_ok = true;
        } else {
            static_assert(std::is_nothrow_move_constructible_v<err_wrapper>);
            err_wrapper backup(move_value(err_state()));

            destroy_at(addressof(m_data.err));
#if RESULT_HAS_HOSTED_EXCEPTIONS
            try {
                construct_at(addressof(m_data.ok), source);
                m_ok = true;
            } catch (...) {
                construct_at(addressof(m_data.err), move_value(backup));
                throw;
            }
#else
            construct_at(addressof(m_data.ok), source);
            m_ok = true;
#endif
        }
    }

    constexpr void copy_ok_to_err(const err_wrapper &source)
    {
        if constexpr (std::is_nothrow_move_constructible_v<err_wrapper>) {
            err_wrapper candidate(source);

            destroy_at(addressof(m_data.ok));
            construct_at(addressof(m_data.err), move_value(candidate));
            m_ok = false;
        } else {
            static_assert(std::is_nothrow_move_constructible_v<ok_wrapper>);
            ok_wrapper backup(move_value(ok_state()));

            destroy_at(addressof(m_data.ok));
#if RESULT_HAS_HOSTED_EXCEPTIONS
            try {
                construct_at(addressof(m_data.err), source);
                m_ok = false;
            } catch (...) {
                construct_at(addressof(m_data.ok), move_value(backup));
                throw;
            }
#else
            construct_at(addressof(m_data.err), source);
            m_ok = false;
#endif
        }
    }

    constexpr void move_err_to_ok(ok_wrapper &&source)
    {
        if constexpr (std::is_nothrow_move_constructible_v<ok_wrapper>) {
            destroy_at(addressof(m_data.err));
            construct_at(addressof(m_data.ok), move_value(source));
            m_ok = true;
        } else {
            static_assert(std::is_nothrow_move_constructible_v<err_wrapper>);
            err_wrapper backup(move_value(err_state()));

            destroy_at(addressof(m_data.err));
#if RESULT_HAS_HOSTED_EXCEPTIONS
            try {
                construct_at(addressof(m_data.ok), move_value(source));
                m_ok = true;
            } catch (...) {
                construct_at(addressof(m_data.err), move_value(backup));
                throw;
            }
#else
            construct_at(addressof(m_data.ok), move_value(source));
            m_ok = true;
#endif
        }
    }

    constexpr void move_ok_to_err(err_wrapper &&source)
    {
        if constexpr (std::is_nothrow_move_constructible_v<err_wrapper>) {
            destroy_at(addressof(m_data.ok));
            construct_at(addressof(m_data.err), move_value(source));
            m_ok = false;
        } else {
            static_assert(std::is_nothrow_move_constructible_v<ok_wrapper>);
            ok_wrapper backup(move_value(ok_state()));

            destroy_at(addressof(m_data.ok));
#if RESULT_HAS_HOSTED_EXCEPTIONS
            try {
                construct_at(addressof(m_data.err), move_value(source));
                m_ok = false;
            } catch (...) {
                construct_at(addressof(m_data.ok), move_value(backup));
                throw;
            }
#else
            construct_at(addressof(m_data.err), move_value(source));
            m_ok = false;
#endif
        }
    }

public:
    // The wrappers arrive by rvalue reference and the resolved payload is built straight into
    // the union member. A by-value parameter, or resolving into a whole wrapper::Ok<T> before
    // moving that into place, each cost one more move of the payload per construction.
    template <typename U, std::enable_if_t<resolve_plan<T, U>::legal, int> = 0>
    explicit constexpr result_storage(wrapper::Ok<U> &&ok)
        : m_ok(true)
    {
        construct_at(addressof(m_data.ok), resolve_plan<T, U>::apply(take_payload(move_value(ok))));
    }

    template <typename G, std::enable_if_t<resolve_plan<E, G>::legal, int> = 0>
    explicit constexpr result_storage(wrapper::Err<G> &&err)
        : m_ok(false)
    {
        construct_at(addressof(m_data.err),
                     resolve_plan<E, G>::apply(take_payload(move_value(err))));
    }

    constexpr result_storage(const result_storage &)
        requires trivially_copy_constructible
    = default;

    constexpr result_storage(const result_storage &other)
        requires std::copy_constructible<ok_wrapper> && std::copy_constructible<err_wrapper> &&
                 (!trivially_copy_constructible)
        : m_ok(other.m_ok)
    {
        // See the move constructor below: construct_at returns a pointer, so this cannot be a
        // conditional expression.
        if (m_ok) {
            construct_at(addressof(m_data.ok), other.ok_state());
        } else {
            construct_at(addressof(m_data.err), other.err_state());
        }
    }

    constexpr result_storage(result_storage &&other) noexcept(
        std::is_nothrow_move_constructible_v<ok_wrapper> &&
        std::is_nothrow_move_constructible_v<err_wrapper>)
        requires trivially_move_constructible
    = default;

    constexpr result_storage(result_storage &&other) noexcept(
        std::is_nothrow_move_constructible_v<ok_wrapper> &&
        std::is_nothrow_move_constructible_v<err_wrapper>)
        requires std::move_constructible<ok_wrapper> && std::move_constructible<err_wrapper> &&
                 (!trivially_move_constructible)
        : m_ok(other.m_ok)
    {
        // if/else, not a conditional expression: construct_at returns a pointer, and the two
        // branches yield distinct pointer types unless T and E happen to coincide.
        if (m_ok) {
            construct_at(addressof(m_data.ok), move_value(other.ok_state()));
        } else {
            construct_at(addressof(m_data.err), move_value(other.err_state()));
        }
    }

    constexpr ~result_storage()
        requires std::is_trivially_destructible_v<ok_wrapper> &&
                     std::is_trivially_destructible_v<err_wrapper>
    = default;

    constexpr ~result_storage()
    {
        destroy_active();
    }

    constexpr result_storage &operator=(const result_storage &)
        requires trivially_copy_assignable
    = default;

    constexpr result_storage &operator=(const result_storage &other)
        requires std::copy_constructible<ok_wrapper> && std::copy_constructible<err_wrapper> &&
                 std::is_copy_assignable_v<ok_wrapper> && std::is_copy_assignable_v<err_wrapper> &&
                 (std::is_nothrow_move_constructible_v<ok_wrapper> ||
                  std::is_nothrow_move_constructible_v<err_wrapper>) &&
                 (!trivially_copy_assignable)
    {
        if (this == addressof(other))
            return *this;

        if (m_ok == other.m_ok) {
            if (m_ok) {
                ok_state() = other.ok_state();
            } else {
                err_state() = other.err_state();
            }
        } else if (other.m_ok) {
            copy_err_to_ok(other.ok_state());
        } else {
            copy_ok_to_err(other.err_state());
        }

        return *this;
    }

    constexpr result_storage &operator=(result_storage &&)
        requires trivially_move_assignable
    = default;

    constexpr result_storage &operator=(result_storage &&other) noexcept(
        std::is_nothrow_move_assignable_v<ok_wrapper> &&
        std::is_nothrow_move_assignable_v<err_wrapper> &&
        std::is_nothrow_move_constructible_v<ok_wrapper> &&
        std::is_nothrow_move_constructible_v<err_wrapper>)
        requires std::move_constructible<ok_wrapper> && std::move_constructible<err_wrapper> &&
                 std::is_move_assignable_v<ok_wrapper> && std::is_move_assignable_v<err_wrapper> &&
                 (std::is_nothrow_move_constructible_v<ok_wrapper> ||
                  std::is_nothrow_move_constructible_v<err_wrapper>) &&
                 (!trivially_move_assignable)
    {
        if (this == addressof(other))
            return *this;

        if (m_ok == other.m_ok) {
            if (m_ok) {
                ok_state() = move_value(other.ok_state());
            } else {
                err_state() = move_value(other.err_state());
            }
        } else if (other.m_ok) {
            move_err_to_ok(move_value(other.ok_state()));
        } else {
            move_ok_to_err(move_value(other.err_state()));
        }

        return *this;
    }

    [[nodiscard]] constexpr bool has_ok() const noexcept
    {
        return m_ok;
    }

    [[nodiscard]] constexpr bool has_err() const noexcept
    {
        return !m_ok;
    }

    constexpr decltype(auto) ok_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return *ok_state().value;
        } else {
            return (ok_state().value);
        }
    }

    constexpr decltype(auto) ok_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return *ok_state().value;
        } else {
            return static_cast<const T &>(ok_state().value);
        }
    }

    constexpr decltype(auto) err_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return *err_state().value;
        } else {
            return (err_state().value);
        }
    }

    constexpr decltype(auto) err_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return *err_state().value;
        } else {
            return static_cast<const E &>(err_state().value);
        }
    }

    constexpr decltype(auto) take_ok() &&
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return *ok_state().value;
        } else {
            return move_value(ok_state().value);
        }
    }

    constexpr decltype(auto) take_err() &&
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return *err_state().value;
        } else {
            return move_value(err_state().value);
        }
    }
};

template <typename T>
class result_storage<T, void> {
    static_assert(!std::is_void_v<T>, "Use result_storage<void, void>.");

    result_optional_t<T> m_ok;

public:
    // Emplaced rather than default-constructed and then assigned, which would move the
    // payload a second time.
    template <typename U, std::enable_if_t<resolve_plan<T, U>::legal, int> = 0>
    explicit constexpr result_storage(wrapper::Ok<U> &&ok)
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            m_ok.emplace(addressof(resolve_plan<T, U>::apply(take_payload(move_value(ok)))));
        } else {
            m_ok.emplace(resolve_plan<T, U>::apply(take_payload(move_value(ok))));
        }

        RESULT_ASSERT(m_ok.has_value() && "Ok value equals the reserved empty representation.");
    }

    explicit constexpr result_storage(wrapper::Err<void>) noexcept
    {
    }

    constexpr result_storage(const result_storage &) = default;
    constexpr result_storage(result_storage &&) = default;
    constexpr result_storage &operator=(const result_storage &) = default;
    constexpr result_storage &operator=(result_storage &&) = default;

    [[nodiscard]] constexpr bool has_ok() const noexcept
    {
        return m_ok.has_value();
    }

    [[nodiscard]] constexpr bool has_err() const noexcept
    {
        return !m_ok.has_value();
    }

    constexpr decltype(auto) ok_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return **m_ok;
        } else {
            return (*m_ok);
        }
    }

    constexpr decltype(auto) ok_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return **m_ok;
        } else {
            return static_cast<const T &>(*m_ok);
        }
    }

    constexpr void_value err_ref() const noexcept
    {
        return {};
    }

    constexpr decltype(auto) take_ok() &&
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return **m_ok;
        } else {
            return move_value(*m_ok);
        }
    }

    constexpr void_value take_err() && noexcept
    {
        return {};
    }
};

template <typename E>
class result_storage<void, E> {
    static_assert(!std::is_void_v<E>, "Use result_storage<void, void>.");

    result_optional_t<E> m_err;

public:
    explicit constexpr result_storage(wrapper::Ok<void>) noexcept
    {
    }

    template <typename G, std::enable_if_t<resolve_plan<E, G>::legal, int> = 0>
    explicit constexpr result_storage(wrapper::Err<G> &&err)
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            m_err.emplace(addressof(resolve_plan<E, G>::apply(take_payload(move_value(err)))));
        } else {
            m_err.emplace(resolve_plan<E, G>::apply(take_payload(move_value(err))));
        }

        RESULT_ASSERT(m_err.has_value() && "Err value equals the reserved empty representation.");
    }

    constexpr result_storage(const result_storage &) = default;
    constexpr result_storage(result_storage &&) = default;
    constexpr result_storage &operator=(const result_storage &) = default;
    constexpr result_storage &operator=(result_storage &&) = default;

    [[nodiscard]] constexpr bool has_ok() const noexcept
    {
        return !m_err.has_value();
    }

    [[nodiscard]] constexpr bool has_err() const noexcept
    {
        return m_err.has_value();
    }

    constexpr void_value ok_ref() const noexcept
    {
        return {};
    }

    constexpr decltype(auto) err_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return **m_err;
        } else {
            return (*m_err);
        }
    }

    constexpr decltype(auto) err_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return **m_err;
        } else {
            return static_cast<const E &>(*m_err);
        }
    }

    constexpr void_value take_ok() && noexcept
    {
        return {};
    }

    constexpr decltype(auto) take_err() &&
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return **m_err;
        } else {
            return move_value(*m_err);
        }
    }
};

template <>
class result_storage<void, void> {
    bool m_ok;

public:
    explicit constexpr result_storage(wrapper::Ok<void>) noexcept
        : m_ok(true)
    {
    }

    explicit constexpr result_storage(wrapper::Err<void>) noexcept
        : m_ok(false)
    {
    }

    result_storage(const result_storage &) = default;
    result_storage(result_storage &&) noexcept = default;
    result_storage &operator=(const result_storage &) = default;
    result_storage &operator=(result_storage &&) noexcept = default;

    [[nodiscard]] constexpr bool has_ok() const noexcept
    {
        return m_ok;
    }

    [[nodiscard]] constexpr bool has_err() const noexcept
    {
        return !m_ok;
    }

    constexpr void_value ok_ref() const noexcept
    {
        return {};
    }

    constexpr void_value err_ref() const noexcept
    {
        return {};
    }

    constexpr void_value take_ok() && noexcept
    {
        return {};
    }

    constexpr void_value take_err() && noexcept
    {
        return {};
    }
};

}  // namespace detail

// =================================================================================================
// One public Result implementation
// =================================================================================================

template <typename T, typename E>
class [[nodiscard]] Result : private detail::result_storage<T, E> {
    static_assert(!std::is_rvalue_reference_v<T>, "Result<T&&, E> is not supported.");
    static_assert(!std::is_rvalue_reference_v<E>, "Result<T, E&&> is not supported.");

    // A top-level const or volatile payload cannot be moved out of, so every consuming
    // accessor would silently copy, and the optional-backed storage behind Result<T, void>
    // cannot hold one at all. Rejecting it here keeps all four T/E void combinations
    // agreeing, and says so at the altitude the mistake was made.
    static_assert(std::is_reference_v<T> || !(std::is_const_v<T> || std::is_volatile_v<T>),
                  "Result<const T, E> and Result<volatile T, E> are not supported. Qualify the "
                  "Result itself, or borrow with Result<const T &, E>.");
    static_assert(std::is_reference_v<E> || !(std::is_const_v<E> || std::is_volatile_v<E>),
                  "Result<T, const E> and Result<T, volatile E> are not supported. Qualify the "
                  "Result itself, or borrow with Result<T, const E &>.");

    using storage = detail::result_storage<T, E>;

public:
    using value_type = T;
    using error_type = E;

private:
    constexpr storage &storage_ref() & noexcept
    {
        return static_cast<storage &>(*this);
    }

    constexpr const storage &storage_ref() const & noexcept
    {
        return static_cast<const storage &>(*this);
    }

    constexpr storage &&storage_ref() && noexcept
    {
        return static_cast<storage &&>(*this);
    }

    // Forwards the branch a combinator did not touch. Output is the only parameter, so the
    // rule that admits a propagation and the code that performs it cannot end up describing
    // two different conversions. See detail::branch_propagates_v for what is admitted.
    //
    // The body is guarded rather than merely asserted so that a rejected propagation
    // produces one diagnostic instead of an assertion followed by the wreckage of the
    // construction it was meant to prevent.

    template <typename Output>
    constexpr Output propagate_ok_into() &&
    {
        constexpr bool legal = detail::branch_propagates_v<T, detail::result_ok_t<Output>>;

        static_assert(legal,
                      "This Ok branch cannot be propagated into the target Result. Propagating "
                      "a branch never converts the payload it carries; reach the target type "
                      "with map first.");

        if constexpr (!legal) {
            __builtin_unreachable();
        } else if constexpr (std::is_void_v<T>) {
            return Output(Ok());
        } else {
            return Output(Ok(detail::move_value(*this).storage_ref().take_ok()));
        }
    }

    template <typename Output>
    constexpr Output propagate_err_into() &&
    {
        constexpr bool legal = detail::branch_propagates_v<E, detail::result_err_t<Output>>;

        static_assert(legal,
                      "This Err branch cannot be propagated into the target Result. Propagating "
                      "a branch never converts the error it carries; reach the target type with "
                      "map_err first.");

        if constexpr (!legal) {
            __builtin_unreachable();
        } else if constexpr (std::is_void_v<E>) {
            return Output(Err());
        } else {
            return Output(Err(detail::move_value(*this).storage_ref().take_err()));
        }
    }

public:
    using ok_type = T;
    using err_type = E;

    template <typename U, std::enable_if_t<detail::resolve_plan<T, U>::legal, int> = 0>
    constexpr Result(wrapper::Ok<U> ok)
        : storage(detail::move_value(ok))
    {
    }

    template <typename G, std::enable_if_t<detail::resolve_plan<E, G>::legal, int> = 0>
    constexpr Result(wrapper::Err<G> err)
        : storage(detail::move_value(err))
    {
    }

    Result(const Result &) = default;
    Result(Result &&) noexcept(std::is_nothrow_move_constructible_v<storage>) = default;

    Result &operator=(const Result &) = default;
    Result &operator=(Result &&) noexcept(std::is_nothrow_move_assignable_v<storage>) = default;

    ~Result() = default;

    // ---------------------------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------------------------

    [[nodiscard]] constexpr bool is_ok() const noexcept
    {
        return storage_ref().has_ok();
    }

    [[nodiscard]] constexpr bool is_err() const noexcept
    {
        return storage_ref().has_err();
    }

    // ---------------------------------------------------------------------------------------------
    // Borrowing access
    // ---------------------------------------------------------------------------------------------

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) unwrap_ref() &
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap_ref a Result containing an error");

        return storage_ref().ok_ref();
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) unwrap_ref() const &
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap_ref a Result containing an error");

        return storage_ref().ok_ref();
    }

    template <typename G = E, std::enable_if_t<!std::is_void_v<G>, int> = 0>
    constexpr decltype(auto) unwrap_err_ref() &
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err_ref an Ok Result");

        return storage_ref().err_ref();
    }

    template <typename G = E, std::enable_if_t<!std::is_void_v<G>, int> = 0>
    constexpr decltype(auto) unwrap_err_ref() const &
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err_ref an Ok Result");

        return storage_ref().err_ref();
    }

    // The borrowing accessors are lvalue-only on purpose. Without these overloads the
    // const & versions bind happily to a temporary and hand out a reference into a Result
    // that dies at the end of the full expression. Name the Result first, or use the
    // consuming unwrap() / unwrap_err() on the rvalue.

    template <typename U = T>
    constexpr decltype(auto) unwrap_ref() &&
    {
        static_assert(detail::result_always_false_v<U>,
                      "unwrap_ref() on a temporary would dangle. Name the Result first, or "
                      "use unwrap() to consume it.");
    }

    template <typename U = T>
    constexpr decltype(auto) unwrap_ref() const &&
    {
        static_assert(detail::result_always_false_v<U>,
                      "unwrap_ref() on a temporary would dangle. Name the Result first, or "
                      "use unwrap() to consume it.");
    }

    template <typename G = E>
    constexpr decltype(auto) unwrap_err_ref() &&
    {
        static_assert(detail::result_always_false_v<G>,
                      "unwrap_err_ref() on a temporary would dangle. Name the Result first, "
                      "or use unwrap_err() to consume it.");
    }

    template <typename G = E>
    constexpr decltype(auto) unwrap_err_ref() const &&
    {
        static_assert(detail::result_always_false_v<G>,
                      "unwrap_err_ref() on a temporary would dangle. Name the Result first, "
                      "or use unwrap_err() to consume it.");
    }

    // ---------------------------------------------------------------------------------------------
    // Consuming access
    //
    // These declare T or E as their return type rather than decltype(auto), and that is the whole
    // reason the storage layer can hand out an rvalue. The storage accessors yield T&& / E&&,
    // which costs nothing when a combinator feeds the payload straight into its output; the single
    // move happens here, at the public boundary, where declaring the type by value also makes the
    // result impossible to dangle. Rust gets this for free because consuming self is destructive.
    //
    // Do not "simplify" these back to decltype(auto): it would deduce T&& and leak a reference
    // into storage out to callers, where `auto &&x = f().unwrap();` dangles.
    // ---------------------------------------------------------------------------------------------

    template <typename U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    constexpr void unwrap() const &
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap a Result containing an error");
    }

    template <typename U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    constexpr void unwrap_unchecked() const & noexcept
    {
    }

    template <typename G = E, std::enable_if_t<std::is_void_v<G>, int> = 0>
    constexpr void unwrap_err() const &
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err an Ok Result");
    }

    template <typename G = E, std::enable_if_t<std::is_void_v<G>, int> = 0>
    constexpr void unwrap_err_unchecked() const & noexcept
    {
    }

    constexpr T unwrap() &&
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap a Result containing an error");

        if constexpr (!std::is_void_v<T>)
            return detail::move_value(*this).storage_ref().take_ok();
    }

    constexpr T unwrap_unchecked() &&
    {
        if constexpr (!std::is_void_v<T>)
            return detail::move_value(*this).storage_ref().take_ok();
    }

    constexpr E unwrap_err() &&
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err an Ok Result");

        if constexpr (!std::is_void_v<E>)
            return detail::move_value(*this).storage_ref().take_err();
    }

    constexpr E unwrap_err_unchecked() &&
    {
        if constexpr (!std::is_void_v<E>)
            return detail::move_value(*this).storage_ref().take_err();
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    constexpr U unwrap_or(U fallback) &&
    {
        if (is_ok())
            return detail::resolve_expression<U>(detail::move_value(*this).storage_ref().take_ok());

        if constexpr (std::is_lvalue_reference_v<U>) {
            return fallback;
        } else {
            return detail::move_value(fallback);
        }
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U> && !std::is_reference_v<U> &&
                                                   std::is_default_constructible_v<U>,
                                               int> = 0>
    constexpr U unwrap_or_default() &&
    {
        if (is_ok())
            return detail::resolve_expression<U>(detail::move_value(*this).storage_ref().take_ok());

        return U{};
    }

#if RESULT_HAS_HOSTED_EXCEPTIONS
    constexpr T unwrap_or_throw() &&
    {
        if (is_ok()) {
            if constexpr (std::is_void_v<T>) {
                return;
            } else {
                return detail::move_value(*this).storage_ref().take_ok();
            }
        }

        if constexpr (std::is_void_v<E>) {
            throw bad_result_access{};
        } else {
            static_assert(!std::is_lvalue_reference_v<E>,
                          "unwrap_or_throw() is disabled for "
                          "Result<T, E&>.");
            static_assert(std::is_base_of_v<std::exception, std::remove_reference_t<E>>,
                          "unwrap_or_throw() requires E to derive from "
                          "std::exception.");

            throw detail::move_value(*this).storage_ref().take_err();
        }
    }
#endif

    template <typename Message, typename U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    constexpr void expect(const Message &message) const &
    {
        if (!is_ok())
            RESULT_ERROR(message);
    }

    template <typename Message>
    constexpr T expect(const Message &message) &&
    {
        if (!is_ok())
            RESULT_ERROR(message);

        if constexpr (!std::is_void_v<T>)
            return detail::move_value(*this).storage_ref().take_ok();
    }

    // ---------------------------------------------------------------------------------------------
    // map
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::ok_invoke_plan<T, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] constexpr auto map(Fn &&fn) &&
    {
        using RawReturn = Call::result_type;
        using NewOk = detail::map_output_t<RawReturn, T>;
        using Output = Result<NewOk, E>;

        // The Err branch keeps its type across map, so this propagation is always the identity.
        if (is_err())
            return detail::move_value(*this).template propagate_err_into<Output>();

        if constexpr (std::is_void_v<RawReturn>) {
            Call::apply(detail::move_value(*this).storage_ref(), detail::forward_value<Fn>(fn));
            return Output(Ok());
        } else {
            decltype(auto) transformed =
                Call::apply(detail::move_value(*this).storage_ref(), detail::forward_value<Fn>(fn));

            return Output(Ok(detail::forward_value<decltype(transformed)>(transformed)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // map_err
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::err_invoke_plan<E, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] constexpr auto map_err(Fn &&fn) &&
    {
        using RawReturn = Call::result_type;
        using NewErr = detail::map_output_t<RawReturn, E>;
        using Output = Result<T, NewErr>;

        // The Ok branch keeps its type across map_err, so this propagation is always the identity.
        if (is_ok())
            return detail::move_value(*this).template propagate_ok_into<Output>();

        if constexpr (std::is_void_v<RawReturn>) {
            Call::apply(detail::move_value(*this).storage_ref(), detail::forward_value<Fn>(fn));
            return Output(Err());
        } else {
            decltype(auto) transformed =
                Call::apply(detail::move_value(*this).storage_ref(), detail::forward_value<Fn>(fn));

            return Output(Err(detail::forward_value<decltype(transformed)>(transformed)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // map_or
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Fallback,
              typename Call = detail::ok_invoke_plan<T, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] constexpr decltype(auto) map_or(Fn &&fn, Fallback &&fallback) &&
    {
        using RawReturn = Call::result_type;

        static_assert(!std::is_void_v<RawReturn>,
                      "map_or requires a value-returning callback. Use map_or_else for void.");

        if constexpr (!std::is_void_v<RawReturn>) {
            using Output = detail::map_output_t<RawReturn, T>;
            using FallbackSource = detail::captured_type_t<Fallback &&>;

            static_assert(detail::resolve_plan<Output, FallbackSource>::legal,
                          "map_or fallback cannot be safely resolved to the callback output type.");

            if (is_ok()) {
                decltype(auto) transformed = Call::apply(detail::move_value(*this).storage_ref(),
                                                         detail::forward_value<Fn>(fn));
                return detail::resolve_expression<Output>(
                    detail::forward_value<decltype(transformed)>(transformed));
            }

            return detail::resolve_expression<Output>(detail::forward_value<Fallback>(fallback));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // map_or_else
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename FnOther,
              typename OkCall = detail::ok_invoke_plan<T, storage, Fn &&>,
              typename ErrCall = detail::err_invoke_plan<E, storage, FnOther &&>,
              std::enable_if_t<OkCall::legal && ErrCall::legal, int> = 0>
    [[nodiscard]] constexpr auto map_or_else(Fn &&fn, FnOther &&other) &&
    {
        using OkRet = OkCall::result_type;
        using ErrRet = ErrCall::result_type;
        using Common = detail::common_value_plan<OkRet, ErrRet>;

        static_assert(Common::legal,
                      "map_or_else callbacks must both return void or have a common value type.");

        if constexpr (std::is_void_v<OkRet> && std::is_void_v<ErrRet>) {
            if (is_ok()) {
                OkCall::apply(detail::move_value(*this).storage_ref(),
                              detail::forward_value<Fn>(fn));
            } else {
                ErrCall::apply(detail::move_value(*this).storage_ref(),
                               detail::forward_value<FnOther>(other));
            }
        } else {
            using Output = typename Common::type;

            if (is_ok())
                return Output(OkCall::apply(detail::move_value(*this).storage_ref(),
                                            detail::forward_value<Fn>(fn)));

            return Output(ErrCall::apply(detail::move_value(*this).storage_ref(),
                                         detail::forward_value<FnOther>(other)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // and_then
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::ok_invoke_plan<T, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] constexpr auto and_then(Fn &&fn) &&
    {
        using RawOutput = Call::result_type;
        using Output = detail::remove_cvref_t<RawOutput>;

        if constexpr (!detail::is_result_v<Output>) {
            static_assert(detail::is_result_v<Output>, "and_then callback must return a Result.");
        } else {
            static_assert(!std::is_reference_v<RawOutput>,
                          "and_then callback must return its Result by value.");

            if (is_err())
                return detail::move_value(*this).template propagate_err_into<Output>();

            return Output(Call::apply(detail::move_value(*this).storage_ref(),
                                      detail::forward_value<Fn>(fn)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // or_else
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::err_invoke_plan<E, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] constexpr auto or_else(Fn &&fn) &&
    {
        using RawOutput = Call::result_type;
        using Output = detail::remove_cvref_t<RawOutput>;

        if constexpr (!detail::is_result_v<Output>) {
            static_assert(detail::is_result_v<Output>, "or_else callback must return a Result.");
        } else {
            static_assert(!std::is_reference_v<RawOutput>,
                          "or_else callback must return its Result by value.");

            if (is_ok())
                return detail::move_value(*this).template propagate_ok_into<Output>();

            return Output(Call::apply(detail::move_value(*this).storage_ref(),
                                      detail::forward_value<Fn>(fn)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // inspect / inspect_err
    //
    // A side effect on whichever branch is present, changing neither type. The callback borrows -
    // it is handed a const reference, or nothing at all for a void branch - so the payload is
    // still there afterwards. The Result itself is returned by move, so an inspect in a chain
    // costs one payload move; C++ has no destructive move that would make it free.
    // ---------------------------------------------------------------------------------------------

    template <typename Fn,
              std::enable_if_t<std::is_void_v<T>
                                   ? std::is_invocable_v<Fn>
                                   : std::is_invocable_v<Fn, detail::const_ok_ref_t<storage>>,
                               int> = 0>
    [[nodiscard]] constexpr Result inspect(Fn &&fn) &&
    {
        if (is_ok()) {
            if constexpr (std::is_void_v<T>) {
                detail::invoke(detail::forward_value<Fn>(fn));
            } else {
                const Result &self = *this;
                detail::invoke(detail::forward_value<Fn>(fn), self.storage_ref().ok_ref());
            }
        }

        return detail::move_value(*this);
    }

    template <typename Fn,
              std::enable_if_t<std::is_void_v<E>
                                   ? std::is_invocable_v<Fn>
                                   : std::is_invocable_v<Fn, detail::const_err_ref_t<storage>>,
                               int> = 0>
    [[nodiscard]] constexpr Result inspect_err(Fn &&fn) &&
    {
        if (is_err()) {
            if constexpr (std::is_void_v<E>) {
                detail::invoke(detail::forward_value<Fn>(fn));
            } else {
                const Result &self = *this;
                detail::invoke(detail::forward_value<Fn>(fn), self.storage_ref().err_ref());
            }
        }

        return detail::move_value(*this);
    }

    // ---------------------------------------------------------------------------------------------
    // is_ok_and / is_err_and
    //
    // Borrowing predicates. These are the only combinators that do not consume, which is why
    // they are const & rather than && qualified.
    // ---------------------------------------------------------------------------------------------

    template <typename Fn,
              std::enable_if_t<std::is_void_v<T>
                                   ? std::is_invocable_v<Fn>
                                   : std::is_invocable_v<Fn, detail::const_ok_ref_t<storage>>,
                               int> = 0>
    [[nodiscard]] constexpr bool is_ok_and(Fn &&fn) const &
    {
        if (!is_ok())
            return false;

        if constexpr (std::is_void_v<T>) {
            return static_cast<bool>(detail::invoke(detail::forward_value<Fn>(fn)));
        } else {
            return static_cast<bool>(
                detail::invoke(detail::forward_value<Fn>(fn), storage_ref().ok_ref()));
        }
    }

    template <typename Fn,
              std::enable_if_t<std::is_void_v<E>
                                   ? std::is_invocable_v<Fn>
                                   : std::is_invocable_v<Fn, detail::const_err_ref_t<storage>>,
                               int> = 0>
    [[nodiscard]] constexpr bool is_err_and(Fn &&fn) const &
    {
        if (!is_err())
            return false;

        if constexpr (std::is_void_v<E>) {
            return static_cast<bool>(detail::invoke(detail::forward_value<Fn>(fn)));
        } else {
            return static_cast<bool>(
                detail::invoke(detail::forward_value<Fn>(fn), storage_ref().err_ref()));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // unwrap_or_else
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::err_invoke_plan<E, storage, Fn &&>,
              typename U = T, std::enable_if_t<Call::legal && !std::is_void_v<U>, int> = 0>
    [[nodiscard]] constexpr U unwrap_or_else(Fn &&fn) &&
    {
        using Recovered = detail::captured_type_t<typename Call::result_type &&>;

        static_assert(detail::resolve_plan<U, Recovered>::legal,
                      "unwrap_or_else callback result cannot be safely resolved to the Ok type.");

        if (is_ok())
            return detail::resolve_expression<U>(detail::move_value(*this).storage_ref().take_ok());

        return detail::resolve_expression<U>(
            Call::apply(detail::move_value(*this).storage_ref(), detail::forward_value<Fn>(fn)));
    }

    // ---------------------------------------------------------------------------------------------
    // expect_err
    // ---------------------------------------------------------------------------------------------

    template <typename Message, typename G = E, std::enable_if_t<std::is_void_v<G>, int> = 0>
    constexpr void expect_err(const Message &message) const &
    {
        if (!is_err())
            RESULT_ERROR(message);
    }

    template <typename Message>
    constexpr E expect_err(const Message &message) &&
    {
        if (!is_err())
            RESULT_ERROR(message);

        if constexpr (!std::is_void_v<E>)
            return detail::move_value(*this).storage_ref().take_err();
    }

    // ---------------------------------------------------------------------------------------------
    // flatten: Result<Result<U, E>, E> -> Result<U, E>
    //
    // The two Err types must be the same, for the reason propagation requires it everywhere else:
    // the inner error crosses out untouched, and nothing at this call site named a conversion.
    // ---------------------------------------------------------------------------------------------

    template <typename U = T, std::enable_if_t<detail::is_result_v<U>, int> = 0>
    [[nodiscard]] constexpr detail::remove_cvref_t<U> flatten() &&
    {
        using Inner = detail::remove_cvref_t<U>;

        static_assert(detail::branch_propagates_v<E, detail::result_err_t<Inner>>,
                      "flatten requires the outer and inner Err types to agree. Reach the target "
                      "type with map_err first.");

        if (is_err())
            return detail::move_value(*this).template propagate_err_into<Inner>();

        return detail::move_value(*this).storage_ref().take_ok();
    }

    // ---------------------------------------------------------------------------------------------
    // transpose: Result<optional<U>, E> -> optional<Result<U, E>>
    //
    //   Ok(engaged)   -> engaged holding Ok
    //   Ok(disengaged) -> disengaged
    //   Err(e)        -> engaged holding Err
    //
    // Which optional is produced comes from detail::optional_traits, so this follows whichever
    // optional the payload used rather than imposing one.
    // ---------------------------------------------------------------------------------------------

    template <typename U = T, std::enable_if_t<detail::optional_traits<U>::is_optional, int> = 0>
    [[nodiscard]] constexpr auto transpose() &&
    {
        using Traits = detail::optional_traits<U>;
        using Inner = Result<typename Traits::value_type, E>;
        using Output = typename Traits::template rebind<Inner>;

        if (is_err())
            return Output(detail::move_value(*this).template propagate_err_into<Inner>());

        auto held = detail::move_value(*this).storage_ref().take_ok();

        if (!held.has_value())
            return Output{};

        return Output(Inner(Ok(detail::move_value(*held))));
    }

    // ---------------------------------------------------------------------------------------------
    // Comparisons
    // ---------------------------------------------------------------------------------------------

    template <typename U>
    constexpr bool operator==(const wrapper::Ok<U> &ok) const
    {
        if (!is_ok())
            return false;

        if constexpr (std::is_void_v<T> || std::is_void_v<U>) {
            return std::is_void_v<T> && std::is_void_v<U>;
        } else {
            return storage_ref().ok_ref() == detail::payload_ref(ok);
        }
    }

    template <typename U>
    constexpr bool operator!=(const wrapper::Ok<U> &ok) const
    {
        return !(*this == ok);
    }

    template <typename G>
    constexpr bool operator==(const wrapper::Err<G> &err) const
    {
        if (!is_err())
            return false;

        if constexpr (std::is_void_v<E> || std::is_void_v<G>) {
            return std::is_void_v<E> && std::is_void_v<G>;
        } else {
            return storage_ref().err_ref() == detail::payload_ref(err);
        }
    }

    template <typename G>
    constexpr bool operator!=(const wrapper::Err<G> &err) const
    {
        return !(*this == err);
    }

    template <typename U, typename G>
    constexpr bool operator==(const Result<U, G> &other) const
    {
        if (is_ok() != other.is_ok())
            return false;

        if (is_ok()) {
            if constexpr (std::is_void_v<T> || std::is_void_v<U>) {
                return std::is_void_v<T> && std::is_void_v<U>;
            } else {
                return unwrap_ref() == other.unwrap_ref();
            }
        }

        if constexpr (std::is_void_v<E> || std::is_void_v<G>) {
            return std::is_void_v<E> && std::is_void_v<G>;
        } else {
            return unwrap_err_ref() == other.unwrap_err_ref();
        }
    }

    template <typename U, typename G>
    constexpr bool operator!=(const Result<U, G> &other) const
    {
        return !(*this == other);
    }
};

/* ------------------------------------------------------------------------------------------
   Error conversion for RESULT_TRY.

   RESULT_TRY crosses an error-type boundary the same way Rust's `?` does, and like `?` it
   requires the conversion to be written down. Relying on C++'s implicit conversions here
   would let an error type silently widen, change signedness, or decay through an unscoped
   enum's integral promotion at every propagation site, with no warning even under
   -Wconversion. So: specialize error_from, or it does not compile.

       RESULT_ERROR_CONVERSION(DriverError, PciError, { return DriverError::Bus; });

   which is shorthand for

       template <>
       struct lsr::error_from<DriverError, PciError> {
           static constexpr DriverError convert(PciError source) { return DriverError::Bus; }
       };

   Note the scope of this rule: it governs propagation, not construction. Writing
   `return Err(e);` by hand is still an ordinary conversion, because there you named the
   error at that site deliberately. The hole this closes is the silent one that spreads
   across many call sites.
   ------------------------------------------------------------------------------------------ */

template <typename To, typename From>
struct error_from;

template <typename E>
struct error_from<E, E> {
    static constexpr E convert(E source)
    {
        return static_cast<E &&>(source);
    }
};

template <typename To, typename From>
concept error_convertible_from = requires(From source) {
    { error_from<To, From>::convert(static_cast<From &&>(source)) } -> std::convertible_to<To>;
};

namespace detail {

/* RESULT_TRY does not know the enclosing function's error type, so propagation returns a
   proxy instead of a finished Result. The target type arrives when the proxy is converted,
   which is where error_from gets applied. */
template <typename E>
class [[nodiscard]] err_propagator {
    E m_error;

public:
    constexpr explicit err_propagator(E &&error)
        : m_error(static_cast<E &&>(error))
    {
    }

    template <typename T2, typename E2>
    constexpr operator RESULT_NAMESPACE::Result<T2, E2>() &&
    {
        static_assert(!std::is_void_v<E2>,
                      "Cannot propagate an error into a Result whose error type is void.");
        static_assert(RESULT_NAMESPACE::error_convertible_from<E2, E>,
                      "RESULT_TRY needs an explicit conversion between these error types. "
                      "Specialize lsr::error_from<Target, Source>, or use "
                      "RESULT_ERROR_CONVERSION(Target, Source, { ... }).");

        if constexpr (RESULT_NAMESPACE::error_convertible_from<E2, E>) {
            return RESULT_NAMESPACE::Err(
                RESULT_NAMESPACE::error_from<E2, E>::convert(static_cast<E &&>(m_error)));
        } else {
            return RESULT_NAMESPACE::Err(E2{});
        }
    }
};

class [[nodiscard]] void_err_propagator {
public:
    template <typename T2>
    constexpr operator RESULT_NAMESPACE::Result<T2, void>() && noexcept
    {
        return RESULT_NAMESPACE::Err();
    }
};

template <typename R>
[[nodiscard]] constexpr auto propagate_err(R &&source)
{
    using error_type = std::remove_cvref_t<R>::error_type;

    if constexpr (std::is_void_v<error_type>) {
        return void_err_propagator{};
    } else {
        return err_propagator<error_type>{static_cast<R &&>(source).unwrap_err()};
    }
}

}  // namespace detail

}  // namespace RESULT_NAMESPACE

// =================================================================================================
// Error propagation
//
// RESULT_TRY evaluates a fallible expression; if it holds an error, it returns from the
// *enclosing* function with that error, converted via error_from to the enclosing function's
// error type. Otherwise it yields the Ok payload. This is Rust's `?`.
//
// It cannot be a function. `?` returns on behalf of its caller, and no callee can do that.
// The function-shaped equivalent is and_then/map, already provided, and the better choice
// when the steps genuinely form a pipeline. RESULT_TRY is for when they do not - when you
// want ordinary imperative code in between fallible steps.
//
// Two arities, one name:
//
//     const int v = RESULT_TRY(read_reg(addr));    // expression: yields the payload
//     RESULT_TRY(enable_msi(dev));                 // statement:  payload discarded
//     RESULT_TRY(const auto v, read_reg(addr));    // declaration: binds v
//
// Pick the declaration form when either of these matters:
//
//   * Constant evaluation. The expression form is a statement expression, and GCC will not
//     constant-evaluate a return that jumps out of one, so at compile time it works on the
//     Ok path only. The declaration form has no statement expression and is fully constexpr.
//   * Strict conformance. Statement expressions are a GCC/Clang extension. (This header
//     already requires GCC or Clang, so it costs nothing here, but the declaration form uses
//     no extensions at all.)
//
// Macro caveat: arity is counted by commas, so a template argument list needs an extra pair
// of parentheses in either form - RESULT_TRY((make<A, B>())) and
// RESULT_TRY(decl, (make<A, B>())). Three or more arguments are diagnosed with that advice.
// Two are not: RESULT_TRY(make<A, B>()) is indistinguishable from the declaration form, and
// is read as one.
//
// Define RESULT_DEFINE_SHORT_TRY before including to also get the unprefixed spelling TRY.
// It is off by default because `TRY` is a popular macro name.
// =================================================================================================

#define RESULT_DETAIL_CAT_(a, b) a##b
#define RESULT_DETAIL_CAT(a, b)  RESULT_DETAIL_CAT_(a, b)

#define RESULT_DETAIL_PROPAGATE(tmp)                                          \
    do {                                                                      \
        if (!(tmp).is_ok())                                                   \
            return ::RESULT_NAMESPACE::detail::propagate_err(                 \
                static_cast<std::remove_reference_t<decltype(tmp)> &&>(tmp)); \
    } while (false)

// One argument: an expression yielding the Ok payload. __extension__ keeps the statement
// expression quiet under -Wpedantic. Usable as a statement too, which discards the payload.
#define RESULT_DETAIL_TRY_EXPR(...)                                                         \
    __extension__({                                                                         \
        auto &&result_try_tmp_ = (__VA_ARGS__);                                             \
        RESULT_DETAIL_PROPAGATE(result_try_tmp_);                                           \
        static_cast<std::remove_reference_t<decltype(result_try_tmp_)> &&>(result_try_tmp_) \
            .unwrap();                                                                      \
    })

// Two arguments: a declaration plus the expression to bind it to.
#define RESULT_DETAIL_TRY_DECL(decl, ...)                                                          \
    auto &&RESULT_DETAIL_CAT(result_try_at_, __LINE__) = (__VA_ARGS__);                            \
    RESULT_DETAIL_PROPAGATE(RESULT_DETAIL_CAT(result_try_at_, __LINE__));                          \
    decl = static_cast<                                                                            \
               std::remove_reference_t<decltype(RESULT_DETAIL_CAT(result_try_at_, __LINE__))> &&>( \
               RESULT_DETAIL_CAT(result_try_at_, __LINE__))                                        \
               .unwrap()

// A template argument list spends commas the preprocessor counts as arity, so three or four
// arguments almost always means an unparenthesised one. Without these sentinels the pick
// silently selects a fragment of the user's own expression as the macro to invoke, and the
// diagnostic points at a stray `>` instead of at the cause.
#define RESULT_DETAIL_TRY_TOO_MANY(...)                                                         \
    static_assert(false,                                                                        \
                  "RESULT_TRY takes one or two arguments, and macro arity is counted in "       \
                  "commas: a template argument list needs an extra pair of parentheses. Write " \
                  "RESULT_TRY((make<A, B>())) or RESULT_TRY(decl, (make<A, B>())).")

#define RESULT_DETAIL_TRY_PICK(_1, _2, _3, _4, chosen, ...) chosen

#define RESULT_TRY(...)                                                                         \
    RESULT_DETAIL_TRY_PICK(__VA_ARGS__, RESULT_DETAIL_TRY_TOO_MANY, RESULT_DETAIL_TRY_TOO_MANY, \
                           RESULT_DETAIL_TRY_DECL, RESULT_DETAIL_TRY_EXPR, )                    \
    (__VA_ARGS__)

// Writing the conversion RESULT_TRY requires. The incoming error is named `source`.
#define RESULT_ERROR_CONVERSION(To, From, ...)                                \
    template <>                                                               \
    struct RESULT_NAMESPACE::error_from<To, From> {                           \
        static constexpr To convert([[maybe_unused]] From source) __VA_ARGS__ \
    }

#ifdef RESULT_DEFINE_SHORT_TRY
#    define TRY RESULT_TRY
#endif

#ifdef RESULT_DETAIL_DEFINED_RESULT_ASSERT
#    undef RESULT_ASSERT
#    undef RESULT_DETAIL_DEFINED_RESULT_ASSERT
#endif

#ifdef RESULT_DETAIL_DEFINED_RESULT_ERROR
#    undef RESULT_ERROR
#    undef RESULT_DETAIL_DEFINED_RESULT_ERROR
#endif

// RESULT_NAMESPACE is deliberately left defined. It is the library's public configuration
// point, so generic user code can keep spelling RESULT_NAMESPACE::Result regardless of which
// name the namespace was given. RESULT_ERROR and RESULT_ASSERT above are internal and are
// torn down; this one is not.

#endif  // LRUSINGER_RESULT_INCLUDE_RESULT_RESULT_HPP_