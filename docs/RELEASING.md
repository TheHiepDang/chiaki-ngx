# Releasing Chiaki-ngx

Push a version tag after merging the tested source into `main`. GitHub Actions builds,
packages, checks and publishes the release; no local compiler, upload or PAT is needed.
The existing `build-release.yaml` is the single production pipeline. It and weekly validation
reuse the read-only `build-release-assets.yml` jobs. Manual runs and weekly builds only produce
temporary Actions artifacts; only the tag-only publisher requests release-write permissions.

## Release matrix and existing infrastructure

| Target | Release status | Build/packaging path |
|---|---|---|
| Windows x64 portable ZIP + installer | Required together | MSYS2 CLANG64 GUI, MSVC worker, existing MSYS2 deployer, enhancement packager and Inno script |
| Linux x86_64 AppImage | Required | Existing builder container and `scripts/build-appimage.sh` |
| Windows ARM64 | Manual validation | Existing `build-windows-arm-portable.yml` / MSYS2 ARM workflows; RTX worker is currently x64-only |
| Linux ARM64 AppImage | Manual validation | Existing `build-appimage-arm64.yml`; its older Qt builder and ARM deployment tools are not qualified by the x64 checks |
| macOS ARM64 DMG | Manual validation | Existing `build-macos-arm.yml`; Homebrew paths, deployment/signing and runtime closure still need platform testing |
| Windows x64 Visual C++ GUI | Experimental alternative | Existing `build-windows-x86.yml`, with package dependency/startup checks |

Additional targets remain available, but are not silently included in releases as verified
packages. Promote them only after adding equivalent checks and making their jobs required.
Windows portable and installer must come from the same staged payload; neither can publish
without the other and the Linux build succeeding.

The initial audit found that the former release workflow created a public release first,
used `RELEASE_PAT`, allowed updates, and uploaded ZIP names the fork's updater did not accept.
It also omitted the RTX worker, runtime manifest and update installer helper. The former
weekly workflow separately published mutable canaries. It now calls validation-only builds.
The upstream Discord webhook workflows are disabled for this fork; this pipeline sends no
Discord notifications.

The MSYS2 path is closest to the existing working GUI deployment. The VC path has mutable
Vulkan/shaderc downloads, an unpinned SPIRV-Cross checkout, and hand-maintained DLL copies;
it remains manual until it passes the new clean-path validation and supplies the enhanced
payload. Its old dependency recipe is not used for production. No claim of VC/ARM/macOS
runtime verification follows from source review alone.

## One-time GitHub setup

Merge the workflow, scripts and tests into the default branch **`main`**. Enable Actions,
allow the SHA-pinned `actions/checkout`, `actions/upload-artifact`, `actions/download-artifact`
and `msys2/setup-msys2` actions, and permit the publishing job to request `contents: write`
for the built-in `GITHUB_TOKEN`. Keep default workflow permissions read-only. No secret or
PAT is required; `RELEASE_PAT` and upstream `WEBHOOK_URL` are not used by this pipeline.

Recommended: protect `main`, require review/status checks, and protect `v*` tags against
updates/deletion while allowing release maintainers to create them. The workflow checks
remote-main ancestry and rechecks the remote tag object before publication. It never creates,
retargets or deletes tags. GitHub-hosted Windows 2022 and Ubuntu 24.04 runners, public dependency
download access and sufficient Actions minutes/artifact storage are required.

Before the first public tag, run a full manual validation and test its artifacts on real
Windows/Steam Deck hardware. The implementation cannot be exercised remotely until it has
been pushed with maintainer approval.

## Stable release and prerelease

From a clean checkout, inspect the commit and ensure the version is unused:

```sh
git switch main
git pull --ff-only origin main
git status --short
git log -1 --oneline
git ls-remote --tags origin refs/tags/v0.2.0
# Stop if dirty, the commit is wrong, or the remote tag already exists.
git tag -a v0.2.0 -m "Release v0.2.0"
git push origin refs/tags/v0.2.0
```

Use `v0.2.0-rc.1` instead for a prerelease. Any valid prerelease suffix creates a GitHub
prerelease and is not marked Latest. Stable tags use GitHub's version-based Latest policy.
Tags must be `vMAJOR.MINOR.PATCH[-prerelease]`, without numeric leading zeroes or `+metadata`;
each main numeric component must fit Windows' 0–65535 resource range. Both stable and RC
commits must already be reachable from remote `main`.

CI overlays `cmake/Version.txt` with the tag's version before configuring either platform.
This is a documented build input, not a commit/tag rewrite. Provenance records the original
version, tag, source commit/tree, submodules and dependency-lock hash. `CHIAKI_VERSION`, runtime
display, Windows ProductVersion and installer AppVersion therefore retain the same full
version (including `-rc.1`); numeric Windows/installer version fields use MAJOR.MINOR.PATCH.0.
The checked-in `0.1` remains the development baseline until changed normally in source.

## Manual validation and monitoring

Authenticate your terminal with GitHub CLI normally. CI uses its own token.

```sh
gh workflow run build-release.yaml --repo TheHiepDang/chiaki-ngx --ref main -f target=all
# Optional: target=windows or target=linux, or a version label such as -f version=v0.2.0-rc.1
gh run list --repo TheHiepDang/chiaki-ngx --workflow build-release.yaml --limit 5
gh run watch RUN_ID --repo TheHiepDang/chiaki-ngx --exit-status
gh run view RUN_ID --repo TheHiepDang/chiaki-ngx --log-failed
gh run download RUN_ID --repo TheHiepDang/chiaki-ngx --dir validation
```

After the workflow exists on `main`, `--ref` can name a pushed test branch containing it.
Manual runs never create releases, including when invoked against a tag. Without a supplied
label they use a `-ci.RUN_NUMBER` version. Branch pushes do not trigger this release workflow.

## Package verification and automatic updates

Public assets are `chiaki-ngx-windows-x64-vVERSION.zip`, its `-installer.exe` counterpart,
`chiaki-ngx-linux-x86_64-vVERSION.AppImage`, `SHA256SUMS` and `BUILD-PROVENANCE.json`.
Per-platform Actions artifacts also include checksums and provenance.

The updater stays on **TheHiepDang/chiaki-ngx** and preserves its legacy `chiaki-ngx.zip`/
`pre-release` baseline handling. New releases use the canonical Windows ZIP name above. The
publisher checks GitHub's asset digest, uploaded state and size as well as downloading the
draft assets and verifying all bytes. Missing updater metadata prevents publication. Windows
ZIPs include the worker, version-2 runtime manifest, executable hashes in `build-info.json`
and `updates/Install-Update.ps1`, as required by existing installations. Linux/macOS continue
to use their existing release-page update behavior; no platform is redirected upstream.

CI runs core, updater/channel/mock-network, QML and enhancement model/manifest tests. Windows
checks PE architecture/imports throughout the package, Qt/QML/runtime resources, clean-PATH
startup, runtime/resource versions, installer installation and payload equality. The AppImage
is extracted and started outside its build container, without the builder's Qt/library paths.
`--package-smoke-test` exercises packaged QtQuick/Controls/Layouts without console access.
These tests do not prove PS streaming, graphics-driver compatibility or RTX rendering on
hardware. Hosted CI explicitly skips the worker's NVIDIA-only GPU self-test; model and worker
loader checks still run. Local worker builds keep their GPU self-test by default.

Download a release, then verify on Linux (`Get-FileHash -Algorithm SHA256` on Windows):

```sh
gh release download v0.2.0 --repo TheHiepDang/chiaki-ngx --dir downloaded
cd downloaded
sha256sum --check SHA256SUMS
```

Compare provenance's source commit with `git rev-parse 'v0.2.0^{commit}'`. Checksums detect
corruption; they are not independent signatures. Windows installer and binaries are unsigned
unless a separate signing process is configured.

## Dependencies and maintenance

`scripts/ci/dependencies.json` locks the builder image digest, source revisions, SDL/protoc/
deployment downloads, Windows FFmpeg and SM86 archives, and the transitive MSYS2 CLANG64 package
set. A changed `latest`/`continuous` asset fails its hash check; there is no floating fallback.
The SDK is sparsely checked out at a fixed NVIDIA commit. SM86 0.4.0 matches the existing
deployment's inspected FG/proxy/backend/kernel hashes; notices are retained. NR is never
downloaded or distributed; only the existing compatible NR hash is retained in the manifest.

MSYS2 base utilities, hosted runner images, MSVC and the runner's Inno Setup 6 remain managed
by the runner/package provider; this is not a claim of bit-for-bit reproducibility. The package
set is recorded in CI diagnostics. Old package URLs may be pruned: refresh/review the lock and
validate the candidate rather than bypassing hashes. In MSYS2, run `python scripts/ci/lock-msys.py`
to refresh the CLANG64 closure, then inspect its diff and run full validation. Update pinned
source refs in build scripts/workflow together with the lock. Never refresh dependencies during
an existing tag's build to work around a failure.

## Recovery

Build/test/package failures publish nothing. Inspect job logs and `diagnostics-*` artifacts.
For transient failures rerun the unchanged build with
`gh run rerun RUN_ID --failed --repo TheHiepDang/chiaki-ngx`.

Upload/verification interruptions may leave an **unpublished draft**. Inspect it first with
`gh release view TAG --repo TheHiepDang/chiaki-ngx`. After confirming it is still a draft,
explicitly delete that failed draft with `gh release delete TAG --repo TheHiepDang/chiaki-ngx`
and rerun the failed publishing job. Do not use `--cleanup-tag`. If publication timed out,
check whether it actually became public before retrying. Existing drafts/releases are refused,
never overwritten. Source fixes require a new reviewed commit and new version tag.
