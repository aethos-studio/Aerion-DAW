#pragma once
#include <JuceHeader.h>
#include <map>

//==============================================================================
// Editable keyboard-shortcut model for Aerion DAW.
//
// AerionActionCatalog defines every shortcut-able action with a stable id and a
// default key. AerionKeymap holds the (possibly user-customised) bindings,
// performs conflict detection, and persists to the app PropertiesFile.

struct AerionAction
{
    juce::String id;          // stable identifier, e.g. "transport.playStop"
    juce::String section;     // grouping label for the editor UI
    juce::String name;        // human-readable action name
    juce::String defaultKey;  // KeyPress description, or gesture text if !rebindable
    bool rebindable = true;   // false => mouse-gesture row, informational only
};

struct AerionActionCatalog
{
    static juce::Array<AerionAction> actions()
    {
        juce::Array<AerionAction> a;
        auto add = [&] (const char* id, const char* sec, const char* nm,
                        const char* def, bool reb = true)
        {
            a.add ({ juce::String (id), juce::String (sec), juce::String (nm),
                     juce::String (def), reb });
        };

        add ("file.new",               "File",       "New Project",          "ctrl + N");
        add ("file.open",              "File",       "Open Project",         "ctrl + O");
        add ("file.save",              "File",       "Save Project",         "ctrl + S");

        add ("edit.undo",              "Edit",       "Undo",                 "ctrl + Z");
        add ("edit.redo",              "Edit",       "Redo",                 "ctrl + shift + Z");

        add ("transport.playStop",     "Transport",  "Play / Stop",          "spacebar");
        add ("transport.record",       "Transport",  "Record",               "ctrl + R");
        add ("transport.goToStart",    "Transport",  "Go to Start",          "home");

        add ("clip.nudgeLeft",         "Clip",       "Nudge Clip Left",      "cursor left");
        add ("clip.nudgeRight",        "Clip",       "Nudge Clip Right",     "cursor right");
        add ("clip.trimLeft",          "Clip",       "Trim Clip Shorter",    "alt + cursor left");
        add ("clip.trimRight",         "Clip",       "Trim Clip Longer",     "alt + cursor right");
        add ("clip.delete",            "Clip",       "Delete Clip / Track",  "delete");

        add ("audio.crossfade",        "Audio",      "Force Crossfade",      "X");

        add ("track.mute",             "Track",      "Toggle Mute",          "M");
        add ("track.solo",             "Track",      "Toggle Solo",          "S");
        add ("track.arm",              "Track",      "Toggle Record Arm",    "R");

        add ("pianoRoll.selectAll",    "Piano Roll", "Select All Notes",     "ctrl + A");
        add ("pianoRoll.copy",         "Piano Roll", "Copy Notes",           "ctrl + C");
        add ("pianoRoll.cut",          "Piano Roll", "Cut Notes",            "ctrl + X");
        add ("pianoRoll.paste",        "Piano Roll", "Paste Notes",          "ctrl + V");
        add ("pianoRoll.duplicate",    "Piano Roll", "Duplicate Notes",      "ctrl + D");
        add ("pianoRoll.delete",       "Piano Roll", "Delete Notes",         "delete");
        add ("pianoRoll.nudgeLeft",    "Piano Roll", "Nudge Notes Left",     "cursor left");
        add ("pianoRoll.nudgeRight",   "Piano Roll", "Nudge Notes Right",    "cursor right");
        add ("pianoRoll.transposeUp",  "Piano Roll", "Transpose Up",         "cursor up");
        add ("pianoRoll.transposeDown","Piano Roll", "Transpose Down",       "cursor down");
        add ("pianoRoll.quantize",     "Piano Roll", "Quantize Notes",       "Q");
        add ("pianoRoll.clearSel",     "Piano Roll", "Clear Selection",      "escape");

        // Mouse gestures — shown for reference, not rebindable.
        add ("gesture.loopRange", "Timeline", "Create Loop Range", "Alt+Click Ruler",         false);
        add ("gesture.addMarker", "Timeline", "Add Marker",        "Shift+Click Ruler",       false);
        add ("gesture.addTempo",  "Timeline", "Add Tempo Node",    "Double-click Tempo Lane", false);
        add ("gesture.delTempo",  "Timeline", "Delete Tempo Node", "Right-click Tempo Node",  false);

        return a;
    }
};

//==============================================================================
class AerionKeymap
{
public:
    AerionKeymap() { resetToDefaults(); }

    void resetToDefaults()
    {
        bindings.clear();
        for (auto& a : AerionActionCatalog::actions())
            if (a.rebindable)
                bindings[a.id] = juce::KeyPress::createFromDescription (a.defaultKey);
    }

    juce::KeyPress get (const juce::String& id) const
    {
        auto it = bindings.find (id);
        return it != bindings.end() ? it->second : juce::KeyPress();
    }

    void set (const juce::String& id, const juce::KeyPress& kp) { bindings[id] = kp; }

    bool matches (const juce::String& id, const juce::KeyPress& key) const
    {
        auto it = bindings.find (id);
        return it != bindings.end() && it->second.isValid() && sameKey (it->second, key);
    }

    // Returns ids of any other rebindable actions already bound to this key.
    juce::StringArray conflicts (const juce::KeyPress& key, const juce::String& excludeId) const
    {
        juce::StringArray out;
        if (! key.isValid())
            return out;
        for (auto& b : bindings)
            if (b.first != excludeId && b.second.isValid() && sameKey (b.second, key))
                out.add (b.first);
        return out;
    }

    static juce::String describe (const juce::KeyPress& kp)
    {
        return kp.isValid() ? kp.getTextDescription() : juce::String ("Unassigned");
    }

    //== persistence ===========================================================
    void loadFrom (juce::PropertiesFile* p)
    {
        resetToDefaults();
        if (p == nullptr)
            return;
        if (auto xml = p->getXmlValue ("keymap"))
            applyXml (*xml);
    }

    void saveTo (juce::PropertiesFile* p) const
    {
        if (p == nullptr)
            return;
        p->setValue ("keymap", toXml().get());
        p->saveIfNeeded();
    }

    bool exportToFile (const juce::File& f) const
    {
        return toXml()->writeTo (f);
    }

    bool importFromFile (const juce::File& f)
    {
        if (auto xml = juce::XmlDocument::parse (f))
        {
            if (! xml->hasTagName ("keymap"))
                return false;
            resetToDefaults();
            applyXml (*xml);
            return true;
        }
        return false;
    }

    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("keymap");
        for (auto& a : AerionActionCatalog::actions())
        {
            if (! a.rebindable)
                continue;
            auto it = bindings.find (a.id);
            if (it == bindings.end())
                continue;
            auto* e = xml->createNewChildElement ("binding");
            e->setAttribute ("id", a.id);
            e->setAttribute ("key", it->second.getTextDescription());
        }
        return xml;
    }

    static bool sameKey (const juce::KeyPress& a, const juce::KeyPress& b)
    {
        auto norm = [] (int kc) { return (kc >= 'a' && kc <= 'z') ? kc - 'a' + 'A' : kc; };
        if (norm (a.getKeyCode()) != norm (b.getKeyCode()))
            return false;
        auto ma = a.getModifiers();
        auto mb = b.getModifiers();
        return ma.isCommandDown() == mb.isCommandDown()
            && ma.isShiftDown()   == mb.isShiftDown()
            && ma.isAltDown()     == mb.isAltDown();
    }

private:
    void applyXml (const juce::XmlElement& xml)
    {
        for (auto* e : xml.getChildWithTagNameIterator ("binding"))
        {
            auto id = e->getStringAttribute ("id");
            if (id.isNotEmpty() && bindings.count (id) != 0)
                bindings[id] = juce::KeyPress::createFromDescription (e->getStringAttribute ("key"));
        }
    }

    std::map<juce::String, juce::KeyPress> bindings;
};
