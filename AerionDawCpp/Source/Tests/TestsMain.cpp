#include <JuceHeader.h>
#include <iostream>
#include "../ProjectData.h"
#include "../Keymap.h"
#include "../UI/GraphicsEngine.h"
#include "../UI/UiScale.h"
#include "../UI/Dialogs.h"
#include "../CrashReporter.h"

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
// Choice logic only: applying an engine needs a window, which this runner must
// not open. `AerionBench --verify` checks that windows actually switch.
class GraphicsEngineTests final : public juce::UnitTest
{
public:
    GraphicsEngineTests() : juce::UnitTest ("GraphicsEngine", "Aerion") {}

    void runTest() override
    {
        using namespace GraphicsEngine;

        beginTest ("stored setting maps to a choice, unknown values fall back to Auto");
        {
            expect (choiceFromInt (0) == Choice::automatic);
            expect (choiceFromInt (1) == Choice::hardware);
            expect (choiceFromInt (2) == Choice::software);
            expect (choiceFromInt (7) == Choice::automatic);
            expect (choiceFromInt (-1) == Choice::automatic);
        }

        beginTest ("explicit choices are honoured, Auto follows display size");
        {
            expect (wantsSoftware (Choice::software));
            expect (! wantsSoftware (Choice::hardware));
            expect (wantsSoftware (Choice::automatic) == (largestDisplayPixels() <= kHardwareAutoPixels));
        }
    }
};

//==============================================================================
// Builds each question dialog the way JUCE does (the default look-and-feel's
// createAlertWindow) and checks what each labelled button returns, so a
// reordered button can never again save when the user asked to cancel.
class DialogTests final : public juce::UnitTest
{
public:
    DialogTests() : juce::UnitTest ("Dialogs", "Aerion") {}

    static std::unique_ptr<juce::AlertWindow> build (const juce::MessageBoxOptions& o)
    {
        return std::unique_ptr<juce::AlertWindow> (juce::LookAndFeel::getDefaultLookAndFeel()
            .createAlertWindow (o.getTitle(), o.getMessage(), o.getButtonText (0), o.getButtonText (1),
                                o.getButtonText (2), o.getIconType(), o.getNumButtons(), nullptr));
    }

    static int resultOf (juce::AlertWindow& w, const juce::String& label)
    {
        for (int i = 0; i < w.getNumButtons(); ++i)
            if (w.getButton (i)->getButtonText() == label)
                return w.getButton (i)->getCommandID();

        return -1;
    }

    void runTest() override
    {
        using namespace Dialogs;

        beginTest ("save-changes buttons give the choice they are labelled with");
        {
            auto w = build (saveChangesOptions ("Quit", "Save?", "Save & Quit", "Discard & Quit"));
            expectEquals (w->getNumButtons(), 3);
            expect (saveChoiceFromResult (resultOf (*w, "Save & Quit")) == SaveChoice::save);
            expect (saveChoiceFromResult (resultOf (*w, "Discard & Quit")) == SaveChoice::discard);
            expect (saveChoiceFromResult (resultOf (*w, "Cancel")) == SaveChoice::cancel);
            expect (saveChoiceFromResult (0) == SaveChoice::cancel, "Escape must cancel");
        }

        beginTest ("confirm buttons give the answer they are labelled with");
        {
            auto w = build (confirmOptions ("Crash Recovery", "Restore?", "Restore", "Discard"));
            expectEquals (w->getNumButtons(), 2);
            expect (confirmedFromResult (resultOf (*w, "Restore")));
            expect (! confirmedFromResult (resultOf (*w, "Discard")));
            expect (! confirmedFromResult (0), "Escape must decline");
        }
    }
};

class UiScaleTests final : public juce::UnitTest
{
public:
    UiScaleTests() : juce::UnitTest ("UiScale", "Aerion") {}

    void runTest() override
    {
        using namespace UiScale;

        beginTest ("Auto grows the UI on tall displays");
        expectEquals (autoPercentFor ({ 1920, 1080 }), 100);
        expectEquals (autoPercentFor ({ 2560, 1440 }), 125);
        expectEquals (autoPercentFor ({ 3440, 1440 }), 125);
        expectEquals (autoPercentFor ({ 3840, 2160 }), 150);
        // 4K at 150 % Windows scaling is already 2560 x 1440 in OS-scaled pixels.
        expectEquals (autoPercentFor ({ 2560, 1440 }), 125);
        expectEquals (autoPercentFor ({ 1366, 768 }),  100);

        beginTest ("Auto keeps room for the layout on narrow displays");
        // Tall but narrow (a portrait 1440p display): 1440 / 1.25 < 1280.
        expectEquals (autoPercentFor ({ 1440, 2560 }), 100);
        expectEquals (autoPercentFor ({ 1700, 2160 }), 125);

        beginTest ("explicit sizes are clamped to the supported range");
        expectEquals (percentFor (150), 150);
        expectEquals (percentFor (50),  kMinPercent);
        expectEquals (percentFor (400), kMaxPercent);
    }
};

static DialogTests dialogTests;
static UiScaleTests uiScaleTests;
static ProjectDataTests projectDataTests;
static KeymapTests keymapTests;
static GraphicsEngineTests graphicsEngineTests;

class ConsoleUnitTestRunner final : public juce::UnitTestRunner
{
public:
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};

// The CrashReporter test runs this app as "--crash-into <reports folder> <log file>":
// install the crash handler, then crash.
static int crashInto (const juce::File& reportsFolder, const juce::File& logFile)
{
    CrashReporter::install (reportsFolder, logFile);

    volatile int* volatile nowhere = nullptr;
    *nowhere = 1;
    return 0;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc == 4 && juce::String (argv[1]) == "--crash-into")
        return crashInto (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));

    ConsoleUnitTestRunner runner;
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
