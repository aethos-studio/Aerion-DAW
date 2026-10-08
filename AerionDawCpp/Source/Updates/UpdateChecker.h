#pragma once

// In-app updates (Help › Check for Updates, and a check at startup).
//
// Aerion asks GitHub for its releases, picks the newest one this build should
// install, downloads that platform's installer, checks it against the size and
// SHA-256 GitHub publishes for it, and starts it once Aerion has closed.
// Everything here except checkForUpdate() and download() works without a
// network, so the tests cover how a release is chosen.

#include <JuceHeader.h>
#include <functional>
#include <optional>

namespace Updates
{
    inline constexpr const char* releasesUrl = "https://api.github.com/repos/aethos-studio/Aerion-DAW/releases?per_page=30";

    // User settings keys.
    inline constexpr const char* autoCheckKey      = "updates.autoCheck";       // bool, default true
    inline constexpr const char* skippedVersionKey = "updates.skippedVersion";  // e.g. "0.6.0"

    /** A semantic version such as 0.6.0 or 0.6.0-alpha.2. */
    struct Version
    {
        int major = 0, minor = 0, patch = 0;
        juce::String preRelease;   // "alpha.2"; empty for a final release

        /** Accepts a leading "v" and ignores build metadata ("+..."). */
        static std::optional<Version> parse (juce::String text);

        /** Below zero, zero or above zero. A pre-release sorts before its final
            release; its identifiers compare as in semver: numbers numerically
            and before words, words alphabetically, and more of them is later. */
        static int compare (const Version& a, const Version& b);

        bool isPreRelease() const                  { return preRelease.isNotEmpty(); }
        juce::String toString() const;

        bool operator<  (const Version& o) const   { return compare (*this, o) < 0; }
        bool operator== (const Version& o) const   { return compare (*this, o) == 0; }
    };

    enum class Platform { windows, macOS, unsupported };
    Platform currentPlatform();

    struct Release
    {
        Version      version;
        juce::String tag, title, notes, pageUrl;
        bool         preRelease = false;

        juce::String assetName, assetUrl;
        juce::String sha256;         // lower-case hex; empty if GitHub gave none
        juce::int64  assetSize = 0;
    };

    /** The newest release in GitHub's /releases JSON that is newer than
        `current` and has an installer for `platform`. Drafts are skipped, and
        pre-releases unless includePreReleases. */
    std::optional<Release> findUpdate (const juce::var& releases, const Version& current,
                                       bool includePreReleases, Platform platform);

    /** This build's version: the release tag it was packaged from, or the
        project version for a local build. */
    Version currentVersion();

    /** A version as people read it: "0.5.1 Alpha", or "0.5.1-alpha.2" when the
        release tag already carries a pre-release label. */
    juce::String versionText (const Version& v);

    /** This build's own version as text, ignoring AERION_PRETEND_VERSION. The
        About dialog, crash reports and the log show this. */
    juce::String buildVersionText();

    /** Alpha and beta builds, and builds of a pre-release tag, also take pre-releases. */
    bool buildTakesPreReleases();

    struct CheckResult
    {
        juce::String           error;    // empty if GitHub answered
        std::optional<Release> update;   // empty if Aerion is up to date
    };

    /** Asks GitHub. Blocks, so call it off the message thread. */
    CheckResult checkForUpdate (const Version& current = currentVersion(),
                                bool includePreReleases = buildTakesPreReleases());

    /** Where installers are downloaded to. */
    juce::File getDownloadFolder();

    /** Downloads the release's installer to `target`, calling onProgress with
        0..1 (or -1 when the size is unknown); onProgress returning false
        cancels. Then checks the file with verifyDownload(). Blocks. Returns an
        empty string on success, otherwise what went wrong. */
    juce::String download (const Release&, const juce::File& target,
                           const std::function<bool (double)>& onProgress);

    /** Empty if the file has the size and SHA-256 the release names, otherwise
        what does not match. */
    juce::String verifyDownload (const juce::File&, const Release&);

    /** The installer to start once Aerion has closed. */
    void setPendingInstaller (const juce::File&);
    juce::File getPendingInstaller();

    /** Starts the pending installer, if there is one. Main.cpp calls it at shutdown. */
    bool launchPendingInstaller();
}
