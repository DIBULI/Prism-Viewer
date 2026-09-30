#pragma once
#include "prism/usb/camera_assembler.hpp"
namespace prism_viewer::transfer {
using CameraFrameSet = prism::capture::CameraFrameSet;
using CameraTransferState = prism::capture::CameraTransferState;
using CameraTransferProgress = prism::capture::CameraTransferProgress;
using CameraFrameDiagnostic = prism::capture::CameraFrameDiagnostic;
using CameraChunkResult = prism::capture::CameraChunkResult;
using CameraFrameAssembler = prism::capture::CameraFrameAssembler;
}
