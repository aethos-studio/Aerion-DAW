#include <JuceHeader.h>
#include <iostream>
#include "../ProjectData.h"
#include "../Keymap.h"

//==============================================================================
// Aerion smoke tests (Milestone 5).
//
// First-pass coverage: ProjectData tree round-trip and AerionKeymap
// serialisation/conflict logic. Run headless via the AerionTests console
// target (ctest), so nothing here may open windows or audio devices.

class ProjectDataTests final : public juce::UnitTest
{
public:
    ProjectDataTests() : juce::UnitTest ("ProjectData", "Aerion") {}

    void runTest() override
    {
        beginTest ("constructor seeds project defaults");
        {
            ProjectData pd;
            auto& tree = pd.getProjectTree();
            expect (tree.hasType (IDs::Project));
            expect (tree.getChildWithName (IDs::Tracks).isValid());
            expect (tree.getChildWithName (IDs::AuxTracks).isValid());
            expect ((bool) tree.getProperty (IDs::snapEnabled));
            expectEquals ((double) tree.getProperty (IDs::snapInterval), 1.0);
            expect ((bool) tree.getProperty (IDs::autoCrossfadeEnabled));
            expectEquals ((int) tree.getProperty (IDs::autoCrossfadeMaxMs), 120);
            expect (! (bool) tree.getProperty (IDs::masterKMeter));
        }

        beginTest ("XML round-trip preserves the full tree");
        {
            ProjectData pd;
            pd.createMockData();
            const auto& original = pd.getProjectTree();

            const auto xmlText = original.toXmlString();
            expect (xmlText.isNotEmpty());

            const auto xml = juce::XmlDocument::parse (xmlText);
            expect (xml != nullptr, "round-trip XML must parse");

            const auto reloaded = juce::ValueTree::fromXml (*xml);
            expect (reloaded.isValid());
            expect (reloaded.isEquivalentTo (original),
                    "reloaded tree must be equivalent to the original");
        }

        beginTest ("ProjectSettings XML preserves project-level edit options");
        {
            ProjectData pd;
            auto& tree = pd.getProjectTree();
            tree.setProperty (IDs::snapEnabled, false, nullptr);
            tree.setProperty (IDs::snapInterval, 0.25, nullptr);
            tree.setProperty (IDs::autoCrossfadeEnabled, false, nullptr);
            tree.setProperty (IDs::autoCrossfadeMaxMs, 500, nullptr);
            tree.setProperty (IDs::masterKMeter, true, nullptr);

            auto settings = pd.createProjectSettingsXml();
            expect (settings != nullptr);

            ProjectData restored;
            restored.restoreProjectSettingsFromXml (*settings);
            auto& restoredTree = restored.getProjectTree();

            expect (! (bool) restoredTree.getProperty (IDs::snapEnabled));
            expectEquals ((double) restoredTree.getProperty (IDs::snapInterval), 0.25);
            expect (! (bool) restoredTree.getProperty (IDs::autoCrossfadeEnabled));
            expectEquals ((int) restoredTree.getProperty (IDs::autoCrossfadeMaxMs), 500);
            expect ((bool) restoredTree.getProperty (IDs::masterKMeter));
        }

        beginTest ("getTrackTree resolves tracks by id");
        {
            ProjectData pd;
            pd.createMockData();

            auto lead = pd.getTrackTree (1);
            expect (lead.isValid());
            expectEquals (lead.getProperty (IDs::name).toString(),
                          juce::String ("Lead Vocal"));

            auto drumBus = pd.getTrackTree (juce::String ("4"));
            expect (drumBus.isValid());
            expectEquals (drumBus.getProperty (IDs::type).toString(),
                          juce::String ("folder"));

            expect (! pd.getTrackTree (999).isValid(),
                    "unknown id must return an invalid tree");
        }

        beginTest ("track properties survive the round-trip");
        {
            ProjectData pd;
            pd.createMockData();

            const auto xml = juce::XmlDocument::parse (pd.getProjectTree().toXmlString());
            const auto reloaded = juce::ValueTree::fromXml (*xml);

            auto tracks = reloaded.getChildWithName (IDs::Tracks);
            expectEquals (tracks.getNumChildren(), 4);

            auto t1 = tracks.getChild (0);
            expectEquals ((float) t1.getProperty (IDs::level), 75.0f);
            expect (t1.getChildWithName (IDs::Regions).getNumChildren() == 2);
            expect (t1.getChildWithName (IDs::Inserts).getNumChildren() == 2);
            expect (t1.getChildWithName (IDs::Sends).getNumChildren() == 1);
        }
    }
};

//==============================================================================
class KeymapTests final : public juce::UnitTest
{
public:
    KeymapTests() : juce::UnitTest ("AerionKeymap", "Aerion") {}

    void runTest() override
    {
        beginTest ("defaults bind every rebindable action");
        {
            AerionKeymap km;
            for (auto& a : AerionActionCatalog::actions())
                if (a.rebindable)
                    expect (km.get (a.id).isValid(),
                            "missing default binding for " + a.id);
        }

        beginTest ("matches() honours default bindings, case-insensitively");
        {
            AerionKeymap km;
            const auto ctrlS = juce::KeyPress::createFromDescription ("ctrl + S");
            expect (km.matches ("file.save", ctrlS));
            expect (! km.matches ("file.open", ctrlS));

            // 'q' vs 'Q' must hit the same binding (sameKey normalisation).
            expect (km.matches ("pianoRoll.quantize",
                                juce::KeyPress::createFromDescription ("Q")));
            expect (km.matches ("pianoRoll.quantize",
                                juce::KeyPress::createFromDescription ("q")));
        }

        beginTest ("conflict detection finds duplicate bindings");
        {
            AerionKeymap km;
            const auto ctrlS = juce::KeyPress::createFromDescription ("ctrl + S");

            auto before = km.conflicts (ctrlS, "file.save");
            expect (before.isEmpty(), "default keymap must have no conflicts on ctrl+S");

            km.set ("track.mute", ctrlS);
            auto after = km.conflicts (ctrlS, "track.mute");
            expect (after.contains ("file.save"),
                    "rebinding track.mute to ctrl+S must conflict with file.save");
        }

        beginTest ("export / import round-trip preserves custom bindings");
        {
            AerionKeymap km;
            const auto custom = juce::KeyPress::createFromDescription ("ctrl + shift + P");
            km.set ("transport.playStop", custom);

            auto file = juce::File::createTempFile (".aerionkeys");
            expect (km.exportToFile (file), "export must succeed");

            AerionKeymap restored;
            expect (restored.importFromFile (file), "import must succeed");
            expect (restored.matches ("transport.playStop", custom),
                    "custom binding must survive the round-trip");
            expect (restored.matches ("file.save",
                                      juce::KeyPress::createFromDescription ("ctrl + S")),
                    "untouched bindings must stay at defaults");

            file.deleteFile();
        }

        beginTest ("import rejects files without a keymap root");
        {
            auto file = juce::File::createTempFile (".aerionkeys");
            file.replaceWithText ("<notakeymap/>");

            AerionKeymap km;
            expect (! km.importFromFile (file));

            file.deleteFile();
        }
    }
};

//==============================================================================
static ProjectDataTests projectDataTests;
static KeymapTests keymapTests;

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("Aerion");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "All Aerion smoke tests passed."
                                : "Aerion smoke tests FAILED.")
              << std::endl;
    return failures == 0 ? 0 : 1;
}
