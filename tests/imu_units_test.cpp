#include "imu_units.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using prism_viewer::imu_units::AccelerationUnit;
using prism_viewer::imu_units::AngularVelocityUnit;
using prism_viewer::imu_units::TemperatureUnit;
using prism_viewer::imu_units::convertAcceleration;
using prism_viewer::imu_units::convertAngularVelocity;
using prism_viewer::imu_units::convertTemperature;
using prism_viewer::imu_units::kPi;
using prism_viewer::imu_units::parseAccelerationUnit;
using prism_viewer::imu_units::parseAngularVelocityUnit;
using prism_viewer::imu_units::parseTemperatureUnit;
using prism_viewer::imu_units::token;

bool nearlyEqual(double actual, double expected) {
  constexpr double kTolerance = 1e-12;
  return std::abs(actual - expected) <=
         kTolerance * std::max({1.0, std::abs(actual), std::abs(expected)});
}

bool expectNear(double actual, double expected, const std::string& name) {
  if (nearlyEqual(actual, expected)) return true;
  std::cerr << name << ": expected " << expected << ", got " << actual
            << '\n';
  return false;
}

bool expect(bool condition, const std::string& name) {
  if (condition) return true;
  std::cerr << name << ": failed\n";
  return false;
}

template <typename Unit, size_t Size, typename Convert>
bool testConversionMatrix(const std::array<Unit, Size>& units,
                          const std::array<double, Size>& equivalents,
                          Convert convert, const char* dimension) {
  bool ok = true;
  for (size_t from = 0; from < units.size(); ++from) {
    for (size_t to = 0; to < units.size(); ++to) {
      const std::string name = std::string(dimension) + " " +
                               std::string(token(units[from])) + " to " +
                               std::string(token(units[to]));
      ok &= expectNear(convert(equivalents[from], units[from], units[to]),
                       equivalents[to], name);
    }
  }
  return ok;
}

template <typename Unit, size_t Size, typename Convert>
bool testZeroAndRoundTrips(const std::array<Unit, Size>& units,
                           Convert convert, const char* dimension) {
  bool ok = true;
  for (Unit from : units) {
    for (Unit to : units) {
      const std::string path = std::string(dimension) + " " +
                               std::string(token(from)) + " via " +
                               std::string(token(to));
      ok &= expectNear(convert(0.0, from, to), 0.0, path + " zero");
      constexpr double kInput = -123.456789;
      const double converted = convert(kInput, from, to);
      ok &= expectNear(convert(converted, to, from), kInput,
                       path + " negative round trip");
      if (from == to) {
        ok &= expect(converted == kInput, path + " identity");
      }
    }
  }
  return ok;
}

template <typename Unit, size_t Size, typename Parse>
bool testTokens(const std::array<Unit, Size>& units,
                const std::array<std::string_view, Size>& expected_tokens,
                Parse parse, const char* dimension) {
  bool ok = true;
  for (size_t index = 0; index < units.size(); ++index) {
    const std::string name = std::string(dimension) + " token " +
                             std::string(expected_tokens[index]);
    ok &= expect(token(units[index]) == expected_tokens[index],
                 name + " is stable");
    const std::optional<Unit> parsed = parse(expected_tokens[index]);
    ok &= expect(parsed.has_value() && *parsed == units[index],
                 name + " round trip");
  }
  ok &= expect(!parse("unknown").has_value(),
               std::string(dimension) + " rejects unknown token");
  ok &= expect(!parse(" G ").has_value(),
               std::string(dimension) + " rejects altered token");
  return ok;
}

}  // namespace

int main() {
  constexpr std::array kAccelerationUnits = {
      AccelerationUnit::MilliGravity, AccelerationUnit::Gravity,
      AccelerationUnit::MetresPerSecondSquared};
  constexpr std::array kAccelerationEquivalents = {1000.0, 1.0, 9.80665};
  constexpr std::array<std::string_view, 3> kAccelerationTokens = {
      "mg", "g", "m_s2"};

  constexpr std::array kAngularVelocityUnits = {
      AngularVelocityUnit::MilliDegreesPerSecond,
      AngularVelocityUnit::DegreesPerSecond,
      AngularVelocityUnit::RadiansPerSecond};
  constexpr std::array kAngularVelocityEquivalents = {180000.0, 180.0, kPi};
  constexpr std::array<std::string_view, 3> kAngularVelocityTokens = {
      "mdps", "deg_s", "rad_s"};

  constexpr std::array kTemperatureUnits = {
      TemperatureUnit::MilliCelsius, TemperatureUnit::Celsius};
  constexpr std::array kTemperatureEquivalents = {25375.0, 25.375};
  constexpr std::array<std::string_view, 2> kTemperatureTokens = {
      "milli_c", "c"};

  bool ok = true;
  ok &= testConversionMatrix(kAccelerationUnits, kAccelerationEquivalents,
                             convertAcceleration, "acceleration");
  ok &= testZeroAndRoundTrips(kAccelerationUnits, convertAcceleration,
                              "acceleration");
  ok &= expectNear(convertAcceleration(-500.0, AccelerationUnit::MilliGravity,
                                       AccelerationUnit::Gravity),
                   -0.5, "negative acceleration");
  ok &= testTokens(kAccelerationUnits, kAccelerationTokens,
                   parseAccelerationUnit, "acceleration");

  ok &= testConversionMatrix(kAngularVelocityUnits,
                             kAngularVelocityEquivalents,
                             convertAngularVelocity, "angular velocity");
  ok &= testZeroAndRoundTrips(kAngularVelocityUnits, convertAngularVelocity,
                              "angular velocity");
  ok &= expectNear(convertAngularVelocity(
                       -90000.0,
                       AngularVelocityUnit::MilliDegreesPerSecond,
                       AngularVelocityUnit::RadiansPerSecond),
                   -kPi / 2.0, "negative angular velocity");
  ok &= testTokens(kAngularVelocityUnits, kAngularVelocityTokens,
                   parseAngularVelocityUnit, "angular velocity");

  ok &= testConversionMatrix(kTemperatureUnits, kTemperatureEquivalents,
                             convertTemperature, "temperature");
  ok &= testZeroAndRoundTrips(kTemperatureUnits, convertTemperature,
                              "temperature");
  ok &= expectNear(convertTemperature(-40000.0,
                                      TemperatureUnit::MilliCelsius,
                                      TemperatureUnit::Celsius),
                   -40.0, "negative temperature");
  ok &= testTokens(kTemperatureUnits, kTemperatureTokens,
                   parseTemperatureUnit, "temperature");

  if (!ok) return EXIT_FAILURE;
  std::cout << "IMU unit conversion tests passed\n";
  return EXIT_SUCCESS;
}
