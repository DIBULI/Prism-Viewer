# Prism Host SDK 0.9.0 binary provenance

These public headers and x86-64 shared libraries are consumed by Prism Viewer
without checking out or compiling the Prism-agent repository.

- Source repository: `DIBULI/Prism-agent`
- Public headers and Windows runtime source commit:
  `26a601b8fc7629673d690ef93a0def43b78574b4`
- Linux runtime source commit:
  `1c3f70252077ede6a49fc3985a2bd038347ad655`
- The two source commits above have the same Agent/SDK tree
  (`e50849459cdac6515fbe7bdd819ba539ddb2feda`); their commit IDs differ only
  because the feature patch was applied separately to the build server.
- Windows build commit: `efb722dae30a130a7ce8027b35efc69b5a39cf95`
  (the source commit above plus the temporary Actions workflow only)
- Windows build: `DIBULI/Prism-agent` Actions run
  [`31416581309`](https://github.com/DIBULI/Prism-agent/actions/runs/31416581309),
  GitHub `windows-2022`, MSVC x64, Release DLL runtime; SDK tests 6/6
- Linux build: Ubuntu 24.04, GCC 13.3 x86-64, Release; SDK tests 6/6

The temporary Windows build branch and workflow are removed after downloading
and verifying the artifact. The Actions run remains the immutable build record.

SHA-256:

```text
5e88232f3117515689f7ab2be14c94b33a8d9be0b874387eb3b03e98d2a73b83  runtime/windows-x64/prism_usb_sdk.dll
5b1092f62b24c7fd36456de337dac078208b2134e278a559a17ffd65460667dc  runtime/linux-x64/libprism_usb_sdk.so
```
