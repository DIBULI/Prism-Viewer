# Licensing

## Viewer source

The project-owned Viewer source code, tests, build scripts, documentation and
assets are licensed under the [Apache License, Version 2.0](LICENSE).
Contributions to those files are accepted under the same license unless
explicitly agreed otherwise. Existing third-party copyright notices must be
retained. No trademark or endorsement rights are granted.

## Separate components

The license above does **not** relicense the contents of `third_party/`,
the proprietary Prism SDK core, or any other third-party component.
Prism SDK interface headers and examples have their own source license;
its compiled libraries remain closed-source under the
[SDK binary license](licenses/Prism-SDK-Binary-LICENSE.txt).
That license permits redistribution of unmodified binaries with applications,
provided its copyright notices and applicable licenses are retained.
Linking the SDK into the Viewer does not turn the SDK into Apache-licensed code.

Qt and other runtime dependencies retain their own licenses. See
[third-party notices](THIRD_PARTY_NOTICES.md) and the accompanying license files.
The release application is a combination of separately licensed components,
not an assertion that every file in the application bundle is Apache-licensed.

## Distribution

Redistributions must include the applicable license texts and notices. The
build/install rules ship this repository's `LICENSE`, `NOTICE`, this scope
document and the third-party notices together with the application.
The SDK core implementation source is not published or included in this repository.
