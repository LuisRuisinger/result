// SPDX-License-Identifier: MIT

#ifndef LRUSINGER_RESULT_INCLUDE_RESULT_RESULT_HPP_
#define LRUSINGER_RESULT_INCLUDE_RESULT_RESULT_HPP_

#if !defined(__clang__) && !defined(__GNUC__)
#    error "Result requires GCC or Clang."
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
// Version
// =================================================================================================

#define CPP_RESULT_VERSION_MAJOR 0
#define CPP_RESULT_VERSION_MINOR 1
#define CPP_RESULT_VERSION_PATCH 0

#define CPP_RESULT_VERSION_STRING "0.1.1"

#define CPP_RESULT_VERSION_ENCODE(major__, minor__, patch__) \
    (((major__) * 10000) + ((minor__) * 100) + (patch__))

#define CPP_RESULT_VERSION                                                        \
    CPP_RESULT_VERSION_ENCODE(CPP_RESULT_VERSION_MAJOR, CPP_RESULT_VERSION_MINOR, \
                              CPP_RESULT_VERSION_PATCH)

#ifndef RESULT_ERROR
#    define RESULT_DETAIL_DEFINED_RESULT_ERROR 1
#    if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
#        define RESULT_ERROR(_m)  \
            do {                  \
                (void)sizeof(_m); \
                __builtin_trap(); \
            } while (0)
#    else
#        define RESULT_ERROR(_m)  \
            do {                  \
                (void)sizeof(_m); \
                std::terminate(); \
            } while (0)
#    endif
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

#ifdef RESULT_NAMESPACE
namespace lsr::result {
#endif

template <typename T, typename E>
class Result;

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

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename T>
struct remove_reference {
    using type = T;
};

template <typename T>
struct remove_reference<T &> {
    using type = T;
};

template <typename T>
struct remove_reference<T &&> {
    using type = T;
};

template <typename T>
using remove_reference_t = typename remove_reference<T>::type;

template <typename T>
constexpr remove_reference_t<T> &&move(T &&value) noexcept
{
    return static_cast<remove_reference_t<T> &&>(value);
}

template <typename T>
struct is_lvalue_reference {
    static constexpr bool value = false;
};

template <typename T>
struct is_lvalue_reference<T &> {
    static constexpr bool value = true;
};

template <typename T>
constexpr T &&forward(remove_reference_t<T> &value) noexcept
{
    return static_cast<T &&>(value);
}

template <typename T>
constexpr T &&forward(remove_reference_t<T> &&value) noexcept
{
    static_assert(!is_lvalue_reference<T>::value,
                  "forward cannot convert an rvalue into an lvalue");

    return static_cast<T &&>(value);
}
#else
using std::forward;
using std::move;
#endif

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

template <std::size_t N>
using byte_array = fixed_array<byte, N>;

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
    return ::new (static_cast<void *>(location)) T(forward<Args>(args)...);
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
decltype(auto) invoke_member_function(MemberPtr pointer, Obj &&object, Args &&...args)
{
    using class_type = typename member_pointer_class<MemberPtr>::type;

    if constexpr (std::is_base_of_v<class_type, std::remove_reference_t<Obj>>) {
        return (forward<Obj>(object).*pointer)(forward<Args>(args)...);
    } else {
        return ((*forward<Obj>(object)).*pointer)(forward<Args>(args)...);
    }
}

template <typename MemberPtr, typename Obj>
decltype(auto) invoke_member_object(MemberPtr pointer, Obj &&object)
{
    using class_type = typename member_pointer_class<MemberPtr>::type;

    if constexpr (std::is_base_of_v<class_type, std::remove_reference_t<Obj>>) {
        return forward<Obj>(object).*pointer;
    } else {
        return (*forward<Obj>(object)).*pointer;
    }
}
#endif

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename Fn, typename... Args>
decltype(auto) invoke(Fn &&fn, Args &&...args)
{
    using callable = std::remove_cvref_t<Fn>;

    if constexpr (std::is_member_function_pointer_v<callable>) {
        return invoke_member_function(forward<Fn>(fn), forward<Args>(args)...);
    } else if constexpr (std::is_member_object_pointer_v<callable>) {
        return invoke_member_object(forward<Fn>(fn), forward<Args>(args)...);
    } else {
        return forward<Fn>(fn)(forward<Args>(args)...);
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
    throw std::logic_error("optional has no value");
#else
    RESULT_ERROR("optional has no value");
#endif
}

[[noreturn]] inline void bad_sentinel_value_access()
{
#if RESULT_HAS_HOSTED_EXCEPTIONS
    throw std::logic_error("tiny::optional cannot store the reserved empty representation");
#else
    RESULT_ERROR("reserved empty representation");
#endif
}

}  // namespace detail

// =================================================================================================
// Wrapper types
// =================================================================================================

namespace wrapper {

template <typename T>
struct Ok {
    using value_type = T;

    explicit Ok(const T &value)
        : value(value)
    {
    }

    explicit Ok(T &&value)
        : value(detail::move(value))
    {
    }

    T value;
};

template <typename T>
struct Ok<T &> {
    using value_type = T &;

    explicit Ok(T &value) noexcept
        : value(detail::addressof(value))
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

    explicit Err(const E &value)
        : value(value)
    {
    }

    explicit Err(E &&value)
        : value(detail::move(value))
    {
    }

    E value;
};

template <typename E>
struct Err<E &> {
    using value_type = E &;

    explicit Err(E &value) noexcept
        : value(detail::addressof(value))
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
[[nodiscard]] static auto Ok(T &&value)
{
    using U = std::conditional_t<std::is_lvalue_reference_v<T>, T, std::decay_t<T>>;
    return wrapper::Ok<U>(detail::forward<T>(value));
}

template <typename E>
[[nodiscard]] static auto Err(E &&value)
{
    using U = std::conditional_t<std::is_lvalue_reference_v<E>, E, std::decay_t<E>>;
    return wrapper::Err<U>(detail::forward<E>(value));
}

[[nodiscard]] static auto Ok() noexcept
{
    return wrapper::Ok<void>{};
}

[[nodiscard]] static auto Err() noexcept
{
    return wrapper::Err<void>{};
}

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

// -------------------------------------------------------------------------------------------------
// Result traits
// -------------------------------------------------------------------------------------------------

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

template <typename X>
inline constexpr bool is_result_v = result_traits<remove_cvref_t<X>>::is_result;

template <typename X>
using result_ok_t = typename result_traits<remove_cvref_t<X>>::ok_type;

template <typename X>
using result_err_t = typename result_traits<remove_cvref_t<X>>::err_type;

// -------------------------------------------------------------------------------------------------
// Stored type resolution
// -------------------------------------------------------------------------------------------------

template <typename T>
struct stored_type {
    using type = T;
};

template <typename T>
struct stored_type<T &> {
    using type = T *;
};

template <typename T>
using stored_type_t = typename stored_type<T>::type;

// -------------------------------------------------------------------------------------------------
// Resolve plans
// -------------------------------------------------------------------------------------------------

enum class resolve_kind {
    INVALID,
    VOID,
    BIND_REF,
    CONSTRUCT_FROM_LVALUE,
    CONSTRUCT_FROM_RVALUE
};

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
    static constexpr bool         legal = std::is_void_v<To> && std::is_void_v<From>;
    static constexpr resolve_kind kind = legal ? resolve_kind::VOID : resolve_kind::INVALID;
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

    static constexpr resolve_kind kind = [] {
        if constexpr (!legal) {
            return resolve_kind::INVALID;
        } else if constexpr (binds_reference) {
            return resolve_kind::BIND_REF;
        } else if constexpr (std::is_lvalue_reference_v<From>) {
            return resolve_kind::CONSTRUCT_FROM_LVALUE;
        } else {
            return resolve_kind::CONSTRUCT_FROM_RVALUE;
        }
    }();

    static decltype(auto) apply(source_expr source)
    {
        static_assert(legal, "Attempted to apply an illegal Result resolve plan.");

        if constexpr (binds_reference) {
            return static_cast<To>(source);
        } else {
            return To(forward<source_expr>(source));
        }
    }
};

template <typename To, typename From>
struct resolve_plan : resolve_plan_impl<To, From> {};

template <typename To, typename Expr>
decltype(auto) resolve_expression(Expr &&expression)
{
    using From = captured_type_t<Expr &&>;
    using Plan = resolve_plan<To, From>;

    static_assert(Plan::legal, "Expression cannot be safely resolved to the requested type.");

    return Plan::apply(forward<Expr>(expression));
}

// -------------------------------------------------------------------------------------------------
// Wrapper payload access and conversion
// -------------------------------------------------------------------------------------------------

template <typename T>
decltype(auto) take_payload(wrapper::Ok<T> &&ok)
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return static_cast<T>(*ok.value);
    } else {
        return move(ok.value);
    }
}

template <typename E>
decltype(auto) take_payload(wrapper::Err<E> &&err)
{
    if constexpr (std::is_lvalue_reference_v<E>) {
        return static_cast<E>(*err.value);
    } else {
        return move(err.value);
    }
}

template <typename T>
decltype(auto) payload_ref(const wrapper::Ok<T> &ok)
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return *ok.value;
    } else {
        return (ok.value);
    }
}

template <typename E>
decltype(auto) payload_ref(const wrapper::Err<E> &err)
{
    if constexpr (std::is_lvalue_reference_v<E>) {
        return *err.value;
    } else {
        return (err.value);
    }
}

template <typename To, typename From, std::enable_if_t<resolve_plan<To, From>::legal, int> = 0>
wrapper::Ok<To> resolve_ok(wrapper::Ok<From> ok)
{
    if constexpr (std::is_void_v<To>) {
        return wrapper::Ok<void>{};
    } else {
        return wrapper::Ok<To>(resolve_plan<To, From>::apply(take_payload(move(ok))));
    }
}

template <typename To, typename From, std::enable_if_t<resolve_plan<To, From>::legal, int> = 0>
wrapper::Err<To> resolve_err(wrapper::Err<From> err)
{
    if constexpr (std::is_void_v<To>) {
        return wrapper::Err<void>{};
    } else {
        return wrapper::Err<To>(resolve_plan<To, From>::apply(take_payload(move(err))));
    }
}

// -------------------------------------------------------------------------------------------------
// Branch invocation plans
// -------------------------------------------------------------------------------------------------

template <bool Legal, typename Payload, typename Storage, typename Fn>
struct ok_invoke_plan_impl {
    static constexpr bool legal = false;
};

template <typename Payload, typename Storage, typename Fn>
struct ok_invoke_plan_impl<true, Payload, Storage, Fn> {
    static constexpr bool legal = true;

    using argument_type = decltype(declval<Storage &&>().take_ok());
    using result_type = std::invoke_result_t<Fn, argument_type>;

    static decltype(auto) apply(Storage &&storage, Fn fn)
    {
        return invoke(forward<Fn>(fn), move(storage).take_ok());
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

    static decltype(auto) apply(Storage &&, Fn fn)
    {
        return invoke(forward<Fn>(fn));
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

    static decltype(auto) apply(Storage &&storage, Fn fn)
    {
        return invoke(forward<Fn>(fn), move(storage).take_err());
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

    static decltype(auto) apply(Storage &&, Fn fn)
    {
        return invoke(forward<Fn>(fn));
    }
};

template <typename Storage, typename Fn>
struct err_invoke_plan<void, Storage, Fn>
    : err_invoke_plan_impl<std::is_invocable_v<Fn>, void, Storage, Fn> {};

// A reference returned from mapping an owned payload may refer into the
// consumed source Result. Preserve callback lvalue references only when the
// source payload itself is borrowed.
template <typename RawReturn, typename SourcePayload>
using map_output_t =
    std::conditional_t<std::is_void_v<RawReturn>, void,
                       std::conditional_t<std::is_lvalue_reference_v<RawReturn> &&
                                              std::is_lvalue_reference_v<SourcePayload>,
                                          RawReturn, std::decay_t<RawReturn>>>;

// -------------------------------------------------------------------------------------------------
// Branch propagation plans
// -------------------------------------------------------------------------------------------------

template <typename Src, typename Tgt, typename Storage, bool SourceVoid = std::is_void_v<Src>>
struct ok_propagation_plan;

template <typename Src, typename Tgt, typename Storage>
struct ok_propagation_plan<Src, Tgt, Storage, false> {
    using expression_type = decltype(declval<Storage &&>().take_ok());
    using captured_type = captured_type_t<expression_type>;

    static constexpr bool legal = resolve_plan<Tgt, captured_type>::legal;

    template <typename Output>
    static Output apply(Storage &&storage)
    {
        static_assert(legal, "Ok branch cannot be propagated to the target Result.");
        return Output(Ok(move(storage).take_ok()));
    }
};

template <typename Src, typename Tgt, typename Storage>
struct ok_propagation_plan<Src, Tgt, Storage, true> {
    static constexpr bool legal = resolve_plan<Tgt, void>::legal;

    template <typename Output>
    static Output apply(Storage &&)
    {
        static_assert(legal,
                      "Void Ok branch cannot be propagated to "
                      "the target Result.");
        return Output(Ok());
    }
};

template <typename Src, typename Tgt, typename Storage, bool SourceVoid = std::is_void_v<Src>>
struct err_propagation_plan;

template <typename Src, typename Tgt, typename Storage>
struct err_propagation_plan<Src, Tgt, Storage, false> {
    using expression_type = decltype(declval<Storage &&>().take_err());
    using captured_type = captured_type_t<expression_type>;

    static constexpr bool legal = resolve_plan<Tgt, captured_type>::legal;

    template <typename Output>
    static Output apply(Storage &&storage)
    {
        static_assert(legal,
                      "Err branch cannot be propagated to the "
                      "target Result.");
        return Output(Err(move(storage).take_err()));
    }
};

template <typename Src, typename Tgt, typename Storage>
struct err_propagation_plan<Src, Tgt, Storage, true> {
    static constexpr bool legal = resolve_plan<Tgt, void>::legal;

    template <typename Output>
    static Output apply(Storage &&)
    {
        static_assert(legal,
                      "Void Err branch cannot be propagated to "
                      "the target Result.");
        return Output(Err());
    }
};

// -------------------------------------------------------------------------------------------------
// Common value output for map_or_else
// -------------------------------------------------------------------------------------------------

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
// Exception
// =================================================================================================

#if RESULT_HAS_HOSTED_EXCEPTIONS
class bad_result_access : public std::exception {
public:
    [[nodiscard]] const char *what() const noexcept override
    {
        return "Bad Result access!";
    }
};
#endif

// =================================================================================================
// Optional storage
// =================================================================================================

namespace storage {

template <typename...>
inline constexpr bool always_false_v = false;

namespace detail {

template <typename T>
using unqualified_t = std::remove_cv_t<T>;

#if !defined(__STDC_HOSTED__) || !__STDC_HOSTED__
template <typename T>
constexpr void swap(T &lhs, T &rhs) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                             std::is_nothrow_move_assignable_v<T>)
{
    T temporary(move(lhs));
    lhs = move(rhs);
    rhs = move(temporary);
}
#else
using std::swap;
#endif

template <typename Representation>
constexpr auto representation_bytes(Representation representation) noexcept
{
    static_assert(std::is_trivially_copyable_v<Representation>);
    return std::bit_cast<fixed_array<byte, sizeof(Representation)>>(representation);
}

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
    for (auto del : delimiters) {
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
    static_cast<std::underlying_type_t<T>>(
        splitmix64(enum_type_hash<T> + static_cast<std::uint64_t>(I)));

template <typename T, std::size_t... Is>
constexpr std::underlying_type_t<T> find_hashed_enum_sentinel(index_sequence<Is...>)
{
    using underlying_type = std::underlying_type_t<T>;

    constexpr fixed_array<underlying_type, sizeof...(Is)> candidates = {
        hashed_enum_candidate<T, Is>...};

    constexpr fixed_array<bool, sizeof...(Is)> named = {
        is_named_enum_value<T, static_cast<T>(hashed_enum_candidate<T, Is>)>()...};

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!named[index])
            return candidates[index];
    }

    return candidates[0];
}

template <typename T, std::size_t... Is>
constexpr std::underlying_type_t<T> find_small_enum_sentinel(index_sequence<Is...>)
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
            return candidates[index];
    }

    return candidates[0];
}

template <typename E>
constexpr std::underlying_type_t<E> find_automatic_enum_sentinel()
{
    static_assert(std::is_enum_v<E>);

    using underlying_type = std::underlying_type_t<E>;

    if constexpr (sizeof(underlying_type) == 1U) {
        return find_small_enum_sentinel<E>(make_index_sequence<256>{});
    } else {
        constexpr std::size_t probe_count = 256;
        return find_hashed_enum_sentinel<E>(make_index_sequence<probe_count>{});
    }
}

}  // namespace detail

template <auto Representation>
struct sentinel_bits {
    using representation_type = decltype(Representation);
    static constexpr representation_type value = Representation;
};

/* Selects ordinary flag-backed storage. */
struct separate_flag_policy {};

template <typename T, typename Enable = void>
struct automatic_sentinel;

template <>
struct automatic_sentinel<bool> : sentinel_bits<std::uint8_t{0xfe}> {};

template <>
struct automatic_sentinel<float> : sentinel_bits<std::uint32_t{0x7fed'cba9U}> {
    static_assert(std::numeric_limits<float>::is_iec559);
};

template <>
struct automatic_sentinel<double> : sentinel_bits<std::uint64_t{0x7ff8'fedc'ba98'7654ULL}> {
    static_assert(std::numeric_limits<double>::is_iec559);
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

template <typename E>
struct automatic_sentinel<E, std::enable_if_t<std::is_enum_v<E>>> {
    using representation_type = std::underlying_type_t<E>;
    static constexpr representation_type value = detail::find_automatic_enum_sentinel<E>();

    static_assert(!detail::is_named_enum_value<E, static_cast<E>(value)>());
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

template <typename T, bool = has_automatic_sentinel<T>>
struct default_policy_selector {
    using type = separate_flag_policy;
};

template <typename T>
struct default_policy_selector<T, true> {
    using type = automatic_sentinel<detail::unqualified_t<T>>;
};

template <typename T>
using default_policy_t = typename default_policy_selector<T>::type;

namespace detail {

template <typename Policy, bool = sentinel_policy<Policy>>
struct sentinel_metadata {};

template <typename Policy>
struct sentinel_metadata<Policy, true> {
    using sentinel_representation_type = typename Policy::representation_type;
    inline static constexpr sentinel_representation_type sentinel_representation = Policy::value;
};

template <typename T, typename Policy>
class compressed_storage {
private:
    using representation_type = typename Policy::representation_type;

    static_assert(std::is_scalar_v<T>,
                  "Compressed storage currently supports scalar payloads only");
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_trivially_copyable_v<representation_type>);
    static_assert(std::has_unique_object_representations_v<representation_type>);
    static_assert(sizeof(representation_type) == sizeof(T),
                  "The sentinel representation must have the same size as T");

    inline static constexpr auto EMPTY_BYTES = representation_bytes(Policy::value);

    alignas(T) fixed_array<byte, sizeof(T)> m_storage{};

    [[nodiscard]] T *raw_ptr() noexcept
    {
        return reinterpret_cast<T *>(m_storage.data());
    }

    [[nodiscard]] const T *raw_ptr() const noexcept
    {
        return reinterpret_cast<const T *>(m_storage.data());
    }

    [[nodiscard]] T *ptr() noexcept
    {
        return std::launder(raw_ptr());
    }

    [[nodiscard]] const T *ptr() const noexcept
    {
        return std::launder(raw_ptr());
    }

    constexpr void write_empty() noexcept
    {
        m_storage = EMPTY_BYTES;
    }

    [[nodiscard]] static bool has_reserved_representation(const T &value) noexcept
    {
        fixed_array<byte, sizeof(T)> bytes{};
        memcpy(bytes.data(), addressof(value), sizeof(T));
        return bytes == EMPTY_BYTES;
    }

    void start_lifetime_from(const T &value) noexcept
    {
        // memcpy implicit lifetime start behavior
        memcpy(m_storage.data(), addressof(value), sizeof(T));
    }

public:
    inline static constexpr bool is_compressed = true;

    constexpr compressed_storage() noexcept
    {
        write_empty();
    }

    constexpr compressed_storage(nullopt_t) noexcept
        : compressed_storage()
    {
    }

    compressed_storage(const T &value)
        : compressed_storage()
    {
        emplace(value);
    }

    compressed_storage(T &&value)
        : compressed_storage()
    {
        emplace(move(value));
    }

    template <typename... Args>
    explicit compressed_storage(in_place_t, Args &&...args)
        : compressed_storage()
    {
        emplace(forward<Args>(args)...);
    }

    compressed_storage(const compressed_storage &other)
        : compressed_storage()
    {
        if (other.has_value())
            start_lifetime_from(*other);
    }

    compressed_storage(compressed_storage &&other) noexcept
        : compressed_storage()
    {
        if (other.has_value())
            start_lifetime_from(*other);
    }

    ~compressed_storage()
    {
        if (has_value())
            destroy_at(ptr());
    }

    compressed_storage &operator=(const compressed_storage &other)
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

    compressed_storage &operator=(compressed_storage &&other) noexcept
    {
        if (this != addressof(other)) {
            if (other.has_value()) {
                emplace(move(*other));
            } else {
                reset();
            }
        }

        return *this;
    }

    compressed_storage &operator=(nullopt_t) noexcept
    {
        reset();
        return *this;
    }

    compressed_storage &operator=(const T &value)
    {
        emplace(value);
        return *this;
    }

    compressed_storage &operator=(T &&value)
    {
        emplace(move(value));
        return *this;
    }

    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return m_storage != EMPTY_BYTES;
    }

    explicit constexpr operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] T &operator*() & noexcept
    {
        RESULT_ASSERT(has_value());
        return *ptr();
    }

    [[nodiscard]] const T &operator*() const & noexcept
    {
        RESULT_ASSERT(has_value());
        return *ptr();
    }

    [[nodiscard]] T &&operator*() && noexcept
    {
        RESULT_ASSERT(has_value());
        return move(*ptr());
    }

    [[nodiscard]] const T &&operator*() const && noexcept
    {
        RESULT_ASSERT(has_value());
        return move(*ptr());
    }

    [[nodiscard]] T *operator->() noexcept
    {
        RESULT_ASSERT(has_value());
        return ptr();
    }

    [[nodiscard]] const T *operator->() const noexcept
    {
        RESULT_ASSERT(has_value());
        return ptr();
    }

    [[nodiscard]] T &value() &
    {
        if (!has_value())
            optional_empty_access();

        return *ptr();
    }

    [[nodiscard]] const T &value() const &
    {
        if (!has_value())
            optional_empty_access();

        return *ptr();
    }

    [[nodiscard]] T &&value() &&
    {
        if (!has_value())
            optional_empty_access();

        return move(*ptr());
    }

    [[nodiscard]] const T &&value() const &&
    {
        if (!has_value())
            optional_empty_access();

        return move(*ptr());
    }

    template <typename U>
    [[nodiscard]] T value_or(U &&default_value) const &
    {
        return has_value() ? **this : static_cast<T>(forward<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] T value_or(U &&default_value) &&
    {
        return has_value() ? move(**this) : static_cast<T>(forward<U>(default_value));
    }

    template <typename... Args>
    T &emplace(Args &&...args)
    {
        T candidate(forward<Args>(args)...);

        if (has_reserved_representation(candidate))
            bad_sentinel_value_access();

        reset();
        start_lifetime_from(candidate);

        return *ptr();
    }

    constexpr void reset() noexcept
    {
        if (has_value())
            destroy_at(ptr());

        write_empty();
    }

    void swap(compressed_storage &other) noexcept(noexcept(detail::swap(**this, *other)))
    {
        if (has_value() && other.has_value()) {
            using detail::swap;
            swap(**this, *other);
        } else if (has_value()) {
            other.emplace(move(**this));
            reset();
        } else if (other.has_value()) {
            emplace(move(*other));
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

        ~storage_union()
        {
        }
    } m_storage;

    bool m_has_value = false;

    [[nodiscard]] constexpr T *ptr() noexcept
    {
        return std::launder(addressof(m_storage.value));
    }

    [[nodiscard]] constexpr const T *ptr() const noexcept
    {
        return std::launder(addressof(m_storage.value));
    }

public:
    inline static constexpr bool is_compressed = false;

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
        emplace(forward<U>(value));
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit constexpr separate_storage(in_place_t, Args &&...args)
        : separate_storage()
    {
        emplace(forward<Args>(args)...);
    }

    separate_storage(const separate_storage &other)
        requires std::copy_constructible<T>
        : separate_storage()
    {
        if (other.has_value())
            emplace(*other);
    }

    separate_storage(separate_storage &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires std::move_constructible<T>
        : separate_storage()
    {
        if (other.has_value())
            emplace(move(*other));
    }

    ~separate_storage()
    {
        reset();
    }

    separate_storage &operator=(const separate_storage &other)
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

    separate_storage &operator=(separate_storage &&other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        requires std::move_constructible<T>
    {
        if (this != addressof(other)) {
            if (other.has_value()) {
                emplace(move(*other));
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
        emplace(forward<U>(value));
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
        return move(*ptr());
    }

    [[nodiscard]] constexpr const T &&operator*() const && noexcept
    {
        RESULT_ASSERT(has_value());
        return move(*ptr());
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

        return move(*ptr());
    }

    [[nodiscard]] constexpr const T &&value() const &&
    {
        if (!has_value())
            optional_empty_access();

        return move(*ptr());
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) const &
    {
        return has_value() ? **this : static_cast<T>(forward<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) &&
    {
        return has_value() ? move(**this) : static_cast<T>(forward<U>(default_value));
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    constexpr T &emplace(Args &&...args)
    {
        reset();
        construct_at(addressof(m_storage.value), forward<Args>(args)...);
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
            using detail::swap;
            swap(**this, *other);
        } else if (has_value()) {
            other.emplace(move(**this));
            reset();
        } else if (other.has_value()) {
            emplace(move(*other));
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
                  "Policy must be a sentinel policy or tiny::separate_flag_policy");
    using type = separate_storage<T>;
};

}  // namespace detail

template <typename T, typename Policy = default_policy_t<T>>
class optional : public detail::sentinel_metadata<Policy> {
private:
    static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                  "tiny::optional requires an unqualified payload type");
    static_assert(std::is_object_v<T> && !std::is_array_v<T>,
                  "tiny::optional requires a non-array object type");
    static_assert(std::is_destructible_v<T>);

    using storage_type = typename detail::storage_selector<T, Policy>::type;
    storage_type m_storage;

public:
    using value_type = T;
    using policy_type = Policy;

    inline static constexpr bool uses_compressed_storage = storage_type::is_compressed;

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
        : m_storage(forward<U>(value))
    {
    }

    template <typename... Args>
        requires std::constructible_from<storage_type, in_place_t, Args...>
    explicit constexpr optional(in_place_t, Args &&...args)
        : m_storage(in_place, forward<Args>(args)...)
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
        requires requires(storage_type &storage, U &&value) { storage = forward<U>(value); } &&
                 (!std::same_as<std::remove_cvref_t<U>, optional>)
    constexpr optional &operator=(U &&value)
    {
        m_storage = forward<U>(value);
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
        return *move(m_storage);
    }

    [[nodiscard]] constexpr const T &&operator*() const && noexcept
    {
        return *move(m_storage);
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
        return move(m_storage).value();
    }

    [[nodiscard]] constexpr const T &&value() const &&
    {
        return move(m_storage).value();
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) const &
    {
        return m_storage.value_or(forward<U>(default_value));
    }

    template <typename U>
    [[nodiscard]] constexpr T value_or(U &&default_value) &&
    {
        return move(m_storage).value_or(forward<U>(default_value));
    }

    template <typename... Args>
        requires requires(storage_type &storage, Args &&...args) {
            storage.emplace(forward<Args>(args)...);
        }
    constexpr T &emplace(Args &&...args)
    {
        return m_storage.emplace(forward<Args>(args)...);
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

template <typename T, typename LeftPolicy, typename RightPolicy>
[[nodiscard]] constexpr bool operator==(const optional<T, LeftPolicy>  &lhs,
                                        const optional<T, RightPolicy> &rhs)
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

}  // namespace storage

// =================================================================================================
// Specialized storage, uniform interface
// =================================================================================================

template <typename T, typename E>
class result_storage {
    static_assert(!std::is_void_v<T> && !std::is_void_v<E>,
                  "Primary result_storage requires non-void T and E.");

    union data_union {
        wrapper::Ok<T>  ok;
        wrapper::Err<E> err;

        data_union() noexcept
        {
        }

        ~data_union()
        {
        }
    } m_data;

    bool m_ok;

    wrapper::Ok<T> &ok_state()
    {
        return m_data.ok;
    }

    const wrapper::Ok<T> &ok_state() const
    {
        return m_data.ok;
    }

    wrapper::Err<E> &err_state()
    {
        return m_data.err;
    }

    const wrapper::Err<E> &err_state() const
    {
        return m_data.err;
    }

    void destroy_active() noexcept
    {
        if (m_ok) {
            destroy_at(addressof(m_data.ok));
        } else {
            destroy_at(addressof(m_data.err));
        }
    }

public:
    template <typename U, std::enable_if_t<resolve_plan<T, U>::legal, int> = 0>
    explicit result_storage(wrapper::Ok<U> ok)
        : m_ok(true)
    {
        construct_at(addressof(m_data.ok), resolve_ok<T>(move(ok)));
    }

    template <typename G, std::enable_if_t<resolve_plan<E, G>::legal, int> = 0>
    explicit result_storage(wrapper::Err<G> err)
        : m_ok(false)
    {
        construct_at(addressof(m_data.err), resolve_err<E>(move(err)));
    }

    result_storage(const result_storage &other)
        requires std::copy_constructible<wrapper::Ok<T>> && std::copy_constructible<wrapper::Err<E>>
        : m_ok(other.m_ok)
    {
        if (m_ok) {
            construct_at(addressof(m_data.ok), other.ok_state());
        } else {
            construct_at(addressof(m_data.err), other.err_state());
        }
    }

    result_storage(result_storage &&other) noexcept(
        std::is_nothrow_move_constructible_v<wrapper::Ok<T>> &&
        std::is_nothrow_move_constructible_v<wrapper::Err<E>>)
        requires std::move_constructible<wrapper::Ok<T>> && std::move_constructible<wrapper::Err<E>>
        : m_ok(other.m_ok)
    {
        if (m_ok) {
            construct_at(addressof(m_data.ok), move(other.ok_state()));
        } else {
            construct_at(addressof(m_data.err), move(other.err_state()));
        }
    }

    ~result_storage()
    {
        destroy_active();
    }

    result_storage &operator=(const result_storage &other)
        requires std::copy_constructible<wrapper::Ok<T>> && std::copy_constructible<wrapper::Err<E>>
    {
        if (this != addressof(other)) {
            destroy_active();

            m_ok = other.m_ok;
            if (m_ok) {
                construct_at(addressof(m_data.ok), other.ok_state());
            } else {
                construct_at(addressof(m_data.err), other.err_state());
            }
        }

        return *this;
    }

    result_storage &operator=(result_storage &&other) noexcept(
        std::is_nothrow_move_constructible_v<wrapper::Ok<T>> &&
        std::is_nothrow_move_constructible_v<wrapper::Err<E>>)
        requires std::move_constructible<wrapper::Ok<T>> && std::move_constructible<wrapper::Err<E>>
    {
        if (this != addressof(other)) {
            destroy_active();

            m_ok = other.m_ok;
            if (m_ok) {
                construct_at(addressof(m_data.ok), move(other.ok_state()));
            } else {
                construct_at(addressof(m_data.err), move(other.err_state()));
            }
        }

        return *this;
    }

    [[nodiscard]] bool has_ok() const noexcept
    {
        return m_ok;
    }

    [[nodiscard]] bool has_err() const noexcept
    {
        return !m_ok;
    }

    decltype(auto) ok_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return *ok_state().value;
        } else {
            return (ok_state().value);
        }
    }

    decltype(auto) ok_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return *ok_state().value;
        } else {
            return static_cast<const T &>(ok_state().value);
        }
    }

    decltype(auto) err_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return *err_state().value;
        } else {
            return (err_state().value);
        }
    }

    decltype(auto) err_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return *err_state().value;
        } else {
            return static_cast<const E &>(err_state().value);
        }
    }

    T take_ok() &&
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return *ok_state().value;
        } else {
            return move(ok_state().value);
        }
    }

    E take_err() &&
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return *err_state().value;
        } else {
            return move(err_state().value);
        }
    }
};

template <typename T>
class result_storage<T, void> {
    static_assert(!std::is_void_v<T>, "Use result_storage<void, void>.");

    using stored = stored_type_t<T>;
    storage::optional<stored> m_ok;

public:
    template <typename U, std::enable_if_t<resolve_plan<T, U>::legal, int> = 0>
    explicit result_storage(wrapper::Ok<U> ok)
    {
        auto resolved = resolve_ok<T>(move(ok));

        if constexpr (std::is_lvalue_reference_v<T>) {
            m_ok = resolved.value;
        } else {
            m_ok = move(resolved.value);
        }

        RESULT_ASSERT(m_ok.has_value() && "Ok value equals the reserved empty representation.");
    }

    explicit result_storage(wrapper::Err<void>) noexcept
    {
    }

    result_storage(const result_storage &) = default;
    result_storage(result_storage &&) noexcept = default;
    result_storage &operator=(const result_storage &) = default;
    result_storage &operator=(result_storage &&) noexcept = default;

    [[nodiscard]] bool has_ok() const noexcept
    {
        return m_ok.has_value();
    }

    [[nodiscard]] bool has_err() const noexcept
    {
        return !m_ok.has_value();
    }

    decltype(auto) ok_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return **m_ok;
        } else {
            return (*m_ok);
        }
    }

    decltype(auto) ok_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return **m_ok;
        } else {
            return static_cast<const T &>(*m_ok);
        }
    }

    void_value err_ref() const noexcept
    {
        return {};
    }

    decltype(auto) take_ok() &&
    {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return **m_ok;
        } else {
            return move(*m_ok);
        }
    }

    void_value take_err() && noexcept
    {
        return {};
    }
};

template <typename E>
class result_storage<void, E> {
    static_assert(!std::is_void_v<E>, "Use result_storage<void, void>.");

    using stored = stored_type_t<E>;
    storage::optional<stored> m_err;

public:
    explicit result_storage(wrapper::Ok<void>) noexcept
    {
    }

    template <typename G, std::enable_if_t<resolve_plan<E, G>::legal, int> = 0>
    explicit result_storage(wrapper::Err<G> err)
    {
        auto resolved = resolve_err<E>(move(err));

        if constexpr (std::is_lvalue_reference_v<E>) {
            m_err = resolved.value;
        } else {
            m_err = move(resolved.value);
        }

        RESULT_ASSERT(m_err.has_value() && "Err value equals the reserved empty representation.");
    }

    result_storage(const result_storage &) = default;
    result_storage(result_storage &&) noexcept = default;
    result_storage &operator=(const result_storage &) = default;
    result_storage &operator=(result_storage &&) noexcept = default;

    [[nodiscard]] bool has_ok() const noexcept
    {
        return !m_err.has_value();
    }

    [[nodiscard]] bool has_err() const noexcept
    {
        return m_err.has_value();
    }

    void_value ok_ref() const noexcept
    {
        return {};
    }

    decltype(auto) err_ref() &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return **m_err;
        } else {
            return (*m_err);
        }
    }

    decltype(auto) err_ref() const &
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return **m_err;
        } else {
            return static_cast<const E &>(*m_err);
        }
    }

    void_value take_ok() && noexcept
    {
        return {};
    }

    decltype(auto) take_err() &&
    {
        if constexpr (std::is_lvalue_reference_v<E>) {
            return **m_err;
        } else {
            return move(*m_err);
        }
    }
};

template <>
class result_storage<void, void> {
    bool m_ok;

public:
    explicit result_storage(wrapper::Ok<void>) noexcept
        : m_ok(true)
    {
    }

    explicit result_storage(wrapper::Err<void>) noexcept
        : m_ok(false)
    {
    }

    result_storage(const result_storage &) = default;
    result_storage(result_storage &&) noexcept = default;
    result_storage &operator=(const result_storage &) = default;
    result_storage &operator=(result_storage &&) noexcept = default;

    [[nodiscard]] bool has_ok() const noexcept
    {
        return m_ok;
    }

    [[nodiscard]] bool has_err() const noexcept
    {
        return !m_ok;
    }

    void_value ok_ref() const noexcept
    {
        return {};
    }

    void_value err_ref() const noexcept
    {
        return {};
    }

    void_value take_ok() && noexcept
    {
        return {};
    }

    void_value take_err() && noexcept
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

    using storage = detail::result_storage<T, E>;

    storage &storage_ref() & noexcept
    {
        return static_cast<storage &>(*this);
    }

    const storage &storage_ref() const & noexcept
    {
        return static_cast<const storage &>(*this);
    }

    storage &&storage_ref() && noexcept
    {
        return static_cast<storage &&>(*this);
    }

public:
    using ok_type = T;
    using err_type = E;

    template <typename U, std::enable_if_t<detail::resolve_plan<T, U>::legal, int> = 0>
    Result(wrapper::Ok<U> ok)
        : storage(detail::move(ok))
    {
    }

    template <typename G, std::enable_if_t<detail::resolve_plan<E, G>::legal, int> = 0>
    Result(wrapper::Err<G> err)
        : storage(detail::move(err))
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

    [[nodiscard]] bool is_ok() const noexcept
    {
        return storage_ref().has_ok();
    }

    [[nodiscard]] bool is_err() const noexcept
    {
        return storage_ref().has_err();
    }

    // ---------------------------------------------------------------------------------------------
    // Borrowing access
    // ---------------------------------------------------------------------------------------------

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    decltype(auto) unwrap_ref() &
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap_ref a Result containing an error");

        return storage_ref().ok_ref();
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    decltype(auto) unwrap_ref() const &
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap_ref a Result containing an error");

        return storage_ref().ok_ref();
    }

    template <typename G = E, std::enable_if_t<!std::is_void_v<G>, int> = 0>
    decltype(auto) unwrap_err_ref() &
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err_ref an Ok Result");

        return storage_ref().err_ref();
    }

    template <typename G = E, std::enable_if_t<!std::is_void_v<G>, int> = 0>
    decltype(auto) unwrap_err_ref() const &
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err_ref an Ok Result");

        return storage_ref().err_ref();
    }

    // ---------------------------------------------------------------------------------------------
    // Consuming access
    // ---------------------------------------------------------------------------------------------

    template <typename U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    void unwrap() const &
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap a Result containing an error");
    }

    template <typename U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    void unwrap_unchecked() const & noexcept
    {
    }

    template <typename G = E, std::enable_if_t<std::is_void_v<G>, int> = 0>
    void unwrap_err() const &
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err an Ok Result");
    }

    template <typename G = E, std::enable_if_t<std::is_void_v<G>, int> = 0>
    void unwrap_err_unchecked() const & noexcept
    {
    }

    decltype(auto) unwrap() &&
    {
        if (!is_ok())
            RESULT_ERROR("Tried to unwrap a Result containing an error");

        if constexpr (!std::is_void_v<T>)
            return detail::move(*this).storage_ref().take_ok();
    }

    decltype(auto) unwrap_unchecked() &&
    {
        if constexpr (!std::is_void_v<T>)
            return detail::move(*this).storage_ref().take_ok();
    }

    decltype(auto) unwrap_err() &&
    {
        if (!is_err())
            RESULT_ERROR("Tried to unwrap_err an Ok Result");

        if constexpr (!std::is_void_v<E>)
            return detail::move(*this).storage_ref().take_err();
    }

    decltype(auto) unwrap_err_unchecked() &&
    {
        if constexpr (!std::is_void_v<E>)
            return detail::move(*this).storage_ref().take_err();
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U unwrap_or(U fallback) &&
    {
        if (is_ok())
            return detail::resolve_expression<U>(detail::move(*this).storage_ref().take_ok());

        if constexpr (std::is_lvalue_reference_v<U>) {
            return fallback;
        } else {
            return detail::move(fallback);
        }
    }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U> && !std::is_reference_v<U> &&
                                                   std::is_default_constructible_v<U>,
                                               int> = 0>
    U unwrap_or_default() &&
    {
        if (is_ok())
            return detail::resolve_expression<U>(detail::move(*this).storage_ref().take_ok());

        return U{};
    }

#if RESULT_HAS_HOSTED_EXCEPTIONS
    decltype(auto) unwrap_or_throw() &&
    {
        if (is_ok()) {
            if constexpr (std::is_void_v<T>) {
                return;
            } else {
                return detail::move(*this).storage_ref().take_ok();
            }
        }

        if constexpr (std::is_void_v<E>) {
            throw detail::bad_result_access{};
        } else {
            static_assert(!std::is_lvalue_reference_v<E>,
                          "unwrap_or_throw() is disabled for "
                          "Result<T, E&>.");
            static_assert(std::is_base_of_v<std::exception, std::remove_reference_t<E>>,
                          "unwrap_or_throw() requires E to derive from "
                          "std::exception.");

            throw detail::move(*this).storage_ref().take_err();
        }
    }
#endif

    template <typename Message, typename U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    void expect(const Message &message) const &
    {
        if (!is_ok())
            RESULT_ERROR(message);
    }

    template <typename Message>
    decltype(auto) expect(const Message &message) &&
    {
        if (!is_ok())
            RESULT_ERROR(message);

        if constexpr (!std::is_void_v<T>)
            return detail::move(*this).storage_ref().take_ok();
    }

    // ---------------------------------------------------------------------------------------------
    // map
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::ok_invoke_plan<T, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] auto map(Fn &&fn) &&
    {
        using RawReturn = typename Call::result_type;
        using NewOk = detail::map_output_t<RawReturn, T>;
        using Output = Result<NewOk, E>;
        using PropagateErr = detail::err_propagation_plan<E, E, storage>;

        static_assert(PropagateErr::legal, "map cannot propagate this Result's Err branch.");

        if (is_err())
            return PropagateErr::template apply<Output>(detail::move(*this).storage_ref());

        if constexpr (std::is_void_v<RawReturn>) {
            Call::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn));
            return Output(Ok());
        } else {
            decltype(auto) transformed =
                Call::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn));

            return Output(Ok(detail::forward<decltype(transformed)>(transformed)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // map_err
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::err_invoke_plan<E, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] auto map_err(Fn &&fn) &&
    {
        using RawReturn = typename Call::result_type;
        using NewErr = detail::map_output_t<RawReturn, E>;
        using Output = Result<T, NewErr>;
        using PropagateOk = detail::ok_propagation_plan<T, T, storage>;

        static_assert(PropagateOk::legal, "map_err cannot propagate this Result's Ok branch.");

        if (is_ok())
            return PropagateOk::template apply<Output>(detail::move(*this).storage_ref());

        if constexpr (std::is_void_v<RawReturn>) {
            Call::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn));
            return Output(Err());
        } else {
            decltype(auto) transformed =
                Call::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn));

            return Output(Err(detail::forward<decltype(transformed)>(transformed)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // map_or
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Fallback,
              typename Call = detail::ok_invoke_plan<T, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] decltype(auto) map_or(Fn &&fn, Fallback &&fallback) &&
    {
        using RawReturn = typename Call::result_type;

        static_assert(!std::is_void_v<RawReturn>,
                      "map_or requires a value-returning callback. Use "
                      "map_or_else for void.");

        if constexpr (!std::is_void_v<RawReturn>) {
            using Output = detail::map_output_t<RawReturn, T>;
            using FallbackSource = detail::captured_type_t<Fallback &&>;

            static_assert(detail::resolve_plan<Output, FallbackSource>::legal,
                          "map_or fallback cannot be safely resolved to "
                          "the callback output type.");

            if (is_ok()) {
                decltype(auto) transformed =
                    Call::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn));
                return detail::resolve_expression<Output>(
                    detail::forward<decltype(transformed)>(transformed));
            }

            return detail::resolve_expression<Output>(detail::forward<Fallback>(fallback));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // map_or_else
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename FnOther,
              typename OkCall = detail::ok_invoke_plan<T, storage, Fn &&>,
              typename ErrCall = detail::err_invoke_plan<E, storage, FnOther &&>,
              std::enable_if_t<OkCall::legal && ErrCall::legal, int> = 0>
    [[nodiscard]] auto map_or_else(Fn &&fn, FnOther &&other) &&
    {
        using OkRet = typename OkCall::result_type;
        using ErrRet = typename ErrCall::result_type;
        using Common = detail::common_value_plan<OkRet, ErrRet>;

        static_assert(Common::legal,
                      "map_or_else callbacks must both return void or "
                      "have a common value type.");

        if constexpr (std::is_void_v<OkRet> && std::is_void_v<ErrRet>) {
            if (is_ok()) {
                OkCall::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn));
            } else {
                ErrCall::apply(detail::move(*this).storage_ref(), detail::forward<FnOther>(other));
            }
        } else {
            using Output = typename Common::type;

            if (is_ok())
                return Output(
                    OkCall::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn)));

            return Output(
                ErrCall::apply(detail::move(*this).storage_ref(), detail::forward<FnOther>(other)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // and_then
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::ok_invoke_plan<T, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] auto and_then(Fn &&fn) &&
    {
        using RawOutput = typename Call::result_type;
        using Output = detail::remove_cvref_t<RawOutput>;

        if constexpr (!detail::is_result_v<Output>) {
            static_assert(detail::is_result_v<Output>, "and_then callback must return a Result.");
        } else {
            static_assert(!std::is_reference_v<RawOutput>,
                          "and_then callback must return its "
                          "Result by value.");

            using OutputError = detail::result_err_t<Output>;
            using PropagateErr = detail::err_propagation_plan<E, OutputError, storage>;

            static_assert(PropagateErr::legal,
                          "The source Err branch cannot be "
                          "propagated into the and_then output.");

            if (is_err())
                return PropagateErr::template apply<Output>(detail::move(*this).storage_ref());

            return Output(Call::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // or_else
    // ---------------------------------------------------------------------------------------------

    template <typename Fn, typename Call = detail::err_invoke_plan<E, storage, Fn &&>,
              std::enable_if_t<Call::legal, int> = 0>
    [[nodiscard]] auto or_else(Fn &&fn) &&
    {
        using RawOutput = typename Call::result_type;
        using Output = detail::remove_cvref_t<RawOutput>;

        if constexpr (!detail::is_result_v<Output>) {
            static_assert(detail::is_result_v<Output>, "or_else callback must return a Result.");
        } else {
            static_assert(!std::is_reference_v<RawOutput>,
                          "or_else callback must return its Result "
                          "by value.");

            using OutputOk = detail::result_ok_t<Output>;
            using PropagateOk = detail::ok_propagation_plan<T, OutputOk, storage>;

            static_assert(PropagateOk::legal,
                          "The source Ok branch cannot be "
                          "propagated into the or_else output.");

            if (is_ok())
                return PropagateOk::template apply<Output>(detail::move(*this).storage_ref());

            return Output(Call::apply(detail::move(*this).storage_ref(), detail::forward<Fn>(fn)));
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Comparisons
    // ---------------------------------------------------------------------------------------------

    template <typename U>
    bool operator==(const wrapper::Ok<U> &ok) const
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
    bool operator!=(const wrapper::Ok<U> &ok) const
    {
        return !(*this == ok);
    }

    template <typename G>
    bool operator==(const wrapper::Err<G> &err) const
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
    bool operator!=(const wrapper::Err<G> &err) const
    {
        return !(*this == err);
    }

    template <typename U, typename G>
    bool operator==(const Result<U, G> &other) const
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
    bool operator!=(const Result<U, G> &other) const
    {
        return !(*this == other);
    }
};

#ifdef RESULT_NAMESPACE
}  // namespace lsr::result
#endif

#if defined(RESULT_DETAIL_DEFINED_RESULT_ASSERT)
#    undef RESULT_ASSERT
#    undef RESULT_DETAIL_DEFINED_RESULT_ASSERT
#endif

#if defined(RESULT_DETAIL_DEFINED_RESULT_ERROR)
#    undef RESULT_ERROR
#    undef RESULT_DETAIL_DEFINED_RESULT_ERROR
#endif

#endif  // LRUSINGER_RESULT_INCLUDE_RESULT_RESULT_HPP_
