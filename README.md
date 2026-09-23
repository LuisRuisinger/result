# cpp_result

Single-header `Result<T, E>` for C++20 and onwards.
Requires GCC or Clang.

```cmake
include(FetchContent)
FetchContent_Declare(result
        GIT_REPOSITORY https://github.com/LuisRuisinger/cpp_result.git
        GIT_TAG        main) # more suitable would a release tag
FetchContent_MakeAvailable(result)
target_link_libraries(your_target PRIVATE lsr::result)
```

## Why

- A function's signature shows that it can fail and how, instead of hiding it in `errno` or a
  return code.
- `Result` is `[[nodiscard]]`, and you only get the value by handling potentinal the error.
- `map`, `map_err` and `and_then` pass errors up through layers without exceptions; Under this both the expected result and an potential error value cannot get lost and for each constructed path at compile time such values will be propagated. It is impossible to discard by accident an errornous state.

Compared with `std::expected`:

- Under certain circumstances tag bytes or other niche optimizations are being introduced at compiletime; E.g. it is possible to abuse non-zeroness of references or alignment of pointer values or unused enum values to store bool values. The result of such optimization is that under circumstances the same `Result<T, E>` fits in 8 bytes compared to 16 with `std::expected`; As such it can be retured by a register which would else happen through a memory address.
- It allows `void` and references on either side, e.g. `Result<Regs &, void>`.
- `unwrap()` on an error always executed a user-defined panic hook. On `std::expected`, `*e` is undefined
  behaviour.
- It needs only C++20, not C++23; Future work could backport it to C++14/17.
- Freestanding support

## Example

```cpp
struct I2c { volatile std::uint32_t ctrl, status, data; };

// constexpr enum probing finds values that are unused by the underlying type
enum class I2cError : std::uint8_t { Nack, Timeout };

Result<void, I2cError> wait_done(I2c &bus)
{
    for (int spin = 0; spin < 10'000; ++spin) {
        const std::uint32_t s = bus.status;
        
        if (s & NACK) 
            return Err(I2cError::Nack);
            
        if (s & DONE) 
            return Ok();
    }
    
    return Err(I2cError::Timeout);
}

Result<std::uint8_t, I2cError> read_reg(I2c &bus, std::uint8_t reg)
{
    bus.data = reg;
    bus.ctrl = START;
    
    return wait_done(bus).map([&] { 
        return static_cast<std::uint8_t>(bus.data); 
    });
}

auto temp = read_reg(bus, REG_TEMP);
if (temp.is_err())
    return fault(temp.unwrap_err_ref());
    
report(std::move(temp).unwrap());
```

## License

MIT
