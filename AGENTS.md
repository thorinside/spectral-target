# Agent Notes

## Release Workflow

Releases are driven by GitHub Actions from pushed version tags.

1. Make the code/docs changes and run the relevant local checks.
2. Commit the changes and push the branch to GitHub.
3. Choose the next SemVer tag in the form `vMAJOR.MINOR.PATCH`.
   - Use a patch bump for fixes, docs, tests, build changes, and release workflow updates.
   - Use a minor bump for new user-visible features that preserve existing preset/plugin compatibility.
   - Use a major bump for breaking changes such as parameter order changes, preset format breaks, GUID changes, or incompatible behavior changes.
4. Create and push the tag, for example:

```sh
git tag -a v1.0.0 -m "v1.0.0"
git push origin v1.0.0
```

The pushed tag triggers `.github/workflows/release.yaml`, which builds the hardware plugin and uploads `spectral_target-plugin.zip` to the GitHub Release.
