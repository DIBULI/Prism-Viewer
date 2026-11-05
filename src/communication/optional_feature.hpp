#pragma once
#include <exception>
#include <string>
namespace prism_viewer {
// Only an explicit, consumed protocol rejection is an absent feature.
// Never turn a timeout, malformed reply, busy state or lost connection into it.
inline bool unsupportedFeature(const std::exception& error){
  const std::string text=error.what();
  return text=="agent error: unknown message type";
}
inline bool unsupportedNativeControl(const std::exception& error){
  return unsupportedFeature(error)||std::string(error.what())==
      "agent error: matched 415RSV0 control descriptor v4 required";
}
}
