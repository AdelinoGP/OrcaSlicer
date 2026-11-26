#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/Utils.hpp"
#include "test_utils.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>

#include <string>

using namespace Slic3r;

TEST_CASE("Floats print as printf prints them in the C locale", "[LocalesUtils]")
{
    const auto [value, precision, text] = GENERATE(table<double, int, std::string>({
        {0.5, -1, "0.5"},
        {25. / 3., -1, "8.33333"},
        {1500.5, -1, "1500.5"},
        {1e6, -1, "1e+06"},
        {-0.000123, -1, "-0.000123"},
        {25. / 3., 3, "8.333"},
        {2., 0, "2"},
        // PNP fork (UpstreamSyncMap ticket 06): upstream's 1e21 row is dropped; this base's Windows
        // to_chars buffer is 20 bytes, so that value throws here (a separate buffer-size change).
    }));
    CHECK(float_to_string_decimal_point(value, precision) == text);
}

TEST_CASE("Floats print with a decimal point in a locale whose decimal separator is a comma", "[LocalesUtils]")
{
    CommaNumericLocale comma;
    {
        CNumericLocalesSetter outer;
        if (! comma.apply())
            SKIP("no locale with a comma decimal separator is installed");
        CHECK(float_to_string_decimal_point(1500.5) == "1500.5");
        CHECK(float_to_string_decimal_point(25. / 3., 3) == "8.333");
    }
}

TEST_CASE("a setter nested in another leaves the C locale in place for the outer one", "[LocalesUtils]")
{
    CNumericLocalesSetter outer;
    {
        CNumericLocalesSetter inner;
        CHECK(is_decimal_separator_point());
    }
    CHECK(is_decimal_separator_point());
}

TEST_CASE("a setter nested in another sets C again when the locale changed between them", "[LocalesUtils]")
{
    CommaNumericLocale comma;
    {
        CNumericLocalesSetter outer;
        if (! comma.apply())
            SKIP("no locale with a comma decimal separator is installed");
        REQUIRE_FALSE(is_decimal_separator_point());
        {
            CNumericLocalesSetter inner;
            CHECK(is_decimal_separator_point());
        }
        CHECK_FALSE(is_decimal_separator_point());
    }
}

// PNP fork (UpstreamSyncMap ticket 06): upstream's atof_decimal_point test is dropped; that helper
// was removed here with GCode/, and string_to_double_decimal_point is the parser this base uses.

TEST_CASE("Nozzle diameter strings read back in a locale whose decimal separator is a comma", "[LocalesUtils]")
{
    CommaNumericLocale comma;
    {
        CNumericLocalesSetter outer;
        if (! comma.apply())
            SKIP("no locale with a comma decimal separator is installed");
        const std::string diameter = format_diameter_to_str(0.4);
        CHECK(diameter == "0.4");
        CHECK_THAT(string_to_double_decimal_point(diameter), Catch::Matchers::WithinAbs(0.4, 1e-12));
        CHECK_THAT(boost::lexical_cast<double>(diameter), Catch::Matchers::WithinAbs(0.4, 1e-12));
        CHECK_THAT(boost::lexical_cast<float>(diameter), Catch::Matchers::WithinAbs(0.4, 1e-6));
        // A comma is not read as the decimal separator.
        CHECK_THAT(string_to_double_decimal_point("0,4"), Catch::Matchers::WithinAbs(0., 1e-12));
        // Callers that skip a diameter they cannot read rely on lexical_cast throwing for it.
        for (const char* text : { "", "abc", "0.4x", "0,4", "1e1000" }) {
            INFO(text);
            CHECK_THROWS_AS(boost::lexical_cast<float>(text), boost::bad_lexical_cast);
        }
    }
}
