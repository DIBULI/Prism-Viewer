#include "imu_units.hpp"

#include <stdexcept>

namespace prism_viewer::imu_units {
namespace {

double accelerationScaleToSi(AccelerationUnit unit) {
  switch (unit) {
    case AccelerationUnit::MilliGravity:
      return kStandardGravityMetresPerSecondSquared / 1000.0;
    case AccelerationUnit::Gravity:
      return kStandardGravityMetresPerSecondSquared;
    case AccelerationUnit::MetresPerSecondSquared:
      return 1.0;
  }
  throw std::invalid_argument("invalid acceleration unit");
}

double angularVelocityScaleToSi(AngularVelocityUnit unit) {
  switch (unit) {
    case AngularVelocityUnit::MilliDegreesPerSecond:
      return kRadiansPerDegree / 1000.0;
    case AngularVelocityUnit::DegreesPerSecond:
      return kRadiansPerDegree;
    case AngularVelocityUnit::RadiansPerSecond:
      return 1.0;
  }
  throw std::invalid_argument("invalid angular velocity unit");
}

double temperatureScaleToCelsius(TemperatureUnit unit) {
  switch (unit) {
    case TemperatureUnit::MilliCelsius:
      return 1.0 / 1000.0;
    case TemperatureUnit::Celsius:
      return 1.0;
  }
  throw std::invalid_argument("invalid temperature unit");
}

}  // namespace

double convertAcceleration(double value, AccelerationUnit from,
                           AccelerationUnit to) {
  if (from == to) {
    // Validate the enum even on the identity path.
    static_cast<void>(accelerationScaleToSi(from));
    return value;
  }
  return value * accelerationScaleToSi(from) / accelerationScaleToSi(to);
}

double convertAngularVelocity(double value, AngularVelocityUnit from,
                              AngularVelocityUnit to) {
  if (from == to) {
    static_cast<void>(angularVelocityScaleToSi(from));
    return value;
  }
  return value * angularVelocityScaleToSi(from) /
         angularVelocityScaleToSi(to);
}

double convertTemperature(double value, TemperatureUnit from,
                          TemperatureUnit to) {
  if (from == to) {
    static_cast<void>(temperatureScaleToCelsius(from));
    return value;
  }
  return value * temperatureScaleToCelsius(from) /
         temperatureScaleToCelsius(to);
}

std::string_view token(AccelerationUnit unit) {
  switch (unit) {
    case AccelerationUnit::MilliGravity:
      return "mg";
    case AccelerationUnit::Gravity:
      return "g";
    case AccelerationUnit::MetresPerSecondSquared:
      return "m_s2";
  }
  throw std::invalid_argument("invalid acceleration unit");
}

std::string_view token(AngularVelocityUnit unit) {
  switch (unit) {
    case AngularVelocityUnit::MilliDegreesPerSecond:
      return "mdps";
    case AngularVelocityUnit::DegreesPerSecond:
      return "deg_s";
    case AngularVelocityUnit::RadiansPerSecond:
      return "rad_s";
  }
  throw std::invalid_argument("invalid angular velocity unit");
}

std::string_view token(TemperatureUnit unit) {
  switch (unit) {
    case TemperatureUnit::MilliCelsius:
      return "milli_c";
    case TemperatureUnit::Celsius:
      return "c";
  }
  throw std::invalid_argument("invalid temperature unit");
}

std::optional<AccelerationUnit> parseAccelerationUnit(std::string_view value) {
  if (value == "mg") return AccelerationUnit::MilliGravity;
  if (value == "g") return AccelerationUnit::Gravity;
  if (value == "m_s2") return AccelerationUnit::MetresPerSecondSquared;
  return std::nullopt;
}

std::optional<AngularVelocityUnit> parseAngularVelocityUnit(
    std::string_view value) {
  if (value == "mdps") return AngularVelocityUnit::MilliDegreesPerSecond;
  if (value == "deg_s") return AngularVelocityUnit::DegreesPerSecond;
  if (value == "rad_s") return AngularVelocityUnit::RadiansPerSecond;
  return std::nullopt;
}

std::optional<TemperatureUnit> parseTemperatureUnit(std::string_view value) {
  if (value == "milli_c") return TemperatureUnit::MilliCelsius;
  if (value == "c") return TemperatureUnit::Celsius;
  return std::nullopt;
}

}  // namespace prism_viewer::imu_units
