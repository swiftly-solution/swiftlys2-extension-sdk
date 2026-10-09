#include <expected>
#include <limits>
#include <numeric>
#include <print>
#include <string>

#define CHECK(condition) do { if (!(condition)) return 1; } while (false)

int main(int argc, char**)
{
    // Runtime input prevents the entire probe from becoming a constant expression.
    std::expected<int, std::string> value(argc);
    auto doubled = value.transform([](int n) { return n * 2; });
    CHECK(doubled && *doubled == argc * 2);
    std::expected<int, std::string> error = std::unexpected(std::string(128, 'x'));
    bool caught = false;
    try {
        (void)error.value();
    } catch (const std::bad_expected_access<std::string>& exception) {
        caught = exception.error() == std::string(128, 'x');
    }
    CHECK(caught);
    std::print("C++23 expected, exception and print probes passed: {}\n", *doubled);

#ifdef TEST_CPP26
#if !defined(__cpp_lib_saturation_arithmetic) || __cpp_lib_saturation_arithmetic < 202311L
#error "The C++26 probe requires saturation arithmetic support."
#endif
    CHECK(std::add_sat(std::numeric_limits<int>::max(), argc) == std::numeric_limits<int>::max());
    CHECK(std::sub_sat(std::numeric_limits<int>::min(), argc) == std::numeric_limits<int>::min());
    std::print("C++26 saturation arithmetic probes passed.\n");
#endif
}
