#pragma once

#include <optional>
#include <string_view>

namespace prism_viewer::imu_units {

enum class AccelerationUnit {
  MilliGravity,
  Gravity,
  MetresPerSecondSquared,
};

enum class AngularVelocityUnit {
  MilliDegreesPerSecond,
  DegreesPerSecond,
  RadiansPerSecond,
};

enum class TemperatureUnit {
  MilliCelsius,
  Celsius,
};

inline constexpr AccelerationUnit kDefaultAccelerationUnit =
    AccelerationUnit::Gravity;
inline constexpr AngularVelocityUnit kDefaultAngularVelocityUnit =
    AngularVelocityUnit::DegreesPerSecond;
inline constexpr TemperatureUnit kDefaultTemperatureUnit =
    TemperatureUnit::Celsius;

// Physical constants used by every conversion in this module.
inline constexpr double kStandardGravityMetresPerSecondSquared = 9.80665;
inline constexpr double kPi = 3.141592653589793238462643383279502884;
inline constexpr double kRadiansPerDegree = kPi / 180.0;

double convertAcceleration(double value, AccelerationUnit from,
                           AccelerationUnit to);
double convertAngularVelocity(double value, AngularVelocityUnit from,
                              AngularVelocityUnit to);
double convertTemperature(double value, TemperatureUnit from,
                          TemperatureUnit to);

// Tokens are deliberately ASCII and stable so callers can persist them.
// token() throws std::invalid_argument for an invalid enum value.
std::string_view token(AccelerationUnit unit);
std::string_view token(AngularVelocityUnit unit);
std::string_view token(TemperatureUnit unit);

std::optional<AccelerationUnit> parseAccelerationUnit(std::string_view value);
std::optional<AngularVelocityUnit> parseAngularVelocityUnit(
    std::string_view value);
std::optional<TemperatureUnit> parseTemperatureUnit(std::string_view value);

}  // namespace prism_viewer::imu_units
