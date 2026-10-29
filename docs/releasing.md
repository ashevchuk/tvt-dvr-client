# Releasing

Releases are built by the `Release` workflow (`.github/workflows/release.yml`), not on a developer
machine, so the file in a release is the one built from the tagged commit on a clean runner.

## Making a release

1. Make sure the commit you want to release is on `main` and its checks pass.
2. Tag it and push the tag:

   ```sh
   git tag v0.1.1
   git push origin v0.1.1
   ```

3. The workflow runs the unit tests, builds the AppImage in the container described in
   `packaging/appimage/`, checks it (checksum, no missing libraries, glibc 2.28 at most) and
   creates the GitHub release with the AppImage, its SHA-256 and the FFmpeg source.

The workflow can also be started by hand from the Actions tab or with
`gh workflow run release.yml`. That only builds the AppImage and keeps it as a workflow artifact
for 14 days; nothing is published.

## What the workflow does not check

It has no access to a DVR, so it cannot run `bin/devicecheck` or the GUI against a device. Do that
with the downloaded AppImage before announcing a release.

## Changing the bundled software

The versions and SHA-256 hashes of FFmpeg, raylib and the packaging tools are in
`packaging/appimage/Dockerfile`. Update the version and the hash together; the build fails if they
disagree. FFmpeg publishes a signature next to each release: verify it when you update the hash.
The third-party actions used by the workflow are pinned to commit SHAs; update them the same way.
