// Cases that must NOT compile. One per REJECT_CASE value; each is built as its own
// target and registered with ctest as WILL_FAIL. Adding a case here means adding it to
// RESULT_REJECT_CASES in CMakeLists.txt.

#include <cstdint>
#include <result/result.hpp>
#include <string>
#include <string_view>
#include <utility>

#ifndef REJECT_CASE
#    error "Define REJECT_CASE to select a case."
#endif

using lsr::Err;
using lsr::Ok;
using lsr::Result;

enum class ErrorA {
    Bus
};
enum class ErrorB {
    Fatal
};
enum Unscoped {
    Promotes
};  // NOLINT: an unscoped enum is the point of case 5

struct Base {
    int tag;
};

struct Unrelated {
    int tag;
};

#if REJECT_CASE == 1
// Propagation must not convert the error it carries. int64_t -> int8_t silently
// truncated before, with no warning even under -Wconversion.
Result<double, std::int8_t> narrows_propagated_err(Result<int, std::int64_t> source)
{
    return std::move(source).and_then(
        [](int v) { return Result<double, std::int8_t>(Ok(v * 1.0)); });
}
#endif

#if REJECT_CASE == 2
// The same rule on the Ok branch of or_else.
Result<std::int8_t, ErrorB> narrows_propagated_ok(Result<std::int64_t, ErrorA> source)
{
    return std::move(source).or_else(
        [](ErrorA) { return Result<std::int8_t, ErrorB>(Ok(std::int8_t{0})); });
}
#endif

#if REJECT_CASE == 3
// Widening is refused too: RESULT_TRY already refuses it, so propagation must as well.
Result<double, std::int64_t> widens_propagated_err(Result<int, std::int16_t> source)
{
    return std::move(source).and_then(
        [](int v) { return Result<double, std::int64_t>(Ok(v * 1.0)); });
}
#endif

#if REJECT_CASE == 4
// A user-defined conversion behind a view would bind to the propagated temporary.
Result<double, std::string_view> dangles_propagated_err(Result<int, std::string> source)
{
    return std::move(source).and_then(
        [](int v) { return Result<double, std::string_view>(Ok(v * 1.0)); });
}
#endif

#if REJECT_CASE == 5
// An unscoped enum promoting to its underlying type is exactly what error_from exists
// to stop from happening silently.
Result<double, int> promotes_propagated_err(Result<int, Unscoped> source)
{
    return std::move(source).and_then([](int v) { return Result<double, int>(Ok(v * 1.0)); });
}
#endif

#if REJECT_CASE == 6
// An unrelated class type is not a base, so the reference is not bindable.
Result<double, Base> unrelated_propagated_err(Result<int, Unrelated> source)
{
    return std::move(source).and_then([](int v) { return Result<double, Base>(Ok(v * 1.0)); });
}
#endif

#if REJECT_CASE == 7
// A void Err branch carries nothing to build a real error from.
Result<int, ErrorA> void_err_into_real_err(Result<int, void> source)
{
    return std::move(source).and_then([](int v) { return Result<int, ErrorA>(Ok(v)); });
}
#endif

#if REJECT_CASE == 8
// A real error cannot be discarded into a void Err branch.
Result<int, void> real_err_into_void_err(Result<int, ErrorA> source)
{
    return std::move(source).and_then([](int v) { return Result<int, void>(Ok(v)); });
}
#endif

#if REJECT_CASE == 9
// cv-qualified payloads are rejected in Result itself, not deep inside the storage.
Result<const int, ErrorA> const_payload()
{
    return Ok(1);
}
#endif

#if REJECT_CASE == 10
Result<const int, void> const_payload_void_err()
{
    return Ok(1);
}
#endif

#if REJECT_CASE == 11
// A template argument list makes RESULT_TRY see three arguments.
Result<int, ErrorA> try_too_many_arguments(int a)
{
    RESULT_TRY(const int v, Result<int, ErrorA>(Ok(a)));
    return Ok(v);
}
#endif

#if REJECT_CASE == 12
// Crossing an error-type boundary without an error_from specialization.
Result<int, ErrorB> try_without_conversion(int a)
{
    auto read = [](int v) -> Result<int, ErrorA> { return Ok(v); };

    RESULT_TRY(const int v, read(a));
    return Ok(v);
}
#endif

#if REJECT_CASE == 13
// A borrowing accessor on a temporary would dangle.
int unwrap_ref_on_temporary()
{
    return Result<int, ErrorA>(Ok(1)).unwrap_ref();
}
#endif

#if REJECT_CASE == 14
// and_then must be handed a Result-returning callback.
auto and_then_without_result(Result<int, ErrorA> source)
{
    return std::move(source).and_then([](int v) { return v * 2; });
}
#endif

#if REJECT_CASE == 15
// Rvalue reference payloads are not supported.
Result<int &&, ErrorA> rvalue_reference_payload();
static_assert(sizeof(Result<int &&, ErrorA>) > 0);
#endif

int main()
{
    return 0;
}
