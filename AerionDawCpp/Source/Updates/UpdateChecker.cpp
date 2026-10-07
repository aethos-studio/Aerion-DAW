#include "UpdateChecker.h"

// CMake sets both for the app and the tests: the release tag's version
// (AERION_PACKAGE_VERSION) and the release stage (e.g. "Alpha").
#ifndef AERION_BUILD_VERSION
 #define AERION_BUILD_VERSION ProjectInfo::versionString
#endif
#ifndef AERION_RELEASE_STAGE
 #define AERION_RELEASE_STAGE ""
#endif

namespace Updates
{
namespace
{
    bool isNumber (const juce::String& s)
    {
        return s.isNotEmpty() && s.containsOnly ("0123456789");
    }

    int sign (juce::int64 v)   { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

    int compareIdentifiers (const juce::String& a, const juce::String& b)
    {
        const bool aNumber = isNumber (a), bNumber = isNumber (b);

        if (aNumber && bNumber)  return sign (a.getLargeIntValue() - b.getLargeIntValue());
        if (aNumber)             return -1;
        if (bNumber)             return 1;
        return sign (a.compare (b));
    }

    juce::String installerExtension (Platform platform)
    {
        switch (platform)
        {
            case Platform::windows:     return ".exe";
            case Platform::macOS:       return ".dmg";
            case Platform::unsupported: break;
        }
        return {};
    }

    juce::var installerAsset (const juce::var& assets, Platform platform)
    {
        const auto extension = installerExtension (platform);

        if (extension.isNotEmpty())
            if (auto* list = assets.getArray())
                for (const auto& asset : *list)
                    if (asset["name"].toString().endsWithIgnoreCase (extension)
                        && asset["browser_download_url"].toString().startsWithIgnoreCase ("https://"))
                        return asset;

        return {};
    }

    juce::String requestHeaders()
    {
        // GitHub refuses API requests without a User-Agent.
        return "Accept: application/vnd.github+json\r\n"
               "User-Agent: Aerion-DAW/" + currentVersion().toString();
    }

    juce::File& pendingInstaller()
    {
        static juce::File file;
        return file;
    }
}

//==============================================================================
std::optional<Version> Version::parse (juce::String text)
{
    text = text.trim();
    if (text.startsWithIgnoreCase ("v"))
        text = text.substring (1);
    text = text.upToFirstOccurrenceOf ("+", false, false);

    const auto core = text.upToFirstOccurrenceOf ("-", false, false);
    const auto pre  = text.fromFirstOccurrenceOf ("-", false, false);

    const auto numbers = juce::StringArray::fromTokens (core, ".", "");
    if (numbers.size() != 3)
        return {};

    for (const auto& n : numbers)
        if (! isNumber (n))
            return {};

    if (text.containsChar ('-') && pre.isEmpty())
        return {};

    if (text.containsChar ('-'))
        for (const auto& id : juce::StringArray::fromTokens (pre, ".", ""))
            if (id.isEmpty() || ! id.containsOnly ("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-"))
                return {};

    Version v;
    v.major      = numbers[0].getIntValue();
    v.minor      = numbers[1].getIntValue();
    v.patch      = numbers[2].getIntValue();
    v.preRelease = pre.toLowerCase();
    return v;
}

int Version::compare (const Version& a, const Version& b)
{
    if (a.major != b.major)  return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)  return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)  return a.patch < b.patch ? -1 : 1;

    if (a.isPreRelease() != b.isPreRelease())
        return a.isPreRelease() ? -1 : 1;

    const auto aIds = juce::StringArray::fromTokens (a.preRelease, ".", "");
    const auto bIds = juce::StringArray::fromTokens (b.preRelease, ".", "");

    for (int i = 0; i < juce::jmin (aIds.size(), bIds.size()); ++i)
        if (const auto c = compareIdentifiers (aIds[i], bIds[i]); c != 0)
            return c;

    return sign (aIds.size() - bIds.size());
}

juce::String Version::toString() const
{
    return juce::String (major) + "." + juce::String (minor) + "." + juce::String (patch)
         + (isPreRelease() ? "-" + preRelease : juce::String());
}

//==============================================================================
Platform currentPlatform()
{
   #if JUCE_WINDOWS
    return Platform::windows;
   #elif JUCE_MAC
    return Platform::macOS;
   #else
    return Platform::unsupported;
   #endif
}

std::optional<Release> findUpdate (const juce::var& releases, const Version& current,
                                   bool includePreReleases, Platform platform)
{
    std::optional<Release> best;

    if (auto* list = releases.getArray())
    {
        for (const auto& r : *list)
        {
            if ((bool) r["draft"])
                continue;

            const auto version = Version::parse (r["tag_name"].toString());
            if (! version.has_value() || ! (current < *version))
                continue;

            const bool preRelease = (bool) r["prerelease"] || version->isPreRelease();
            if (preRelease && ! includePreReleases)
                continue;

            if (best.has_value() && ! (best->version < *version))
                continue;

            const auto asset = installerAsset (r["assets"], platform);
            if (! asset.isObject())
                continue;

            Release release;
            release.version    = *version;
            release.tag        = r["tag_name"].toString();
            release.title      = r["name"].toString().isNotEmpty() ? r["name"].toString() : release.tag;
            release.notes      = r["body"].toString();
            release.pageUrl    = r["html_url"].toString();
            release.preRelease = preRelease;
            release.assetName  = asset["name"].toString();
            release.assetUrl   = asset["browser_download_url"].toString();
            release.assetSize  = (juce::int64) asset["size"];

            const auto digest = asset["digest"].toString();
            if (digest.startsWithIgnoreCase ("sha256:"))
                release.sha256 = digest.substring (7).trim().toLowerCase();

            best = release;
        }
    }

    return best;
}

Version currentVersion()
{
    static const auto version = Version::parse (AERION_BUILD_VERSION).value_or (Version {});
    return version;
}

bool buildTakesPreReleases()
{
    return currentVersion().isPreRelease() || juce::String (AERION_RELEASE_STAGE).isNotEmpty();
}

//==============================================================================
CheckResult checkForUpdate (const Version& current, bool includePreReleases)
{
    int status = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders (requestHeaders())
                             .withConnectionTimeoutMs (10000)
                             .withStatusCode (&status);

    auto stream = juce::URL (releasesUrl).createInputStream (options);

    if (stream == nullptr)
        return { "Could not reach GitHub. Check the internet connection and try again.", {} };

    const auto body = stream->readEntireStreamAsString();

    if (status == 403 || status == 429)
        return { "GitHub is limiting requests from this network. Try again in an hour.", {} };

    if (status != 200)
        return { "GitHub answered with HTTP " + juce::String (status) + ".", {} };

    const auto releases = juce::JSON::parse (body);
    if (! releases.isArray())
        return { "GitHub's answer could not be read.", {} };

    return { {}, findUpdate (releases, current, includePreReleases, currentPlatform()) };
}

juce::File getDownloadFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("AerionDAW")
               .getChildFile ("Updates");
}

juce::String download (const Release& release, const juce::File& target,
                       const std::function<bool (double)>& onProgress)
{
    if (! target.getParentDirectory().createDirectory() || (target.exists() && ! target.deleteFile()))
        return "Could not write to " + target.getParentDirectory().getFullPathName() + ".";

    int status = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders (requestHeaders())
                             .withConnectionTimeoutMs (15000)
                             .withStatusCode (&status);

    auto stream = juce::URL (release.assetUrl).createInputStream (options);

    if (stream == nullptr)
        return "Could not reach GitHub. Check the internet connection and try again.";

    if (status != 200)
        return "GitHub answered with HTTP " + juce::String (status) + ".";

    bool cancelled = false;
    {
        juce::FileOutputStream out (target);
        if (out.failedToOpen())
            return "Could not write " + target.getFullPathName() + ".";

        const auto total = release.assetSize > 0 ? release.assetSize : stream->getTotalLength();
        constexpr int chunk = 64 * 1024;
        juce::HeapBlock<char> buffer (chunk);
        juce::int64 done = 0;

        for (;;)
        {
            const auto n = stream->read (buffer.get(), chunk);
            if (n <= 0)
                break;   // finished or dropped; verifyDownload checks the size

            if (! out.write (buffer.get(), (size_t) n))
                return "Could not write " + target.getFullPathName() + ".";

            done += n;
            if (onProgress != nullptr && ! onProgress (total > 0 ? (double) done / (double) total : -1.0))
            {
                cancelled = true;
                break;
            }
        }

        out.flush();
        if (out.getStatus().failed())
            return "Could not write " + target.getFullPathName() + ": " + out.getStatus().getErrorMessage();
    }

    if (cancelled)
    {
        target.deleteFile();
        return "The download was cancelled.";
    }

    const auto problem = verifyDownload (target, release);
    if (problem.isNotEmpty())
        target.deleteFile();
    return problem;
}

juce::String verifyDownload (const juce::File& file, const Release& release)
{
    if (! file.existsAsFile())
        return "The installer was not saved.";

    if (release.assetSize > 0 && file.getSize() != release.assetSize)
        return "The download is incomplete (" + juce::File::descriptionOfSizeInBytes (file.getSize())
             + " of " + juce::File::descriptionOfSizeInBytes (release.assetSize) + "). Try again.";

    if (release.sha256.isNotEmpty() && juce::SHA256 (file).toHexString() != release.sha256)
        return "The installer does not match the release's checksum, so it may be damaged. Try again.";

    return {};
}

//==============================================================================
void setPendingInstaller (const juce::File& file)   { pendingInstaller() = file; }
juce::File getPendingInstaller()                    { return pendingInstaller(); }

bool launchPendingInstaller()
{
    const auto file = getPendingInstaller();
    if (! file.existsAsFile())
        return false;

    // Windows runs the installer (it asks for elevation itself); macOS opens
    // the disk image in Finder.
    juce::Logger::writeToLog ("Updates: starting " + file.getFullPathName());
    pendingInstaller() = juce::File();
    return file.startAsProcess();
}
}
