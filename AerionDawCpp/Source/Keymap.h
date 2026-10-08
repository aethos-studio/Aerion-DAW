#pragma once
#include <JuceHeader.h>
#include <map>

//==============================================================================
// Editable keyboard-shortcut model for Aerion DAW.
//
// AerionActionCatalog defines every shortcut-able action with a stable id and a
// default key. AerionKeymap holds the (possibly user-customised) bindings,
// performs conflict detection, and persists to the app PropertiesFile.

// Defaults use "command", which JUCE maps to Ctrl on Windows and Linux and to
// Cmd on macOS, so each platform gets the modifier its users expect.
struct AerionAction
{
    juce::String id;          // stable identifier, e.g. "transport.playStop"
    juce::String section;     // grouping label for the editor UI
    juce::String name;        // human-readable action name
    juce::String defaultKey;  // KeyPress description, or gesture text if !rebindable
    bool rebindable = true;   // false => mouse-gesture row, informational only
    juce::String macDefaultKey;   // macOS default when it differs, e.g. the Mac Delete key

    juce::String defaultKeyFor (bool mac) const
    {
        return mac && macDefaultKey.isNotEmpty() ? macDefaultKey : defaultKey;
    }

    juce::String platformDefaultKey() const
    {
       #if JUCE_MAC
        return defaultKeyFor (true);
       #else
        return defaultKeyFor (false);
       #endif
    }

    /** The default in keymaps saved before version 2, which used Ctrl (on
        macOS the physical Control key) and the forward Delete key. */
    juce::String version1DefaultKey() const   { return defaultKey.replace ("command", "ctrl"); }
};

struct AerionActionCatalog
{
    static juce::Array<AerionAction> actions()
    {
        juce::Array<AerionAction> a;
        auto add = [&] (const char* id, const char* sec, const char* nm,
                        const char* def, bool reb = true, const char* macDef = "")
        {
            a.add ({ juce::String (id), juce::String (sec), juce::String (nm),
                     juce::String (def), reb, juce::String (macDef) });
        };

        add ("file.new",               "File",       "New Project",          "command + N");
        add ("file.open",              "File",       "Open Project",         "command + O");
        add ("file.save",              "File",       "Save Project",         "command + S");

        add ("edit.undo",              "Edit",       "Undo",                 "command + Z");
        add ("edit.redo",              "Edit",       "Redo",                 "command + shift + Z");

        add ("transport.playStop",     "Transport",  "Play / Stop",          "spacebar");
        add ("transport.record",       "Transport",  "Record",               "command + R");
        add ("transport.goToStart",    "Transport",  "Go to Start",          "home");
        add ("transport.follow",       "Transport",  "Follow Playback",      "F");

        add ("clip.nudgeLeft",         "Clip",       "Nudge Clip Left",      "cursor left");
        add ("clip.nudgeRight",        "Clip",       "Nudge Clip Right",     "cursor right");
        add ("clip.trimLeft",          "Clip",       "Trim Clip Shorter",    "alt + cursor left");
        add ("clip.trimRight",         "Clip",       "Trim Clip Longer",     "alt + cursor right");
        // The key labelled Delete on a Mac is Backspace; forward Delete needs fn there.
        add ("clip.delete",            "Clip",       "Delete Clip / Track",  "delete", true, "backspace");

        add ("audio.crossfade",        "Audio",      "Force Crossfade",      "X");

        add ("view.nextPane",          "View",       "Focus Next Pane",      "F6");
        add ("app.settings",           "View",       "Audio Settings",       "command + ,");

        add ("track.mute",             "Track",      "Toggle Mute",          "M");
        add ("track.solo",             "Track",      "Toggle Solo",          "S");
        add ("track.arm",              "Track",      "Toggle Record Arm",    "R");

        add ("pianoRoll.selectAll",    "Piano Roll", "Select All Notes",     "command + A");
        add ("pianoRoll.copy",         "Piano Roll", "Copy Notes",           "command + C");
        add ("pianoRoll.cut",          "Piano Roll", "Cut Notes",            "command + X");
        add ("pianoRoll.paste",        "Piano Roll", "Paste Notes",          "command + V");
        add ("pianoRoll.duplicate",    "Piano Roll", "Duplicate Notes",      "command + D");
        add ("pianoRoll.delete",       "Piano Roll", "Delete Notes",         "delete", true, "backspace");
        add ("pianoRoll.nudgeLeft",    "Piano Roll", "Nudge Notes Left",     "cursor left");
        add ("pianoRoll.nudgeRight",   "Piano Roll", "Nudge Notes Right",    "cursor right");
        add ("pianoRoll.transposeUp",  "Piano Roll", "Transpose Up",         "cursor up");
        add ("pianoRoll.transposeDown","Piano Roll", "Transpose Down",       "cursor down");
        add ("pianoRoll.quantize",     "Piano Roll", "Quantize Notes",       "Q");
        add ("pianoRoll.clearSel",     "Piano Roll", "Clear Selection",      "escape");

        // Mouse gestures — shown for reference, not rebindable.
        add ("gesture.loopRange", "Timeline", "Create Loop Range", "Alt+Click Ruler",         false, "Option+Click Ruler");
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
                bindings[a.id] = juce::KeyPress::createFromDescription (a.platformDefaultKey());
    }

    /** Saved keymaps carry this; version 2 moved the defaults from Ctrl to
        Command (Cmd on macOS) and the Mac delete keys to the Mac Delete key. */
    static constexpr int kFormatVersion = 2;

    /** The key a saved binding stands for. On macOS a keymap saved before
        version 2 kept every default it did not change, and those were Ctrl
        (the physical Control key) and forward Delete; such a binding moves to
        today's default. Bindings the user chose are kept. */
    static juce::KeyPress keyFromSaved (const AerionAction& a, const juce::String& saved, int version, bool mac)
    {
        const auto key = juce::KeyPress::createFromDescription (saved);
        if (mac && version < 2
             && sameKey (key, juce::KeyPress::createFromDescription (a.version1DefaultKey())))
            return juce::KeyPress::createFromDescription (a.defaultKeyFor (true));
        return key;
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

    /** A key as menus show it: "Ctrl+Shift+Z" on Windows, "⇧⌘Z" on macOS. */
    static juce::String menuText (const juce::KeyPress& kp)
    {
        if (! kp.isValid())
            return {};

       #if JUCE_MAC
        return kp.getTextDescriptionWithIcons();
       #else
        juce::StringArray parts;
        for (auto part : juce::StringArray::fromTokens (kp.getTextDescription(), "+", ""))
        {
            part = part.trim();
            if (part == "spacebar")
                part = "Space";
            parts.add (part.substring (0, 1).toUpperCase() + part.substring (1));
        }
        return parts.joinIntoString ("+");
       #endif
    }

    /** "\t" plus the key bound to `id`, to follow a menu item's name. */
    juce::String menuHint (const juce::String& id) const
    {
        const auto text = menuText (get (id));
        return text.isNotEmpty() ? "\t" + text : juce::String();
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
        xml->setAttribute ("version", kFormatVersion);
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
        // Compare Ctrl explicitly as well as Command: on macOS they are distinct
        // physical keys, so "ctrl + S" must not collide with a bare "S". On
        // Windows Ctrl maps to the command modifier, so this stays consistent.
        return ma.isCommandDown() == mb.isCommandDown()
            && ma.isCtrlDown()    == mb.isCtrlDown()
            && ma.isShiftDown()   == mb.isShiftDown()
            && ma.isAltDown()     == mb.isAltDown();
    }

private:
    void applyXml (const juce::XmlElement& xml)
    {
       #if JUCE_MAC
        constexpr bool mac = true;
       #else
        constexpr bool mac = false;
       #endif
        const int version = xml.getIntAttribute ("version", 1);
        const auto catalog = AerionActionCatalog::actions();

        for (auto* e : xml.getChildWithTagNameIterator ("binding"))
        {
            auto id = e->getStringAttribute ("id");
            if (id.isEmpty() || bindings.count (id) == 0)
                continue;

            for (auto& a : catalog)
                if (a.id == id)
                    bindings[id] = keyFromSaved (a, e->getStringAttribute ("key"), version, mac);
        }
    }

    std::map<juce::String, juce::KeyPress> bindings;
};
