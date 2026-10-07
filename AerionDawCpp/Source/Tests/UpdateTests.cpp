#include <JuceHeader.h>
#include "../Updates/UpdateChecker.h"

//==============================================================================
// Updates: which GitHub release a build is offered, and the check on the
// downloaded installer. No network: the releases are JSON in GitHub's shape.
//==============================================================================

namespace
{
    using Updates::Platform;
    using Updates::Version;

    Version v (const char* text)   { return Version::parse (text).value_or (Version { -1, -1, -1, {} }); }

    juce::var release (const juce::String& tag, bool preRelease, bool draft = false,
                       juce::StringArray assetNames = { "AerionDAW-x-Windows.exe", "AerionDAW-x-macOS.dmg" })
    {
        juce::Array<juce::var> assets;
        for (const auto& name : assetNames)
        {
            auto* asset = new juce::DynamicObject();
            asset->setProperty ("name", name);
            asset->setProperty ("browser_download_url", "https://github.com/aethos-studio/Aerion-DAW/releases/download/" + tag + "/" + name);
            asset->setProperty ("size", 1234);
            asset->setProperty ("digest", "sha256:ABCDEF0123");
            assets.add (juce::var (asset));
        }

        auto* r = new juce::DynamicObject();
        r->setProperty ("tag_name", tag);
        r->setProperty ("name", "Aerion DAW " + tag);
        r->setProperty ("body", "# Notes for " + tag);
        r->setProperty ("html_url", "https://github.com/aethos-studio/Aerion-DAW/releases/tag/" + tag);
        r->setProperty ("prerelease", preRelease);
        r->setProperty ("draft", draft);
        r->setProperty ("assets", assets);
        return juce::var (r);
    }

    juce::var list (std::initializer_list<juce::var> items)
    {
        return juce::var (juce::Array<juce::var> (items));
    }
}

class UpdateTests final : public juce::UnitTest
{
public:
    UpdateTests() : juce::UnitTest ("Updates", "Aerion") {}

    void runTest() override
    {
        beginTest ("versions are parsed from tags");
        {
            const auto a = Version::parse ("v0.6.0-alpha.2");
            expect (a.has_value());
            expectEquals (a->minor, 6);
            expectEquals (a->preRelease, juce::String ("alpha.2"));
            expectEquals (Version::parse ("0.5.0+build.7")->toString(), juce::String ("0.5.0"));
            expectEquals (Version::parse ("V1.2.3-Beta")->toString(), juce::String ("1.2.3-beta"));

            for (auto* bad : { "", "v", "0.5", "1.2.3.4", "1.x.3", "1.2.3-", "1.2.3-al..pha", "1.2.3-al_pha" })
                expect (! Version::parse (bad).has_value(), juce::String ("parsed: ") + bad);
        }

        beginTest ("versions are ordered as in semver");
        {
            const char* ascending[] = { "0.5.0-alpha", "0.5.0-alpha.1", "0.5.0-alpha.2", "0.5.0-alpha.10",
                                        "0.5.0-alpha.beta", "0.5.0-beta", "0.5.0-rc.1", "0.5.0", "0.5.1",
                                        "0.6.0-alpha", "0.6.0", "0.10.0", "1.0.0" };
            for (size_t i = 0; i + 1 < std::size (ascending); ++i)
            {
                expect (v (ascending[i]) < v (ascending[i + 1]), juce::String (ascending[i]) + " < " + ascending[i + 1]);
                expect (! (v (ascending[i + 1]) < v (ascending[i])));
            }
            expect (v ("v0.5.0") == v ("0.5.0"));
        }

        beginTest ("the newest newer release with an installer is offered");
        {
            const auto releases = list ({ release ("v0.5.0", true), release ("v0.7.0", false),
                                          release ("v0.6.0", false), release ("v0.4.0", false) });
            const auto update = Updates::findUpdate (releases, v ("0.5.0"), false, Platform::windows);
            expect (update.has_value());
            expectEquals (update->version.toString(), juce::String ("0.7.0"));
            expectEquals (update->tag, juce::String ("v0.7.0"));
            expectEquals (update->assetName, juce::String ("AerionDAW-x-Windows.exe"));
            expect (update->assetUrl.endsWith ("/v0.7.0/AerionDAW-x-Windows.exe"));
            expectEquals (update->sha256, juce::String ("abcdef0123"));
            expectEquals (update->assetSize, (juce::int64) 1234);
            expectEquals (update->notes, juce::String ("# Notes for v0.7.0"));
        }

        beginTest ("nothing is offered when the build is up to date");
        {
            const auto releases = list ({ release ("v0.5.0", true), release ("v0.4.0", false) });
            expect (! Updates::findUpdate (releases, v ("0.5.0"), true, Platform::windows).has_value());
            expect (! Updates::findUpdate (juce::var(), v ("0.5.0"), true, Platform::windows).has_value());
        }

        beginTest ("pre-releases only for builds that take them, drafts never");
        {
            const auto releases = list ({ release ("v0.6.0-alpha.1", false), release ("v0.5.1", true),
                                          release ("v0.9.0", false, true) });
            expect (! Updates::findUpdate (releases, v ("0.5.0"), false, Platform::windows).has_value());

            const auto update = Updates::findUpdate (releases, v ("0.5.0"), true, Platform::windows);
            expect (update.has_value());
            expectEquals (update->version.toString(), juce::String ("0.6.0-alpha.1"));
            expect (update->preRelease, "a tag with a pre-release suffix counts as a pre-release");

            // An alpha build of a version is older than that version's final release.
            const auto final = list ({ release ("v0.6.0", false) });
            expect (Updates::findUpdate (final, v ("0.6.0-alpha.3"), true, Platform::windows).has_value());
        }

        beginTest ("each platform gets its own installer");
        {
            const auto releases = list ({ release ("v0.6.0", false, false, { "AerionDAW-0.6.0-Darwin.dmg", "AerionDAW-0.6.0-Windows.exe" }),
                                          release ("v0.7.0", false, false, { "AerionDAW-0.7.0-Windows.exe" }) });

            expectEquals (Updates::findUpdate (releases, v ("0.5.0"), false, Platform::macOS)->assetName,
                          juce::String ("AerionDAW-0.6.0-Darwin.dmg"));
            expectEquals (Updates::findUpdate (releases, v ("0.5.0"), false, Platform::windows)->assetName,
                          juce::String ("AerionDAW-0.7.0-Windows.exe"));
            expect (! Updates::findUpdate (releases, v ("0.5.0"), false, Platform::unsupported).has_value());
        }

        beginTest ("a download is checked against the release's size and SHA-256");
        {
            const juce::TemporaryFile temp (".exe");
            const auto file = temp.getFile();
            file.replaceWithText ("installer bytes");

            Updates::Release r;
            r.assetSize = file.getSize();
            r.sha256    = juce::SHA256 (file).toHexString();
            expect (Updates::verifyDownload (file, r).isEmpty());

            r.sha256 = juce::String::repeatedString ("0", 64);
            expect (Updates::verifyDownload (file, r).contains ("checksum"));

            r.sha256    = {};
            r.assetSize = file.getSize() + 10;
            expect (Updates::verifyDownload (file, r).contains ("incomplete"));

            expect (Updates::verifyDownload (file.getSiblingFile ("missing.exe"), r).isNotEmpty());
        }

        beginTest ("this build knows its own version");
        expect (Updates::currentVersion().major >= 0 && ! (Updates::currentVersion() == Version { 0, 0, 0, {} }));
    }
};

static UpdateTests updateTests;
