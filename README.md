# cpp_result

A Rust-inspired `Result<T, E>` type for C++20 and later.

## Features

- `Ok<T>` / `Err<E>` result values
- `void` specializations
- lvalue reference support
- functional chaining via `map`, `map_err`, `and_then`, and `or_else`
- niche optimization for selected `Result<T, void>` / `Result<void, E>` cases
- freestanding mode via `RESULT_FREESTANDING`

## Requirements

C++20 or later.

## Freestanding

Define `RESULT_FREESTANDING` before including the header when building for a
kernel or another freestanding target:

```cpp
#define RESULT_FREESTANDING
#include <result/result.hpp>
```

In this mode the header avoids hosted-only dependencies such as `<iostream>`,
`<stdexcept>`, `<variant>`, and exception-based APIs. Error paths use
`RESULT_ERROR(message)`, and assertions use `RESULT_ASSERT(condition)`. Define
those macros before including the header if your kernel has its own panic or
assert implementation.

`unwrap_or_throw()` is only available for hosted builds with exceptions enabled.
The `multi_optional` hosted `std::variant` fallback is disabled in freestanding
mode, so its alternatives must share an implicitly discoverable niche.

## Installation

```cmake
include(FetchContent)

FetchContent_Declare(
        result
        GIT_REPOSITORY https://github.com/LuisRuisinger/cpp_result.git
        GIT_TAG v0.1.0
)

FetchContent_MakeAvailable(result)

target_link_libraries(your_target PRIVATE lsr::result)
```

Include:

```cpp
#include <result/result.hpp>
```

## Basic Usage

```cpp
#include <iostream>
#include <string>

#include <result/result.hpp>

using namespace lsr::result;

Result<float, std::string> divide(int a, int b) {
    if (b == 0)
        return Err(std::string("division by zero"));

    return Ok(static_cast<float>(a) / b);
}

int main() {
    auto result = divide(10, 2);

    if (result.is_ok())
        std::cout << result.unwrap_ref() << '\n';

    return 0;
}
```

## Notes

Consuming methods such as `unwrap`, `unwrap_err`, `map`, and `and_then` are `&&`-qualified:

```cpp
auto value = std::move(result).unwrap();
```

Inspection methods such as `is_ok`, `is_err`, `unwrap_ref`, and `unwrap_err_ref` do not consume the result.

Headers under `result/detail/` are implementation details and are not part of the public API.

## License

MIT. See [LICENSE](LICENSE).
