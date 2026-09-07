#pragma once

#include "communication/prism_runtime.hpp"

#include <cstddef>
#include <cstdint>

namespace prism_viewer::communication {

// SDK types are shared by direct linking and the Windows runtime API.
using RtkBaseSource = prism::RtkBaseSource;
using RtkSolution = prism::RtkSolution;
using RtkCorrectionFormat = prism::RtkCorrectionFormat;
using RtkConfidence = prism::RtkConfidence;
using RtkCorrectionStatus = prism::RtkCorrectionStatus;
using RtkNavigationStatus = prism::RtkNavigationStatus;
using RtkSmoothingFlag = prism::RtkSmoothingFlag;
using prism::RtkSmoothingDynamicsEnabled;
using prism::RtkSmoothingTransitionGated;
using prism::RtkSmoothingJumpGated;
using prism::RtkSmoothingResetTransition;
using prism::RtkSmoothingResetPositionJump;
using prism::RtkSmoothingResetEpochGap;
using prism::RtkSmoothingResetBaseSource;

RtkCorrectionStatus parseRtkCorrectionStatus(const prism::Frame& frame);
RtkCorrectionStatus beginRtkCorrections(prism_runtime::Client& client);
RtkCorrectionStatus sendRtkCorrections(prism_runtime::Client& client,
                                       const uint8_t* data, size_t size,
                                       uint32_t timeout_ms = 3000);
RtkCorrectionStatus endRtkCorrections(prism_runtime::Client& client);
RtkCorrectionStatus queryRtkCorrectionStatus(
    prism_runtime::Client& client);
RtkNavigationStatus parseViewerRtkNavigationStatus(const prism::Frame& frame);
bool isViewerRtkNavigationFrame(const prism::Frame& frame) noexcept;
RtkNavigationStatus queryRtkNavigationStatus(
    prism_runtime::Client& client);

const char* rtkSolutionName(RtkSolution solution);
const char* rtkBaseSourceName(RtkBaseSource source);
const char* rtkCorrectionFormatName(RtkCorrectionFormat format);
const char* rtkConfidenceName(RtkConfidence confidence);

}  // namespace prism_viewer::communication
