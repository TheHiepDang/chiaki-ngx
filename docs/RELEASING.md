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
payload. Its old dependency recipe is not used for production. No claim of VC/ARM
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
Windows/Steam Deck hardware. Unmerged PRs exercise the same builds before a release tag is created.

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
each main numeric component must fit Windows' 0-65535 resource range. Both stable and RC
commits must already be reachable from remote `main`.

CI overlays `cmake/Version.txt` with the tag's version before configuring either platform.
This is a documented build input, not a commit/tag rewrite. Provenance records the original
version, tag, source commit/tree, submodules and dependency-lock hash. `CHIAKI_VERSION`, runtime
display, Windows ProductVersion and installer AppVersion therefore retain the same full
version (including `-rc.1`); numeric Windows/installer version fields use MAJOR.MINOR.PATCH.0.
The checked-in `0.1` remains the development baseline until changed normally in source.

## Manual validation and monitoring

Unmerged pull requests automatically run **Validate Chiaki-ngx release packages** from
`build-pr.yaml`, reusing the exact Windows/Linux release-asset jobs with `contents: read`.
PRs compile the checked-out merge commit, use a `-ci.RUN_NUMBER` label and an empty tag object,
and never call a publishing job. `pull_request_target` is not used. Main ancestry is required
only for actual release tags. Require both Windows/Linux jobs in branch protection.
Linux ARM64 remains a separate manual-only workflow and cannot block these checks.

The inherited PR setup's MINGW64 catalog no longer lists hidapi even though historical package
downloads remain visible. CLANG64 is the production path, with its checksum-locked dependency
closure. The shared FFmpeg script always fetches the pinned GitHub commit (no git.ffmpeg.org
fallback). See [CI architecture](CI-ARCHITECTURE.md) for the upstream comparison and
explicit AppImage dependency boundary.


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
and `updates/Install-Update.ps1`, as required by existing installations. Linux continues
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


Linux runtime baseline: Ubuntu 24.04 desktop or current SteamOS with equivalent C/C++, X11/
Wayland, font/audio, GLVND, Vulkan and LibVA host interfaces plus the appropriate GPU driver.
`scripts/ci/linux-runtime.json` defines the exact SONAME boundary and Ubuntu packages; CI installs
that complete baseline without Qt or build-container paths. The complete ELF audit scans the
launcher, executable, libraries, Qt plugins and helpers before startup, reports every unresolved
library together, and rejects unbundled application dependencies and bundled GPU drivers.
The always-retained `diagnostics-linux` artifact includes `appimage-elf-audit.json`, startup logs
and the exact AppImage for inspection without recompiling. The JSON records each ELF's direct
requirements, RPATH/RUNPATH and complete host-loader output (including symbol-version errors).
SteamOS graphics and PS streaming still require hardware acceptance testing.

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

## Publisher protocol and recovery

The publisher creates an unpublished release with `POST /repos/OWNER/REPO/releases`,
`draft: true`, the exact source SHA and generated notes. It captures the **release ID and
upload URL directly from that successful response**. Release listing is only a defensive
pre-creation check: it is never used to rediscover the created draft. GitHub Actions tokens
can omit drafts from tag/list discovery even after successfully creating them; see
[CLI issue 3037](https://github.com/cli/cli/issues/3037),
[5252](https://github.com/cli/cli/issues/5252) and
[9076](https://github.com/cli/cli/issues/9076).

Uploads use the returned, validated release-specific `upload_url` with raw binary bodies.
Downloads use `GET /repos/OWNER/REPO/releases/assets/ASSET_ID` with authenticated octet-stream
responses, streamed directly to disk through `gh api` (including its HTTP redirect handling).
Neither transfer uses `gh release upload/download` or tag-based draft discovery.
The [release API](https://docs.github.com/en/rest/releases/releases#create-a-release) and
[asset API](https://docs.github.com/en/rest/releases/assets#get-a-release-asset) document these paths.

The created release must have the expected ID/tag/source/channel, remain a draft and initially
have no assets. All five assets (Windows ZIP, installer, AppImage, checksums and provenance)
must have unique expected identities and complete GitHub size/state/SHA256 metadata. Every
download is hashed against the verified build. Draft identity and metadata are reread by ID
after transfers, and the remote tag object is rechecked before the ID-specific publish PATCH.
Stable/prerelease and Latest policies are unchanged. Only IDs returned by this invocation's
successful creation/upload responses are modified; existing releases and assets are never
adopted, overwritten or deleted. Conflict or inaccessible ID responses stop the run. There
is no mutation retry, tag edit or personal access token requirement.

Build/test/package failures publish nothing. Inspect job logs and `diagnostics-*` artifacts.
If publishing has started, **do not blindly rerun the tag workflow**: it uses the original
tagged publisher code, and even a failed creation response may have left a draft. The error
log and job summary record the new release ID when available. Inspect it read-only with a
maintainer's authenticated CLI; do not use an empty release listing as proof no draft exists:

```sh
gh api repos/TheHiepDang/chiaki-ngx/releases/RELEASE_ID \
  --jq '{id,tag_name,draft,prerelease,published_at,assets:[.assets[]|{id,name,state,size,digest}]}'
git ls-remote origin refs/tags/TAG 'refs/tags/TAG^{}'
```

Creation/upload/verification failures leave the draft and any partial assets intact. A timeout
on the final PATCH may mean publication succeeded; inspect the ID before doing anything else.
Do not delete/edit the draft, overwrite assets, retarget/delete tags, or retry mutations as an
automatic recovery step. Any cleanup requires explicit maintainer approval. A source fix needs
normal PR review, a merge into main and a **fresh version tag**, not a rerun of the old tag.

### RC1 incident and RC2 plan

Run [40485130607](https://github.com/TheHiepDang/chiaki-ngx/actions/runs/40485130607) built
`v0.2.0-rc.1` at source `51e2760042d2d01b99a1ff2cf36871bdc95052a2`; all build jobs passed.
The publisher created draft **425292968**, then failed its immediate release-list lookup
before uploading anything. Read-only inspection confirmed `draft: true`, `prerelease: true`,
zero assets and no publication. This matches the documented token/discovery failure pattern;
the original list response was not retained, so its server-side cause cannot be distinguished
from transient visibility using that log alone. The unsafe dependency on discovery is removed.

The RC1 annotated tag object remains `a709e0af2a0f0fb20475e39941b3d1564e72c268`, pointing to
the source SHA above. Preserve that tag and draft. After reviewing/merging this fix and passing
full Windows/Linux validation on updated main, obtain maintainer approval for a new
`v0.2.0-rc.2` tag. Confirm that version is unused, select the reviewed main commit and follow
the stable/prerelease tagging procedure above. Its workflow rebuilds and validates the new
version, then exercises ID-based publishing with the built-in token. Real-token publication
is deliberately not exercised by PR tests. Review RC1 cleanup separately; it is not required
to test the different RC2 tag.
