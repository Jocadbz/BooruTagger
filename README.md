# BooruTagger

A native C++/Qt desktop app for manually tagging Danbooru and Shimmie2 posts. Open a post, inspect the image or video, edit tags, save, and move on.

## What it does

- Single-ID, ascending/descending range, and tag-search sessions, with queues and drafts in SQLite.
- Keyboard-first tag editing, undo/redo, autocomplete, instance-specific presets, and local edit history.
- Zoomable images and an embedded libmpv player with seeking, frame stepping, speed, looping, and audio controls.
- Fetch/merge/write/verify saves. Failed or uncertain submissions keep the draft and don't blindly retry.
- Multiple server/account profiles, but only one active session. Secrets use QtKeychain when requested; otherwise they stay in memory.

## Building

Needs C++20, CMake, Ninja, Qt 6.4+ (base, OpenGL widgets, SQL/SQLite, Concurrent, Test), pkg-config, and libmpv. QtKeychain is optional. Extra Qt image-format plugins are useful.

On Arch:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base mpv qtkeychain-qt6 qt6-imageformats
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/boorutagger
```

For release, use `-B build-release -DCMAKE_BUILD_TYPE=Release` and build that directory. Optional install: `cmake --install build-release --prefix ~/.local`.

For distcc, configure with `-DBOORUTAGGER_USE_DISTCC=ON`, then build with `--parallel "$(distcc -j)"`. The launcher strips CMake's absolute compiler path before forwarding it. Workers need a compatible compiler and a running `distccd` service.

## Usage

Add an instance/account profile using its installation base URL. Sign in with a Danbooru API key or Shimmie password, then create or choose a session. Shimmie needs its 2.12+ GraphQL extension and its Ouroboros API extension enabled. The instance dialog has a Testbooru shortcut for safe manual testing.

Paste space-separated tags or use suggestions; Enter/Tab accepts them. Select tags and press Delete, or use Ctrl+E for the full list. Ctrl+Z reverses local edits. Ctrl+S saves; Ctrl+Enter saves and advances; Alt+Left goes back; Alt+Right skips without submitting; Ctrl+K focuses tag input. Help lists the remaining shortcuts.

For sessions with tagme removal enabled, explicitly check **Sufficiently tagged** (Alt+T) before saving. Unchecked keeps `tagme`. Skipping never submits changes. Drafts survive switching sessions and closing; startup offers to resume. Ctrl+D lists pending drafts, including skipped ones. Session → Export draft provides a manual recovery copy if storage fails.

## Notes

- APIs and full UI save flows are tested with local fixtures, not live accounts. Native MP4 rendering is tested on X11; Wayland and a real OS keychain backend still need manual testing. The video test skips without a display or ffmpeg.
- Search IDs are materialized before editing (up to 100,000). Shimmie uses offset discovery only during this phase; external edits can still change its search results during discovery.
- Shimmie tag searches use the official Ouroboros JSON endpoint `post/index.json`; enable the `ouroboros_api` extension in the instance's Extension Manager. GraphQL handles login, single-post lookup, and tag updates. Shimmie autocomplete uses locally observed/entered tags, without invented metadata.
- Shimmie has no atomic compare-and-swap in this API. A change can race between the final read and write; verification catches differing final tags, not every overwritten intermediate edit.
- Videos stream directly without forwarding API credentials/cookies to mpv. Media requiring those credentials may not play. Images use the instance's scoped network client. Image downloads are capped at 64 MiB; very large images decode at up to 8192 pixels per side.
- Sessions, queues, drafts, and local history live in `~/.local/share/BooruTagger/BooruTagger/boorutagger.sqlite` by default (or `$XDG_DATA_HOME/BooruTagger/BooruTagger/boorutagger.sqlite`). Credentials are kept in the OS keychain when available/requested, otherwise only in memory. The decoded-image cache is capped at 96 MiB, disk media cache at 128 MiB per profile, and observed tags at 50,000 per profile. No AI tagging, telemetry, or external services.
