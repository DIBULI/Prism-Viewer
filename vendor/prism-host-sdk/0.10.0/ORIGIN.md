# Prism Host SDK 0.10.0 provenance

All public headers and both runtime libraries in this directory were built from
Prism-agent source commit
`829fae77104cf01be896e4fef5f0aec646f4bbe8` (`feat(time): unify sensor
streams on RK realtime`). This release exposes Runtime API v3 and the v2 LiDAR
point-batch timestamp fields; it must not be mixed with a 0.9 Agent or runtime.

## Public headers

- Source: `prism-sdk/usb-sdk/include` at Agent commit `829fae77104cf01be896e4fef5f0aec646f4bbe8`
- The vendored include tree is an exact byte-for-byte copy of that source tree.

## Linux x86-64 runtime

- Build environment: Ubuntu 22.04 Docker image, GCC 11.4, Release
- Host SDK CTest: 6/6 passed
- File: `runtime/linux-x64/libprism_usb_sdk.so`
- SHA-256: `e6512bf415007ed67d0c9703602f898e589997ba6d653eb02cee78ece625409e`

## Windows x86-64 runtime

- Build environment: GitHub Actions `windows-2022`, MSVC x64, Release
- Host SDK CTest: 6/6 passed before the workflow published the DLL
- Temporary workflow commit: `87f619951377d3052fe4d568bac33a16426940e6`
- Build-result commit: `bc3d7f2d4a15d0537c2390fe6282cb9bb68e7314`
- File: `runtime/windows-x64/prism_usb_sdk.dll`
- SHA-256: `1d92ac9a1f60f85e71bb16d9e55968c47d8bd113dbc679d9b07f5e18ef1aec92`

The one-purpose build branch contained only the frozen Agent source, the
workflow, and its temporary `ci-output` result. The runtime copied here is the
same file recorded by the build-result commit.
