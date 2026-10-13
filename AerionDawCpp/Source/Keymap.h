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
        add ("edit.snap",              "Edit",       "Snap to Grid",         "command + G");

        add ("transport.playStop",     "Transport",  "Play / Stop",          "spacebar");
        add ("transport.record",       "Transport",  "Record",               "command + R");
        add ("transport.goToStart",    "Transport",  "Go to Start",          "home");
        add ("transport.goToEnd",      "Transport",  "Go to End",            "end");
        add ("transport.loop",         "Transport",  "Loop On / Off",        "L");
        add ("transport.metronome",    "Transport",  "Metronome On / Off",   "C");
        add ("transport.follow",       "Transport",  "Follow Playback",      "F");

        add ("tool.select",            "Tools",      "Select Tool",          "1");
        add ("tool.razor",             "Tools",      "Razor Tool",           "2");
        add ("tool.comp",              "Tools",      "Comp Tool",            "3");

        add ("clip.split",             "Clip",       "Split Clip at Playhead", "command + E");
        add ("clip.nudgeLeft",         "Clip",       "Nudge Clip Left",      "cursor left");
        add ("clip.nudgeRight",        "Clip",       "Nudge Clip Right",     "cursor right");
        add ("clip.trimLeft",          "Clip",       "Trim Clip Shorter",    "alt + cursor left");
        add ("clip.trimRight",         "Clip",       "Trim Clip Longer",     "alt + cursor right");
        // The key labelled Delete on a Mac is Backspace; forward Delete needs fn there.
        add ("clip.delete",            "Clip",       "Delete Clip / Track",  "delete", true, "backspace");

        add ("audio.crossfade",        "Audio",      "Force Crossfade",      "X");

        add ("view.zoomIn",            "View",       "Zoom In (Time)",       "H");
        add ("view.zoomOut",           "View",       "Zoom Out (Time)",      "G");
        add ("view.zoomInVertical",    "View",       "Zoom In (Track Height)",  "shift + H");
        add ("view.zoomOutVertical",   "View",       "Zoom Out (Track Height)", "shift + G");
        add ("view.fitProject",        "View",       "Zoom to Project",      "shift + F");
        add ("view.fitTracks",         "View",       "Fit All Tracks",       "shift + T");
        add ("view.zoomToLoop",        "View",       "Zoom to Loop Range",   "shift + L");
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
// Keymap presets: the keys another DAW uses for Aerion's actions, so someone
// coming from it can keep their habits. A preset lists only what differs from
// Aerion's defaults; an Aerion default whose key the preset gives to another
// action is left unassigned. Keys are each DAW's factory defaults as far as
// they have an Aerion equivalent. "command" is Ctrl on Windows, Cmd on macOS.
struct AerionKeymapPreset
{
    struct Binding
    {
        const char* id;
        const char* key;          // "" = unassigned
        const char* macKey = nullptr;   // macOS key when it differs
    };

    const char* id;
    const char* name;
    std::vector<Binding> bindings;

    static const std::vector<AerionKeymapPreset>& all()
    {
        static const std::vector<AerionKeymapPreset> presets {
            { "aerion", "Aerion", {} },

            { "cubase", "Cubase", {
                { "transport.record",    "numpad *" },
                { "transport.loop",      "numpad /" },
                { "clip.split",          "alt + X" },
                { "clip.nudgeLeft",      "command + cursor left" },
                { "clip.nudgeRight",     "command + cursor right" },
                { "tool.select",         "1" },
                { "tool.razor",          "3" },
                { "tool.comp",           "" },
                { "edit.snap",           "J" } } },

            { "protools", "Pro Tools", {
                { "transport.record",    "command + spacebar" },
                { "transport.goToStart", "return" },
                { "transport.goToEnd",   "alt + return" },
                { "transport.loop",      "command + shift + L" },
                { "transport.metronome", "numpad 7" },
                { "view.zoomIn",         "command + ]" },
                { "view.zoomOut",        "command + [" },
                { "view.zoomInVertical", "command + alt + ]" },
                { "view.zoomOutVertical","command + alt + [" },
                { "view.fitProject",     "alt + A" },
                { "tool.select",         "F8" },
                { "tool.razor",          "" },
                { "tool.comp",           "" },
                { "edit.snap",           "F4" },
                { "track.mute",          "shift + M" },
                { "track.solo",          "shift + S" },
                { "track.arm",           "shift + R" } } },

            { "logic", "Logic Pro", {
                { "transport.record",    "R" },
                { "transport.goToStart", "return" },
                { "transport.loop",      "C" },
                { "transport.metronome", "K" },
                { "clip.split",          "command + T" },
                { "view.zoomIn",         "command + cursor right" },
                { "view.zoomOut",        "command + cursor left" },
                { "view.zoomInVertical", "command + cursor down" },
                { "view.zoomOutVertical","command + cursor up" },
                { "view.fitProject",     "Z" },
                { "tool.select",         "" },
                { "tool.razor",          "" },
                { "tool.comp",           "" } } },

            { "ableton", "Ableton Live", {
                { "edit.redo",           "command + Y", "command + shift + Z" },
                { "edit.snap",           "command + 4" },
                { "transport.record",    "F9" },
                { "transport.loop",      "command + L" },
                { "transport.metronome", "O" },
                { "transport.follow",    "command + shift + F" },
                { "view.zoomIn",         "=" },
                { "view.zoomOut",        "-" },
                { "view.zoomInVertical", "alt + =" },
                { "view.zoomOutVertical","alt + -" },
                { "view.fitProject",     "W" },
                { "view.fitTracks",      "H" },
                { "tool.select",         "" },
                { "tool.razor",          "" },
                { "tool.comp",           "" },
                { "pianoRoll.quantize",  "command + U" } } },

            { "studioone", "Studio One", {
                { "transport.record",    "numpad *" },
                { "transport.loop",      "numpad /" },
                { "clip.split",          "alt + X" },
                { "view.zoomIn",         "E" },
                { "view.zoomOut",        "W" },
                { "view.zoomInVertical", "shift + E" },
                { "view.zoomOutVertical","shift + W" },
                { "tool.select",         "1" },
                { "tool.razor",          "3" },
                { "tool.comp",           "" },
                { "edit.snap",           "N" },
                { "track.mute",          "shift + M" },
                { "track.solo",          "shift + S" },
                { "track.arm",           "shift + R" } } },

            { "reaper", "REAPER", {
                { "transport.loop",      "R" },
                { "clip.split",          "S" },
                { "view.zoomIn",         "=" },
                { "view.zoomOut",        "-" },
                { "view.zoomInVertical", "page up" },
                { "view.zoomOutVertical","page down" },
                { "view.fitProject",     "command + page down" },
                { "tool.select",         "" },
                { "tool.razor",          "" },
                { "tool.comp",           "" },
                { "edit.snap",           "alt + S" } } },
        };
        return presets;
    }

    static const AerionKeymapPreset* find (const juce::String& presetId)
    {
        for (auto& p : all())
            if (presetId == p.id)
                return &p;
        return nullptr;
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
        presetId = "aerion";
        for (auto& a : AerionActionCatalog::actions())
            if (a.rebindable)
                bindings[a.id] = juce::KeyPress::createFromDescription (a.platformDefaultKey());
    }

    /** The preset the bindings started from ("aerion", "cubase", ...); keys
        changed by hand since are kept in the bindings, not here. */
    const juce::String& getPresetId() const noexcept   { return presetId; }

    /** True when a key has been changed by hand since the preset was applied. */
    bool differsFromPreset() const
    {
        AerionKeymap fresh;
        fresh.applyPreset (presetId);
        for (auto& [id, key] : bindings)
        {
            const auto other = fresh.get (id);
            if (key.isValid() != other.isValid() || (key.isValid() && ! sameKey (key, other)))
                return true;
        }
        return false;
    }

    /** Replaces every binding with `preset`'s keys (Aerion's defaults for
        actions it leaves out). Unknown ids give Aerion's defaults. */
    void applyPreset (const juce::String& id)
    {
       #if JUCE_MAC
        applyPreset (id, true);
       #else
        applyPreset (id, false);
       #endif
    }

    void applyPreset (const juce::String& id, bool mac)
    {
        // Defaults for `mac` rather than this platform, so tests can check both.
        resetToDefaults();
        for (auto& a : AerionActionCatalog::actions())
            if (a.rebindable)
                bindings[a.id] = juce::KeyPress::createFromDescription (a.defaultKeyFor (mac));

        auto* preset = AerionKeymapPreset::find (id);
        if (preset == nullptr)
            return;

        presetId = preset->id;
        juce::StringArray presetIds;
        for (auto& b : preset->bindings)
        {
            if (bindings.count (b.id) == 0)
                continue;
            const juce::String key = mac && b.macKey != nullptr ? b.macKey : b.key;
            bindings[b.id] = key.isEmpty() ? juce::KeyPress() : juce::KeyPress::createFromDescription (key);
            presetIds.add (b.id);
        }

        unassignDefaultsTakenBy (presetIds);
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

    /** Piano Roll keys act only while the Piano Roll has focus, which takes
        them first, so they may repeat keys from the rest of Aerion. */
    static bool sameScope (const juce::String& a, const juce::String& b)
    {
        return a.startsWith ("pianoRoll.") == b.startsWith ("pianoRoll.");
    }

    // Returns ids of any other rebindable actions already bound to this key,
    // in the same scope as `excludeId` (the action being bound).
    juce::StringArray conflicts (const juce::KeyPress& key, const juce::String& excludeId) const
    {
        juce::StringArray out;
        if (! key.isValid())
            return out;
        for (auto& b : bindings)
            if (b.first != excludeId && sameScope (b.first, excludeId)
                 && b.second.isValid() && sameKey (b.second, key))
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
        xml->setAttribute ("preset", presetId);
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

        // Start from the preset the keymap was made from, so actions added
        // since it was saved get that preset's keys.
        applyPreset (xml.getStringAttribute ("preset", "aerion"), mac);

        juce::StringArray savedIds;
        for (auto* e : xml.getChildWithTagNameIterator ("binding"))
        {
            auto id = e->getStringAttribute ("id");
            if (id.isEmpty() || bindings.count (id) == 0)
                continue;

            for (auto& a : catalog)
                if (a.id == id)
                {
                    bindings[id] = keyFromSaved (a, e->getStringAttribute ("key"), version, mac);
                    savedIds.add (id);
                }
        }

        // An action new since the keymap was saved must not take a key the
        // user already gave to something else.
        unassignDefaultsTakenBy (savedIds);
    }

    /** Clears every binding not in `kept` whose key one of `kept` uses. */
    void unassignDefaultsTakenBy (const juce::StringArray& kept)
    {
        for (auto& [id, key] : bindings)
        {
            if (! key.isValid() || kept.contains (id))
                continue;
            for (auto& k : kept)
                if (sameScope (id, k) && bindings[k].isValid() && sameKey (key, bindings[k]))
                {
                    key = juce::KeyPress();
                    break;
                }
        }
    }

    std::map<juce::String, juce::KeyPress> bindings;
    juce::String presetId { "aerion" };
};
