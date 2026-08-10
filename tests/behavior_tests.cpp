// Behavioural tests: construction, borrowing, the combinators, branch propagation,
// constant evaluation, RESULT_TRY, and the payload move budget.
//
// The cases that must *not* compile live in reject_tests.cpp.

#include <cstdio>
#include <optional>
#include <result/result.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace {

enum class ErrorA {
    Bus,
    Timeout
};
enum class ErrorB {
    Fatal
};

}  // namespace

// RESULT_TRY refuses to cross an error-type boundary until the conversion is written down.
RESULT_ERROR_CONVERSION(ErrorB, ErrorA, { return ErrorB::Fatal; });

namespace {

using lsr::Err;
using lsr::Ok;
using lsr::Result;

int failures = 0;

// Variadic: the conditions below contain template argument lists and lambdas, so they
// reach the preprocessor as several arguments.
#define CHECK(...)                                                             \
    do {                                                                       \
        if (!(__VA_ARGS__)) {                                                  \
            ++failures;                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
        }                                                                      \
    } while (false)

// -------------------------------------------------------------------------------------------------
// Test types
// -------------------------------------------------------------------------------------------------

struct Base {
    int tag = 1;
};

struct Derived : Base {
    Derived() noexcept
    {
        tag = 2;
    }
};

// Counts every constructor so the move budget can be asserted.
struct Counted {
    static int moves;
    static int copies;
    static int values;

    int value = 0;

    explicit Counted(int input)
        : value(input)
    {
        ++values;
    }

    Counted(const Counted &other)
        : value(other.value)
    {
        ++copies;
    }

    Counted(Counted &&other) noexcept
        : value(other.value)
    {
        ++moves;
    }

    Counted &operator=(const Counted &) = default;
    Counted &operator=(Counted &&) noexcept = default;
    ~Counted() = default;

    static void reset() noexcept
    {
        moves = 0;
        copies = 0;
        values = 0;
    }
};

int Counted::moves = 0;
int Counted::copies = 0;
int Counted::values = 0;

// -------------------------------------------------------------------------------------------------
// Construction and state, across every void combination
// -------------------------------------------------------------------------------------------------

void test_states()
{
    Result<int, ErrorA> ok = Ok(7);
    CHECK(ok.is_ok());
    CHECK(!ok.is_err());
    CHECK(ok.unwrap_ref() == 7);
    CHECK(std::move(ok).unwrap() == 7);

    Result<int, ErrorA> err = Err(ErrorA::Bus);
    CHECK(err.is_err());
    CHECK(err.unwrap_err_ref() == ErrorA::Bus);

    Result<void, ErrorA> void_ok = Ok();
    CHECK(void_ok.is_ok());

    Result<void, ErrorA> void_err = Err(ErrorA::Timeout);
    CHECK(void_err.is_err());
    CHECK(void_err.unwrap_err_ref() == ErrorA::Timeout);

    Result<int, void> no_error = Ok(3);
    CHECK(no_error.is_ok());
    CHECK(std::move(no_error).unwrap() == 3);

    Result<int, void> no_error_failed = Err();
    CHECK(no_error_failed.is_err());

    Result<void, void> both_void_ok = Ok();
    CHECK(both_void_ok.is_ok());

    Result<void, void> both_void_err = Err();
    CHECK(both_void_err.is_err());
}

// -------------------------------------------------------------------------------------------------
// Reference payloads stay bound to the original object
// -------------------------------------------------------------------------------------------------

void test_references()
{
    int subject = 41;

    Result<int &, ErrorA> ref_ok = Ok(subject);
    CHECK(&ref_ok.unwrap_ref() == &subject);

    ref_ok.unwrap_ref() += 1;
    CHECK(subject == 42);

    ErrorA                error = ErrorA::Bus;
    Result<int, ErrorA &> ref_err = Err(error);
    CHECK(&ref_err.unwrap_err_ref() == &error);

    Result<int &, void> ref_no_error = Ok(subject);
    CHECK(&ref_no_error.unwrap_ref() == &subject);

    const std::string                   text = "borrowed";
    Result<const std::string &, ErrorA> const_ref = Ok(text);
    CHECK(&const_ref.unwrap_ref() == &text);
}

// -------------------------------------------------------------------------------------------------
// Combinators
// -------------------------------------------------------------------------------------------------

void test_map()
{
    auto doubled = Result<int, ErrorA>(Ok(20)).map([](int v) { return v * 2; });
    CHECK(doubled.unwrap_ref() == 40);

    // The Err branch is forwarded untouched.
    auto mapped_err = Result<int, ErrorA>(Err(ErrorA::Bus)).map([](int v) { return v * 2; });
    CHECK(mapped_err.is_err());
    CHECK(mapped_err.unwrap_err_ref() == ErrorA::Bus);

    // A void-returning callback collapses the Ok payload to void.
    int  observed = 0;
    auto to_void = Result<int, ErrorA>(Ok(5)).map([&observed](int v) { observed = v; });
    CHECK(observed == 5);
    CHECK(to_void.is_ok());
    static_assert(std::is_void_v<decltype(to_void)::value_type>);

    // map on a void Ok branch invokes with no argument.
    auto from_void = Result<void, ErrorA>(Ok()).map([] { return 9; });
    CHECK(from_void.unwrap_ref() == 9);
}

void test_map_err()
{
    auto changed =
        Result<int, ErrorA>(Err(ErrorA::Bus)).map_err([](ErrorA) { return ErrorB::Fatal; });
    CHECK(changed.unwrap_err_ref() == ErrorB::Fatal);

    // The Ok branch is forwarded untouched, across a change of error type.
    auto kept = Result<int, ErrorA>(Ok(4)).map_err([](ErrorA) { return ErrorB::Fatal; });
    CHECK(kept.is_ok());
    CHECK(kept.unwrap_ref() == 4);

    // A void error branch can be given a payload.
    auto given = Result<int, void>(Err()).map_err([] { return ErrorB::Fatal; });
    CHECK(given.unwrap_err_ref() == ErrorB::Fatal);
}

void test_map_or()
{
    CHECK(Result<int, ErrorA>(Ok(3)).map_or([](int v) { return v + 1; }, -1) == 4);
    CHECK(Result<int, ErrorA>(Err(ErrorA::Bus)).map_or([](int v) { return v + 1; }, -1) == -1);
}

void test_map_or_else()
{
    CHECK(Result<int, ErrorA>(Ok(3)).map_or_else([](int v) { return v + 1; },
                                                 [](ErrorA) { return -1; }) == 4);
    CHECK(Result<int, ErrorA>(Err(ErrorA::Bus))
              .map_or_else([](int v) { return v + 1; }, [](ErrorA) { return -1; }) == -1);

    int seen = 0;
    Result<int, ErrorA>(Ok(2)).map_or_else([&seen](int v) { seen = v; },
                                           [&seen](ErrorA) { seen = -1; });
    CHECK(seen == 2);
}

void test_and_then()
{
    auto chained = Result<int, ErrorA>(Ok(4)).and_then(
        [](int v) { return Result<double, ErrorA>(Ok(v * 1.5)); });
    CHECK(chained.unwrap_ref() == 6.0);

    // Identity propagation of the untouched Err branch.
    auto short_circuit = Result<int, ErrorA>(Err(ErrorA::Timeout)).and_then([](int v) {
        return Result<double, ErrorA>(Ok(v * 1.5));
    });
    CHECK(short_circuit.is_err());
    CHECK(short_circuit.unwrap_err_ref() == ErrorA::Timeout);

    // A non-trivial error type survives propagation without dangling.
    auto owning =
        Result<int, std::string>(Err(std::string("device fell off the bus"))).and_then([](int v) {
            return Result<double, std::string>(Ok(v * 1.0));
        });
    CHECK(owning.is_err());
    CHECK(owning.unwrap_err_ref() == "device fell off the bus");

    // Void branches propagate to matching void branches.
    auto void_err =
        Result<int, void>(Err()).and_then([](int v) { return Result<double, void>(Ok(v * 1.0)); });
    CHECK(void_err.is_err());

    auto void_ok = Result<void, ErrorA>(Ok()).and_then([] { return Result<int, ErrorA>(Ok(1)); });
    CHECK(void_ok.unwrap_ref() == 1);
}

void test_or_else()
{
    auto recovered = Result<int, ErrorA>(Err(ErrorA::Bus)).or_else([](ErrorA) {
        return Result<int, ErrorB>(Ok(8));
    });
    CHECK(recovered.unwrap_ref() == 8);

    // Identity propagation of the untouched Ok branch, across a change of error type.
    auto kept =
        Result<int, ErrorA>(Ok(5)).or_else([](ErrorA) { return Result<int, ErrorB>(Ok(0)); });
    CHECK(kept.is_ok());
    CHECK(kept.unwrap_ref() == 5);

    // A non-trivial Ok payload survives propagation without dangling.
    auto owning = Result<std::string, ErrorA>(Ok(std::string("kept intact"))).or_else([](ErrorA) {
        return Result<std::string, ErrorB>(Ok(std::string()));
    });
    CHECK(owning.is_ok());
    CHECK(owning.unwrap_ref() == "kept intact");
}

// -------------------------------------------------------------------------------------------------
// Propagation may adjust a pointer or reference to a base, and nothing else
// -------------------------------------------------------------------------------------------------

void test_propagation_upcast()
{
    Derived subject;

    auto ref_upcast = Result<Derived &, ErrorA>(Ok(subject)).or_else([](ErrorA) {
        return Result<Base &, ErrorB>(Err(ErrorB::Fatal));
    });
    CHECK(ref_upcast.is_ok());
    CHECK(&ref_upcast.unwrap_ref() == static_cast<Base *>(&subject));
    CHECK(ref_upcast.unwrap_ref().tag == 2);

    auto pointer_upcast = Result<Derived *, ErrorA>(Ok(&subject)).or_else([](ErrorA) {
        return Result<Base *, ErrorB>(Err(ErrorB::Fatal));
    });
    CHECK(pointer_upcast.is_ok());
    CHECK(pointer_upcast.unwrap_ref() == static_cast<Base *>(&subject));
}

// -------------------------------------------------------------------------------------------------
// Consuming access
// -------------------------------------------------------------------------------------------------

void test_unwrap_variants()
{
    CHECK(Result<int, ErrorA>(Ok(1)).unwrap_or(9) == 1);
    CHECK(Result<int, ErrorA>(Err(ErrorA::Bus)).unwrap_or(9) == 9);

    CHECK(Result<int, ErrorA>(Ok(1)).unwrap_or_default() == 1);
    CHECK(Result<int, ErrorA>(Err(ErrorA::Bus)).unwrap_or_default() == 0);

    CHECK(Result<int, ErrorA>(Ok(2)).unwrap_unchecked() == 2);
    CHECK(Result<int, ErrorA>(Err(ErrorA::Bus)).unwrap_err_unchecked() == ErrorA::Bus);

    CHECK(Result<int, ErrorA>(Ok(3)).expect("expected an Ok") == 3);
}

// -------------------------------------------------------------------------------------------------
// Comparisons
// -------------------------------------------------------------------------------------------------

void test_comparisons()
{
    const Result<int, ErrorA> ok = Ok(5);
    const Result<int, ErrorA> same = Ok(5);
    const Result<int, ErrorA> other = Ok(6);
    const Result<int, ErrorA> failed = Err(ErrorA::Bus);

    CHECK(ok == same);
    CHECK(ok != other);
    CHECK(ok != failed);
    CHECK(ok == Ok(5));
    CHECK(ok != Ok(6));
    CHECK(failed == Err(ErrorA::Bus));
    CHECK(failed != Err(ErrorA::Timeout));
}

// -------------------------------------------------------------------------------------------------
// Constant evaluation
// -------------------------------------------------------------------------------------------------

constexpr int constexpr_pipeline(int input)
{
    Result<int, ErrorA> source = Ok(input);
    return std::move(source).map([](int v) { return v * 2; }).unwrap_or(-1);
}

// RESULT_TRY returns from the enclosing function, so the enclosing function returns a Result.
// The declaration form carries no statement expression and is fully usable at compile time.
constexpr Result<int, ErrorA> constexpr_try(int input)
{
    auto step = [](int v) -> Result<int, ErrorA> {
        if (v < 0)
            return Err(ErrorA::Bus);

        return Ok(v + 1);
    };

    RESULT_TRY(const int first, step(input));
    RESULT_TRY(const int second, step(first));
    return Ok(second);
}

static_assert(constexpr_pipeline(21) == 42);
static_assert(constexpr_try(1).unwrap() == 3);
static_assert(constexpr_try(-5).unwrap_err() == ErrorA::Bus);

// Result stays a literal type where its payloads are.
static_assert(std::is_trivially_copyable_v<Result<int, ErrorA>>);
static_assert(std::is_trivially_destructible_v<Result<int, ErrorA>>);

// -------------------------------------------------------------------------------------------------
// RESULT_TRY
// -------------------------------------------------------------------------------------------------

Result<int, ErrorA> try_reads_two(int a, int b)
{
    auto read = [](int v) -> Result<int, ErrorA> {
        if (v == 0)
            return Err(ErrorA::Timeout);

        return Ok(v);
    };

    RESULT_TRY(const int first, read(a));
    RESULT_TRY(const int second, read(b));
    return Ok(first + second);
}

Result<void, ErrorA> try_statement_form(int a)
{
    auto check = [](int v) -> Result<void, ErrorA> {
        if (v < 0)
            return Err(ErrorA::Bus);

        return Ok();
    };

    RESULT_TRY(check(a));
    return Ok();
}

// Crossing an error-type boundary goes through error_from.
Result<int, ErrorB> try_converts(int a)
{
    auto read = [](int v) -> Result<int, ErrorA> {
        if (v == 0)
            return Err(ErrorA::Bus);

        return Ok(v);
    };

    RESULT_TRY(const int value, read(a));
    return Ok(value);
}

void test_try()
{
    CHECK(try_reads_two(2, 3).unwrap() == 5);
    CHECK(try_reads_two(0, 3).unwrap_err() == ErrorA::Timeout);
    CHECK(try_reads_two(2, 0).unwrap_err() == ErrorA::Timeout);

    CHECK(try_statement_form(1).is_ok());
    CHECK(try_statement_form(-1).unwrap_err() == ErrorA::Bus);

    CHECK(try_converts(4).unwrap() == 4);
    CHECK(try_converts(0).unwrap_err() == ErrorB::Fatal);
}

// -------------------------------------------------------------------------------------------------
// Payload move budget
//
// Upper bounds, not exact counts: they exist to catch a regression that quietly reintroduces
// a by-value hop through the wrapper plumbing.
// -------------------------------------------------------------------------------------------------

void test_move_budget()
{
    Counted::reset();
    {
        Result<Counted, ErrorA> owned = Ok(Counted{1});
        CHECK(owned.unwrap_ref().value == 1);
    }
    CHECK(Counted::copies == 0);
    CHECK(Counted::moves <= 3);
    if (Counted::moves > 3)
        std::printf("  Result<Counted, ErrorA> construction moves: %d\n", Counted::moves);

    Counted::reset();
    {
        Result<Counted, void> owned = Ok(Counted{1});
        CHECK(owned.unwrap_ref().value == 1);
    }
    CHECK(Counted::copies == 0);
    CHECK(Counted::moves <= 3);
    if (Counted::moves > 3)
        std::printf("  Result<Counted, void> construction moves: %d\n", Counted::moves);

    Counted::reset();
    {
        Result<Counted, ErrorA> source = Ok(Counted{2});
        const int               before = Counted::moves;
        auto                    mapped = std::move(source).map([](Counted c) { return c; });
        const int               crossing = Counted::moves - before;

        CHECK(mapped.unwrap_ref().value == 2);
        CHECK(crossing <= 5);
        if (crossing > 5)
            std::printf("  map(identity) moves: %d\n", crossing);
    }
    CHECK(Counted::copies == 0);

    // Propagating the Err branch must not touch the Ok payload at all.
    Counted::reset();
    {
        Result<Counted, ErrorA> failed = Err(ErrorA::Bus);
        const int               before = Counted::moves;
        auto                    mapped = std::move(failed).map([](Counted c) { return c; });
        CHECK(mapped.is_err());
        CHECK(Counted::moves == before);
    }
}

// -------------------------------------------------------------------------------------------------
// Copy and move of a Result whose two payloads are distinct non-trivial types
//
// This instantiates result_storage's non-trivial copy/move constructors. Nothing did before:
// construction goes through the converting constructor, and every combinator consumes via
// take_ok/take_err rather than moving the Result itself. Both constructors used a conditional
// expression over two construct_at calls, which only compiles when T and E coincide.
// -------------------------------------------------------------------------------------------------

void test_storage_copy_move()
{
    Result<std::string, Counted> ok = Ok(std::string("owned"));

    Result<std::string, Counted> moved = std::move(ok);
    CHECK(moved.is_ok());
    CHECK(moved.unwrap_ref() == "owned");

    Result<std::string, ErrorA> copyable = Ok(std::string("copied"));
    Result<std::string, ErrorA> copied = copyable;
    CHECK(copied.is_ok());
    CHECK(copied.unwrap_ref() == "copied");
    CHECK(copyable.unwrap_ref() == "copied");

    Result<std::string, ErrorA> failed = Err(ErrorA::Bus);
    Result<std::string, ErrorA> failed_copy = failed;
    CHECK(failed_copy.unwrap_err_ref() == ErrorA::Bus);

    Result<std::string, ErrorA> failed_move = std::move(failed);
    CHECK(failed_move.unwrap_err_ref() == ErrorA::Bus);
}

// -------------------------------------------------------------------------------------------------
// unwrap() must return by value for every T/E combination, never a reference into storage
// -------------------------------------------------------------------------------------------------

static_assert(
    std::is_same_v<decltype(std::declval<Result<std::string, ErrorA> &&>().unwrap()), std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<Result<std::string, void> &&>().unwrap()), std::string>);
static_assert(std::is_same_v<decltype(std::declval<Result<void, std::string> &&>().unwrap_err()),
                             std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<Result<std::string, ErrorA> &&>().unwrap_err()), ErrorA>);

// Reference payloads stay references.
static_assert(std::is_same_v<decltype(std::declval<Result<int &, void> &&>().unwrap()), int &>);
static_assert(std::is_same_v<decltype(std::declval<Result<int &, ErrorA> &&>().unwrap()), int &>);

// -------------------------------------------------------------------------------------------------
// inspect / inspect_err
// -------------------------------------------------------------------------------------------------

void test_inspect()
{
    int  seen = 0;
    auto kept = Result<int, ErrorA>(Ok(7)).inspect([&seen](const int &v) { seen = v; });
    CHECK(seen == 7);
    CHECK(kept.unwrap_ref() == 7);

    // Not invoked on the branch that is absent.
    seen = 0;
    auto skipped =
        Result<int, ErrorA>(Err(ErrorA::Bus)).inspect([&seen](const int &v) { seen = v; });
    CHECK(seen == 0);
    CHECK(skipped.is_err());

    std::string note;
    auto        observed = Result<int, std::string>(Err(std::string("bus")))
                        .inspect_err([&note](const std::string &e) { note = e; });
    CHECK(note == "bus");
    CHECK(observed.is_err());

    // A void branch is invoked with no argument.
    int  voids = 0;
    auto void_ok = Result<void, ErrorA>(Ok()).inspect([&voids] { ++voids; });
    CHECK(voids == 1);
    CHECK(void_ok.is_ok());

    // Chains, leaving the type untouched.
    auto chained =
        Result<int, ErrorA>(Ok(2)).inspect([](const int &) {}).map([](int v) { return v * 3; });
    CHECK(chained.unwrap_ref() == 6);
}

// -------------------------------------------------------------------------------------------------
// is_ok_and / is_err_and — the only borrowing combinators
// -------------------------------------------------------------------------------------------------

void test_predicates()
{
    const Result<int, ErrorA> ok = Ok(10);
    CHECK(ok.is_ok_and([](const int &v) { return v > 5; }));
    CHECK(!ok.is_ok_and([](const int &v) { return v > 50; }));
    CHECK(!ok.is_err_and([](const ErrorA &) { return true; }));
    CHECK(ok.unwrap_ref() == 10);  // still intact: these do not consume

    const Result<int, ErrorA> failed = Err(ErrorA::Timeout);
    CHECK(failed.is_err_and([](const ErrorA &e) { return e == ErrorA::Timeout; }));
    CHECK(!failed.is_ok_and([](const int &) { return true; }));

    const Result<void, ErrorA> void_ok = Ok();
    CHECK(void_ok.is_ok_and([] { return true; }));
}

// -------------------------------------------------------------------------------------------------
// unwrap_or_else / expect_err
// -------------------------------------------------------------------------------------------------

void test_recovery()
{
    CHECK(Result<int, ErrorA>(Ok(3)).unwrap_or_else([](ErrorA) { return -1; }) == 3);
    CHECK(Result<int, ErrorA>(Err(ErrorA::Bus)).unwrap_or_else([](ErrorA e) {
        return e == ErrorA::Bus ? 42 : 0;
    }) == 42);
    CHECK(Result<int, void>(Err()).unwrap_or_else([] { return 99; }) == 99);
    CHECK(Result<std::string, ErrorA>(Err(ErrorA::Bus)).unwrap_or_else([](ErrorA) {
        return std::string("fallback");
    }) == "fallback");

    CHECK(Result<int, ErrorA>(Err(ErrorA::Timeout)).expect_err("wanted an error") ==
          ErrorA::Timeout);
    Result<int, void>(Err()).expect_err("wanted an error");
}

// -------------------------------------------------------------------------------------------------
// flatten / transpose
// -------------------------------------------------------------------------------------------------

void test_flatten_transpose()
{
    CHECK(Result<Result<int, ErrorA>, ErrorA>(Ok(Result<int, ErrorA>(Ok(5)))).flatten().unwrap() ==
          5);
    CHECK(Result<Result<int, ErrorA>, ErrorA>(Ok(Result<int, ErrorA>(Err(ErrorA::Bus))))
              .flatten()
              .unwrap_err() == ErrorA::Bus);
    CHECK(Result<Result<int, ErrorA>, ErrorA>(Err(ErrorA::Timeout)).flatten().unwrap_err() ==
          ErrorA::Timeout);

    auto engaged = Result<std::optional<int>, ErrorA>(Ok(std::optional<int>(4))).transpose();
    CHECK(engaged.has_value());
    CHECK(engaged->unwrap_ref() == 4);
    static_assert(std::is_same_v<decltype(engaged), std::optional<Result<int, ErrorA>>>);

    auto disengaged = Result<std::optional<int>, ErrorA>(Ok(std::optional<int>{})).transpose();
    CHECK(!disengaged.has_value());

    auto errored = Result<std::optional<int>, ErrorA>(Err(ErrorA::Bus)).transpose();
    CHECK(errored.has_value());
    CHECK(errored->unwrap_err_ref() == ErrorA::Bus);

    auto owning = Result<std::optional<std::string>, ErrorA>(Ok(std::optional<std::string>("kept")))
                      .transpose();
    CHECK(owning.has_value());
    CHECK(owning->unwrap_ref() == "kept");
}

// Both reach constant evaluation.
constexpr int constexpr_flatten()
{
    Result<Result<int, ErrorA>, ErrorA> nested = Ok(Result<int, ErrorA>(Ok(11)));
    return std::move(nested).flatten().unwrap_or(-1);
}

constexpr int constexpr_or_else()
{
    Result<int, ErrorA> failed = Err(ErrorA::Bus);
    return std::move(failed).unwrap_or_else([](ErrorA) { return 8; });
}

static_assert(constexpr_flatten() == 11);
static_assert(constexpr_or_else() == 8);

// -------------------------------------------------------------------------------------------------
// Move budget for the paths that feed a payload onward
//
// These are the operations that used to materialise a second payload and move that into place.
// Bounds, not exact counts, but tight enough that reintroducing the extra object trips them.
// -------------------------------------------------------------------------------------------------

void test_forwarding_budget()
{
    const auto crossing = [](auto &&make, auto &&op) {
        Counted::reset();
        auto      source = make();
        const int before = Counted::moves;
        auto      out = op(std::move(source));
        (void)out;
        return Counted::moves - before;
    };

    const int propagated_err =
        crossing([] { return Result<int, Counted>(Err(Counted{1})); },
                 [](auto &&r) { return std::move(r).map([](int v) { return v; }); });
    CHECK(propagated_err <= 3);
    if (propagated_err > 3)
        std::printf("  propagate err through map: %d\n", propagated_err);

    const int propagated_ok = crossing(
        [] { return Result<Counted, ErrorA>(Ok(Counted{1})); },
        [](auto &&r) { return std::move(r).map_err([](ErrorA) { return ErrorB::Fatal; }); });
    CHECK(propagated_ok <= 3);
    if (propagated_ok > 3)
        std::printf("  propagate ok through map_err: %d\n", propagated_ok);

    const int chained = crossing([] { return Result<Counted, ErrorA>(Ok(Counted{1})); },
                                 [](auto &&r) {
                                     return std::move(r).and_then([](Counted c) {
                                         return Result<Counted, ErrorA>(Ok(std::move(c)));
                                     });
                                 });
    CHECK(chained <= 4);
    if (chained > 4)
        std::printf("  and_then(identity): %d\n", chained);

    // unwrap_or on the Ok path must not cost more than the one move out of storage.
    Counted::reset();
    {
        Result<Counted, ErrorA> r = Ok(Counted{5});
        const int               before = Counted::moves;
        Counted                 got = std::move(r).unwrap_or(Counted{9});
        CHECK(got.value == 5);
        const int cost = Counted::moves - before;
        CHECK(cost <= 1);
        if (cost > 1)
            std::printf("  unwrap_or(ok path): %d\n", cost);
    }
    CHECK(Counted::copies == 0);
}

}  // namespace

int main()
{
    test_states();
    test_references();
    test_map();
    test_map_err();
    test_map_or();
    test_map_or_else();
    test_and_then();
    test_or_else();
    test_propagation_upcast();
    test_unwrap_variants();
    test_comparisons();
    test_try();
    test_move_budget();
    test_forwarding_budget();
    test_storage_copy_move();
    test_inspect();
    test_predicates();
    test_recovery();
    test_flatten_transpose();

    if (failures == 0) {
        std::printf("behavior_tests: all checks passed\n");
        return 0;
    }

    std::printf("behavior_tests: %d check(s) failed\n", failures);
    return 1;
}
