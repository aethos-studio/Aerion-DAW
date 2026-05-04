#pragma once
#include <JuceHeader.h>
#include "ProjectData.h"
#include "AudioEngine.h"
#include "GoogleDriveClient.h"

namespace Theme
{
    const juce::Colour bgBase        = juce::Colour::fromString("#ff080a0e"); // Deep Charcoal
    const juce::Colour bgPanel       = juce::Colour::fromString("#ff11141a"); // Cool Dark Blue-Gray
    const juce::Colour surface       = juce::Colour::fromString("#ff1c212b"); // Dark Slate
    const juce::Colour border        = juce::Colour::fromString("#ff2d3748"); // Metallic Slate
    const juce::Colour accent        = juce::Colour::fromString("#ff63b3ed"); // Arctic Blue (Faint)
    const juce::Colour active        = juce::Colour::fromString("#ff3182ce"); // Ice Blue
    const juce::Colour textMain      = juce::Colour::fromString("#fff0f4f8"); // Off-White
    const juce::Colour textMuted     = juce::Colour::fromString("#ff8a99a8"); // Steel Gray
    const juce::Colour playhead      = juce::Colour::fromString("#ffebf8ff"); // Polar White
    const juce::Colour meterGreen    = juce::Colour::fromString("#ff48bb78"); // Arctic Emerald
    const juce::Colour meterYellow   = juce::Colour::fromString("#ffecc94b"); // Gold-Tinted Steel
    const juce::Colour meterRed      = juce::Colour::fromString("#fff56565"); // Cold Crimson
    const juce::Colour recordRed     = juce::Colour::fromString("#ffe53e3e"); // Warning Red

    const juce::Colour trackColours[6] = {
        juce::Colour::fromString("#ff3182ce"), // Ice Blue
        juce::Colour::fromString("#ff4a5568"), // Deep Steel
        juce::Colour::fromString("#ff63b3ed"), // Arctic Blue
        juce::Colour::fromString("#ffa0aec0"), // Silver
        juce::Colour::fromString("#ff2c5282"), // Dark Arctic
        juce::Colour::fromString("#ff4299e1")  // Sky Steel
    };

    inline juce::Colour colourForTrack (int idx) { return trackColours[((unsigned)idx) % 6]; }

    static void drawRoundedPanel(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour color = surface, float alpha = 1.0f)
    {
        g.setColour(color.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(border.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(b, 4.0f, 1.0f);
    }
}

enum class EditTool
{
    select,
    razor
};

//==============================================================================
inline void setFaderFromY (AudioEngineManager& audioEngine, tracktion::Track* t, juce::Rectangle<int> area, int y)
{
    if (! t) return;
    int faderTop = area.getY() + 4;
    int faderH   = area.getHeight() - 28;
    float sPos = 1.0f - juce::jlimit(0.0f, 1.0f, (float)(y - faderTop) / (float)juce::jmax(1, faderH));
    audioEngine.setTrackVolumeDb (t, AudioEngineManager::getDbFromFaderPos (sPos));
}

inline void paintFader (juce::Graphics& g, juce::Rectangle<int> area,
                 AudioEngineManager& audioEngine,
                 tracktion::Track* track, juce::Colour tColor, bool isMaster,
                 juce::Drawable* faderKnobDrawable,
                 juce::Rectangle<int>* readoutAreaOut = nullptr)
{
    if (area.getHeight() < 30) return;

    int track_x   = area.getCentreX() - 3;
    int meter_w   = 12;
    int meter_x   = area.getCentreX() + 18;
    int track_w   = 6;
    int faderTop  = area.getY() + 4;
    int faderH    = area.getHeight() - 28;

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle ((float) track_x, (float) faderTop, (float) track_w, (float) faderH, 3.0f);

    // 0dB tick mark.
    float zeroPos = AudioEngineManager::getFaderPosFromDb (0.0f);
    int   zeroY   = faderTop + (int) (faderH * (1.0f - zeroPos));
    g.setColour (Theme::border.withAlpha (0.4f));
    g.drawHorizontalLine (zeroY, (float) (track_x - 4), (float) (track_x + track_w + 4));

    // Meter
    float peak = audioEngine.getTrackPeak (track);
    float pPos = AudioEngineManager::getFaderPosFromDb (peak);

    // Meter background slot (always visible)
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle ((float) meter_x, (float) faderTop, (float) meter_w, (float) faderH, 2.0f);
    g.setColour (Theme::border.withAlpha (0.6f));
    g.drawRoundedRectangle ((float) meter_x, (float) faderTop, (float) meter_w, (float) faderH, 2.0f, 1.0f);

    // Clip indicator light (top of slot)
    if (peak > 0.0f)
    {
        g.setColour (Theme::recordRed);
        g.fillRoundedRectangle ((float) meter_x, (float) faderTop, (float) meter_w, 6.0f, 1.5f);
    }

    if (pPos > 0.0f)
    {
        int mY = faderTop + (int) (faderH * (1.0f - pPos));
        // Multi-stop gradient: Red (>0dB), Yellow (0 to -12dB), Green (<-12dB)
        juce::ColourGradient mg (Theme::meterRed, 0, (float) faderTop,
                                 Theme::meterGreen, 0, (float) (faderTop + faderH), false);
        mg.addColour (0.17, Theme::meterYellow); // 0dB threshold
        mg.addColour (0.33, Theme::meterGreen);  // -12dB threshold
        g.setGradientFill (mg);
        g.fillRoundedRectangle ((float) meter_x, (float) mY, (float) meter_w, (float) (faderTop + faderH - mY), 2.0f);
    }

    // Fader cap.
    float db    = audioEngine.getTrackVolumeDb (track);
    float sPos  = AudioEngineManager::getFaderPosFromDb (db);
    int   capY  = faderTop + (int) (faderH * (1.0f - sPos));
    juce::Rectangle<float> cap ((float) (area.getCentreX() - 9), (float) (capY - 24), 18.0f, 48.0f);
    if (faderKnobDrawable != nullptr)
        faderKnobDrawable->drawWithin (g, cap, juce::RectanglePlacement::centred, 1.0f);

    // dB readout.
    float maxPeak = audioEngine.getTrackMaxPeak (track);
    bool clipping = maxPeak > 0.0f;
    g.setColour (clipping ? Theme::recordRed : Theme::textMuted);
    g.setFont (juce::Font (9.0f).withStyle (juce::Font::bold));
    juce::String dbText = (maxPeak > -90.0f) 
        ? juce::String::formatted ("%+.1f", maxPeak)
        : (track ? juce::String::formatted ("%+.1f dB", db) : juce::String ("MASTER"));

    auto readoutArea = area.withY (area.getBottom() - 16).withHeight (14);
    g.drawText (dbText, readoutArea, juce::Justification::centred);
    if (readoutAreaOut) *readoutAreaOut = readoutArea;
}

//==============================================================================
/** A custom LookAndFeel that implements the "Metal" arctic theme for all
    standard JUCE widgets, including window title bar buttons.
*/
class MetalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MetalLookAndFeel()
    {
        auto scheme = getDarkColourScheme();
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground, Theme::bgBase);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground, Theme::bgPanel);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuBackground, Theme::bgPanel);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::outline, Theme::border);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText, Theme::textMain);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill, Theme::accent);
        scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedFill, Theme::active);
        setColourScheme (scheme);

        setColour (juce::TextButton::buttonColourId, Theme::surface);
        setColour (juce::TextButton::textColourOffId, Theme::textMain);
        setColour (juce::ListBox::backgroundColourId, Theme::bgPanel);
        setColour (juce::Label::textColourId, Theme::textMain);
    }

    // Custom button for window decorations
    class WindowButton : public juce::Button
    {
    public:
        WindowButton (const juce::String& name, juce::Colour c) : juce::Button (name), color (c) {}
        void paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
        {
            // Use precise integer coordinates for maximum crispness
            auto b = getLocalBounds().toFloat();
            auto c = color;
            if (isMouseDown)      c = c.darker (0.3f);
            else if (isMouseOver) c = c.brighter (0.1f);
            
            // Subtle, sharp circle
            g.setColour (c.withAlpha (isMouseOver ? 0.95f : 0.75f));
            g.fillEllipse (b);

            // Hairline outline
            g.setColour (c.brighter (0.3f).withAlpha (0.4f));
            g.drawEllipse (b, 1.0f);

            // Modern, pixel-perfect icons
            g.setColour (Theme::bgBase.withAlpha (0.9f));
            auto iconArea = b.reduced (b.getWidth() * 0.32f);
            const float thickness = 1.0f; // Hairline thickness for elegance

            if (getName() == "close") {
                g.drawLine (iconArea.getX(), iconArea.getY(), iconArea.getRight(), iconArea.getBottom(), thickness);
                g.drawLine (iconArea.getRight(), iconArea.getY(), iconArea.getX(), iconArea.getBottom(), thickness);
            } else if (getName() == "min") {
                g.drawLine (iconArea.getX(), iconArea.getCentreY(), iconArea.getRight(), iconArea.getCentreY(), thickness);
            } else { // max
                g.drawRect (iconArea, thickness);
            }
        }
    private:
        juce::Colour color;
    };

    juce::Button* createDocumentWindowButton (int buttonType) override
    {
        if (buttonType == juce::DocumentWindow::closeButton)
            return new WindowButton ("close", Theme::accent); // Arctic Blue
        if (buttonType == juce::DocumentWindow::minimiseButton)
            return new WindowButton ("min", Theme::textMuted); // Steel Gray
        if (buttonType == juce::DocumentWindow::maximiseButton)
            return new WindowButton ("max", Theme::trackColours[3]); // Silver
        return nullptr;
    }

    void positionDocumentWindowButtons (juce::DocumentWindow&, int x, int y, int w, int h,
                                        juce::Button* minimiseButton,
                                        juce::Button* maximiseButton,
                                        juce::Button* closeButton,
                                        bool positionOnLeft) override
    {
        // Make buttons significantly smaller and more compact (14x14 instead of default ~24x24)
        const int size = 14;
        const int gap = 8;
        int curX = positionOnLeft ? x + 8 : x + w - size - 8;
        const int curY = y + (h - size) / 2;

        juce::Button* buttons[] = { closeButton, maximiseButton, minimiseButton };
        if (positionOnLeft) std::reverse (std::begin(buttons), std::end(buttons));

        for (auto* b : buttons)
        {
            if (b != nullptr)
            {
                b->setBounds (curX, curY, size, size);
                curX += positionOnLeft ? (size + gap) : -(size + gap);
            }
        }
    }
};

//==============================================================================
// Shared plugin picker — shows a manufacturer-grouped popup of every
// scanned plugin. Used by track FX buttons in Timeline + Mixer.
namespace PluginPicker
{
    inline void show (AudioEngineManager& ae,
                       juce::Rectangle<int> screenAnchor,
                       std::function<void (const juce::PluginDescription&)> onPicked)
    {
        auto& known = ae.getEngine().getPluginManager().knownPluginList;
        auto types  = known.getTypes();

        juce::PopupMenu menu;

        if (types.isEmpty())
        {
            menu.addItem (1, ae.isScanningPlugins() ? "Scanning plugins..." : "No plugins scanned",
                          /*enabled*/ false, /*ticked*/ false);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenAnchor));
            return;
        }

        // Group by manufacturer.
        std::map<juce::String, juce::Array<juce::PluginDescription>> grouped;
        for (auto& d : types) grouped[d.manufacturerName.isEmpty() ? "Other" : d.manufacturerName].add (d);

        // Build a flat lookup by menu id.
        auto descs = std::make_shared<juce::Array<juce::PluginDescription>>();
        int id = 1;
        for (auto& [mfg, list] : grouped)
        {
            juce::PopupMenu sub;
            for (auto& d : list)
            {
                sub.addItem (id++, d.name);
                descs->add (d);
            }
            menu.addSubMenu (mfg, sub);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenAnchor),
                            [descs, onPicked] (int chosen)
                            {
                                if (chosen <= 0) return;
                                int idx = chosen - 1;
                                if (idx >= 0 && idx < descs->size() && onPicked)
                                    onPicked (descs->getReference (idx));
                            });
    }
}

//==============================================================================
// Lists plugins on a track and allows opening their editors.
class PluginManagerWindow : public juce::DocumentWindow
{
public:
    PluginManagerWindow (tracktion::Track* t, AudioEngineManager& ae)
        : DocumentWindow (t->getName() + " — Plugins", Theme::bgPanel,
                          DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          track (t), audioEngine (ae)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (28);
        setColour (DocumentWindow::textColourId, Theme::textMain);
        setSize (350, 450);
        
        auto* content = new Content (t, ae);
        setContentOwned (content, true);
        
        centreWithSize (350, 450);
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override { delete this; }

private:
    struct Content : public juce::Component,
                     public juce::Timer
    {
        Content (tracktion::Track* t, AudioEngineManager& ae) : track (t), audioEngine (ae) 
        {
            startTimerHz (10); // Refresh list if plugins are added/removed
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::bgPanel);
            
            auto b = getLocalBounds().reduced (10);
            auto header = b.removeFromTop (30);
            
            g.setColour (Theme::textMuted);
            g.setFont (juce::Font (12.0f).withStyle (juce::Font::bold));
            g.drawText ("ASSIGNED PLUGINS", header, juce::Justification::centredLeft);
            
            // Add button
            addBtnBounds = header.removeFromRight (60).reduced (0, 4);
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (addBtnBounds.toFloat(), 4.0f);
            g.setColour (Theme::active);
            g.drawRoundedRectangle (addBtnBounds.toFloat(), 4.0f, 1.0f);
            g.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
            g.drawText ("+ ADD", addBtnBounds, juce::Justification::centred);

            int y = header.getBottom() + 10;
            rowBounds.clearQuick();
            removeBounds.clearQuick();
            plugins.clearQuick();

            for (auto* p : track->pluginList)
            {
                // Only show external (3rd party) plugins (VST3/AU).
                if (dynamic_cast<tracktion::ExternalPlugin*> (p) == nullptr)
                    continue;

                juce::Rectangle<int> r (10, y, getWidth() - 20, 40);
                Theme::drawRoundedPanel (g, r.toFloat(), Theme::surface);

                g.setColour (Theme::textMain);
                g.setFont (13.0f);
                g.drawText (p->getName(), r.reduced (12, 0).withTrimmedRight (60), juce::Justification::centredLeft);

                g.setColour (Theme::textMuted);
                g.setFont (10.0f);
                g.drawText ("dbl-click", r.withTrimmedRight (40), juce::Justification::centredRight);

                // Per-row remove button.
                juce::Rectangle<int> rm = r.removeFromRight (32).reduced (4);
                g.setColour (Theme::recordRed.withAlpha (0.18f));
                g.fillRoundedRectangle (rm.toFloat(), 3.0f);
                g.setColour (Theme::recordRed);
                g.drawRoundedRectangle (rm.toFloat(), 3.0f, 1.0f);
                g.setFont (juce::Font (12.0f).withStyle (juce::Font::bold));
                g.drawText ("X", rm, juce::Justification::centred);

                rowBounds.add (r);    // r has had the X area trimmed off
                removeBounds.add (rm);
                plugins.add (p);
                y += 46;
            }

            if (plugins.isEmpty())
            {
                g.setColour (Theme::textMuted);
                g.drawText ("No third-party plugins added.", getLocalBounds().withTrimmedTop(40), juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (addBtnBounds.contains (e.getPosition()))
            {
                auto screen = localAreaToGlobal (addBtnBounds);
                PluginPicker::show (audioEngine, screen, [this] (const juce::PluginDescription& d) {
                    if (auto p = audioEngine.addPluginToTrack (track, d))
                        p->showWindowExplicitly();
                    repaint();
                });
                return;
            }

            for (int i = 0; i < removeBounds.size(); ++i)
                if (removeBounds[i].contains (e.getPosition()))
                {
                    audioEngine.removePlugin (plugins[i].get());
                    repaint();
                    return;
                }
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < rowBounds.size(); ++i)
                if (rowBounds[i].contains (e.getPosition()))
                {
                    auto p = plugins[i];
                    p->setEnabled (true);
                    p->setProcessingEnabled (true);
                    p->showWindowExplicitly();
                    return;
                }
        }

        void timerCallback() override { repaint(); }

        tracktion::Track* track;
        AudioEngineManager& audioEngine;
        juce::Rectangle<int> addBtnBounds;
        juce::Array<juce::Rectangle<int>>   rowBounds;
        juce::Array<juce::Rectangle<int>>   removeBounds;
        juce::Array<tracktion::Plugin::Ptr> plugins;
    };

    tracktion::Track* track;
    AudioEngineManager& audioEngine;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManagerWindow)
};

//==============================================================================
class DAWPanel : public juce::Component
{
public:
    DAWPanel(juce::String panelName) : name(panelName) {}
    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bgPanel);
        g.setColour(Theme::border);
        g.drawRect(getLocalBounds(), 1);

        auto header = getLocalBounds().removeFromTop(28);
        g.setColour(Theme::surface);
        g.fillRect(header);
        g.setColour(Theme::border);
        g.drawLine(0.0f, 28.0f, (float)getWidth(), 28.0f);

        g.setColour(Theme::textMuted);
        g.setFont(juce::Font(10.0f).withStyle(juce::Font::bold));
        g.drawText(name.toUpperCase(), header.reduced(12, 0), juce::Justification::centredLeft, false);
    }
protected:
    juce::String name;
};

//==============================================================================
class DAWMenuBar : public juce::Component
{
public:
    DAWMenuBar()
    {
        if (auto svgXml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::aerion_logo_svg, BinaryData::aerion_logo_svgSize)))
            logoDrawable = juce::Drawable::createFromSVG (*svgXml);
    }

    std::function<void()> onNew, onOpen, onSave, onSaveAs, onImport, onSettings;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bgBase);

        auto b = getLocalBounds();
        
        // Draw Logo
        if (logoDrawable != nullptr)
        {
            auto logoArea = b.removeFromLeft (40).reduced (8);
            logoDrawable->drawWithin (g, logoArea.toFloat(), 
                                    juce::RectanglePlacement (juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid), 1.0f);
        }

        g.setColour(Theme::textMuted);
        g.setFont(12.0f);

        juce::StringArray items = { "File", "Edit", "Song", "Track", "Event", "Audio", "Transport", "View", "Help" };
        int x = 50;
        for (auto& s : items) {
            g.drawText(s, x, 0, 60, getHeight(), juce::Justification::centredLeft);
            x += 60;
        }

        g.drawText("My Song - Aerion DAW", 0, 0, getWidth(), getHeight(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.x >= 50 && e.x < 110) // "File" area adjusted for logo
        {
            juce::PopupMenu m;
            m.addItem (1, "New Project");
            m.addItem (2, "Open Project...");
            m.addItem (3, "Save Project");
            m.addItem (6, "Save Project As...");
            m.addSeparator();
            m.addItem (4, "Import Audio File...");
            m.addSeparator();
            m.addItem (5, "Audio Settings...");
            
            m.showMenuAsync (juce::PopupMenu::Options(), [this] (int result) {
                if (result == 1 && onNew) onNew();
                if (result == 2 && onOpen) onOpen();
                if (result == 3 && onSave) onSave();
                if (result == 6 && onSaveAs) onSaveAs();
                if (result == 4 && onImport) onImport();
                if (result == 5 && onSettings) onSettings();
            });
        }
    }

private:
    std::unique_ptr<juce::Drawable> logoDrawable;
};

//==============================================================================
// Top toolbar — tools on the left, view modes + snap on the right.
class DAWToolbar : public juce::Component
{
public:
    std::function<void()> onToggleSnap;
    std::function<void()> onToggleInspector;
    std::function<void()> onToggleBrowser;
    std::function<void(EditTool)> onToolChanged;
    std::function<void()> onToggleMetronome;
    std::function<void()> onShowMetronomeSettings;

    bool snapEnabled = true;
    bool inspectorVisible = true;
    bool browserVisible = true;
    bool metronomeEnabled = false;
    EditTool activeTool = EditTool::select;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bgPanel);
        g.setColour(Theme::border);
        g.drawLine(0.0f, (float)(getHeight()-1), (float)getWidth(), (float)(getHeight()-1));

        // Info / Info tool (leftmost)
        int x = 12;
        inspectorBtn = juce::Rectangle<int>(x, 6, 80, 28);
        g.setColour(inspectorVisible ? Theme::active.withAlpha(0.2f) : Theme::surface);
        g.fillRoundedRectangle(inspectorBtn.toFloat(), 4.0f);
        g.setColour(inspectorVisible ? Theme::active : Theme::border);
        g.drawRoundedRectangle(inspectorBtn.toFloat(), 4.0f, 1.0f);
        g.setColour(inspectorVisible ? Theme::active : Theme::textMuted);
        g.setFont(juce::Font(10.0f).withStyle(juce::Font::bold));
        g.drawText("Inspector", inspectorBtn, juce::Justification::centred);
        x += 92;

        // Tool group
        struct ToolBtn { juce::String label; EditTool tool; juce::Rectangle<int> bounds; };
        ToolBtn tools[] = { 
            { "Sel", EditTool::select, {x, 6, 32, 28} },
            { "Cut", EditTool::razor,  {x + 36, 6, 32, 28} }
        };

        g.setFont(10.0f);
        for (auto& t : tools) {
            bool isActive = (activeTool == t.tool);
            g.setColour(isActive ? Theme::active.withAlpha(0.2f) : Theme::surface);
            g.fillRoundedRectangle(t.bounds.toFloat(), 4.0f);
            g.setColour(isActive ? Theme::active : Theme::border);
            g.drawRoundedRectangle(t.bounds.toFloat(), 4.0f, 1.0f);
            g.setColour(isActive ? Theme::active : Theme::textMuted);
            g.drawText(t.label, t.bounds, juce::Justification::centred);
            
            if (t.tool == EditTool::select) selectBounds = t.bounds;
            if (t.tool == EditTool::razor)  razorBounds = t.bounds;
        }

        // Metronome Button
        x += 72;
        clickBtn = juce::Rectangle<int>(x, 6, 44, 28);
        g.setColour(metronomeEnabled ? Theme::active.withAlpha(0.2f) : Theme::surface);
        g.fillRoundedRectangle(clickBtn.toFloat(), 4.0f);
        g.setColour(metronomeEnabled ? Theme::active : Theme::border);
        g.drawRoundedRectangle(clickBtn.toFloat(), 4.0f, 1.0f);
        g.setColour(metronomeEnabled ? Theme::active : Theme::textMuted);
        g.setFont(juce::Font(10.0f).withStyle(juce::Font::bold));
        g.drawText("CLICK", clickBtn, juce::Justification::centred);
        x += 52;

        // Other mock tools
        const char* extra[] = { "Era", "Mut" };
        for (auto* label : extra) {
            g.setColour(Theme::surface.withAlpha(0.4f));
            g.fillRoundedRectangle((float)x, 6.0f, 32.0f, 28.0f, 4.0f);
            g.setColour(Theme::textMuted.withAlpha(0.3f));
            g.drawText(label, x, 6, 32, 28, juce::Justification::centred);
            x += 36;
        }

        // Right side: Quantize / Timebase / Snap
        int rx = getWidth() - 400;

        browserBtn = juce::Rectangle<int>(rx, 6, 72, 28);
        g.setColour(browserVisible ? Theme::active.withAlpha(0.2f) : Theme::surface);
        g.fillRoundedRectangle(browserBtn.toFloat(), 4.0f);
        g.setColour(browserVisible ? Theme::active : Theme::border);
        g.drawRoundedRectangle(browserBtn.toFloat(), 4.0f, 1.0f);
        g.setColour(browserVisible ? Theme::active : Theme::textMuted);
        g.setFont(juce::Font(10.0f).withStyle(juce::Font::bold));
        g.drawText("Browser", browserBtn, juce::Justification::centred);
        rx += 84;

        g.setColour(Theme::surface.withAlpha(0.4f));
        g.fillRoundedRectangle((float)rx, 6.0f, 120.0f, 28.0f, 4.0f);
        g.setColour(Theme::textMuted.withAlpha(0.3f));
        g.setFont(10.0f);
        g.drawText("QUANTIZE  1/16", rx + 8, 6, 100, 28, juce::Justification::centredLeft);

        rx += 130;
        g.setColour(Theme::surface.withAlpha(0.4f));
        g.fillRoundedRectangle((float)rx, 6.0f, 120.0f, 28.0f, 4.0f);
        g.setColour(Theme::textMuted.withAlpha(0.3f));
        g.drawText("TIMEBASE  Bars", rx + 8, 6, 100, 28, juce::Justification::centredLeft);

        int snapWidth = 60;
        int rightMargin = 12;
        int snapX = getWidth() - snapWidth - rightMargin;
        snapBounds = juce::Rectangle<int>(snapX, 6, snapWidth, 28);
        g.setColour((snapEnabled ? Theme::active : Theme::surface).withAlpha(snapEnabled ? 0.2f : 1.0f));
        g.fillRoundedRectangle(snapBounds.toFloat(), 4.0f);
        g.setColour(snapEnabled ? Theme::active : Theme::border);
        g.drawRoundedRectangle(snapBounds.toFloat(), 4.0f, 1.0f);
        g.setColour(snapEnabled ? Theme::active : Theme::textMain);
        g.drawText("Snap", snapBounds, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (inspectorBtn.contains(e.getPosition())) {
            inspectorVisible = !inspectorVisible;
            repaint();
            if (onToggleInspector) onToggleInspector();
            return;
        }
        if (browserBtn.contains(e.getPosition())) {
            browserVisible = !browserVisible;
            repaint();
            if (onToggleBrowser) onToggleBrowser();
            return;
        }
        if (selectBounds.contains(e.getPosition())) {
            activeTool = EditTool::select;
            repaint();
            if (onToolChanged) onToolChanged(activeTool);
            return;
        }
        if (razorBounds.contains(e.getPosition())) {
            activeTool = EditTool::razor;
            repaint();
            if (onToolChanged) onToolChanged(activeTool);
            return;
        }
        if (clickBtn.contains(e.getPosition())) {
            if (e.mods.isRightButtonDown())
            {
                if (onShowMetronomeSettings) onShowMetronomeSettings();
            }
            else
            {
                metronomeEnabled = !metronomeEnabled;
                repaint();
                if (onToggleMetronome) onToggleMetronome();
            }
            return;
        }
        if (snapBounds.contains(e.getPosition())) {
            snapEnabled = ! snapEnabled;
            repaint();
            if (onToggleSnap) onToggleSnap();
        }
    }

    juce::Rectangle<int> getClickBtnBounds() const { return clickBtn; }

private:
    juce::Rectangle<int> snapBounds, selectBounds, razorBounds, inspectorBtn, browserBtn, clickBtn;
};

//==============================================================================
class Inspector : public DAWPanel,
                  public juce::ValueTree::Listener,
                  private juce::Timer
{
public:
    Inspector (AudioEngineManager& ae, ProjectData& pd)
        : DAWPanel ("Inspector"), audioEngine (ae), projectData (pd)
    {
        projectData.getProjectTree().addListener (this);
        startTimerHz (30); // drives meter animation

        if (auto svgXml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::aerion_fader_svg, BinaryData::aerion_fader_svgSize)))
            faderKnobDrawable = juce::Drawable::createFromSVG (*svgXml);
    }

    ~Inspector() override { projectData.getProjectTree().removeListener (this); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { repaint(); }

    void timerCallback() override { if (selectedTrack != nullptr) repaint (faderArea); }

    juce::String trackName  { "(no selection)" };
    int          trackIndex { -1 };
    bool         armed      { false };
    bool         muted      { false };
    bool         solo       { false };
    tracktion::Track* selectedTrack = nullptr;

    std::unique_ptr<juce::Drawable> faderKnobDrawable;

    void paint (juce::Graphics& g) override
    {
        DAWPanel::paint (g);
        auto b = getLocalBounds().withTrimmedTop (28).reduced (12);

        // Fader & Meter area on the right.
        if (selectedTrack != nullptr)
        {
            auto faderMeterArea = b.removeFromRight (45);
            faderArea = faderMeterArea.withTrimmedBottom (12);
            ::paintFader (g, faderArea, audioEngine, selectedTrack, Theme::colourForTrack (trackIndex), false, faderKnobDrawable.get());
            b.removeFromRight (10); // Gap
        }
        else
        {
            faderArea = juce::Rectangle<int>();
        }

        insertRows.clearQuick();
        insertPlugins.clearQuick();
        sendRows.clearQuick();

        // Track header
        auto headerB = b.removeFromTop (70);
        g.setColour (trackIndex >= 0 ? Theme::colourForTrack (trackIndex) : Theme::textMuted);
        g.fillEllipse ((float) headerB.getX(), (float) headerB.getY() + 4.0f, 12.0f, 12.0f);
        g.setColour (Theme::textMain);
        g.setFont (juce::Font (15.0f).withStyle (juce::Font::bold));
        g.drawText (trackName, headerB.withTrimmedLeft (20).removeFromTop (22), juce::Justification::topLeft);

        g.setColour (Theme::textMuted);
        g.setFont (11.0f);
        g.drawText ("In",  headerB.getX(),       headerB.getY() + 32, 40, 18, juce::Justification::left);
        g.drawText ("Out", headerB.getX(),       headerB.getY() + 50, 40, 18, juce::Justification::left);
        g.setColour (Theme::textMain);
        g.drawText ("Input L+R", headerB.getRight() - 100, headerB.getY() + 32, 100, 18, juce::Justification::right);
        g.drawText ("Main",      headerB.getRight() - 100, headerB.getY() + 50, 100, 18, juce::Justification::right);

        // State pills
        b.removeFromTop (8);
        auto pills = b.removeFromTop (24);
        drawPill (g, pills.removeFromLeft (48), "ARM",  armed, Theme::recordRed);
        pills.removeFromLeft (6);
        drawPill (g, pills.removeFromLeft (48), "MUTE", muted, Theme::meterYellow);
        pills.removeFromLeft (6);
        drawPill (g, pills.removeFromLeft (48), "SOLO", solo,  Theme::accent);

        b.removeFromTop (16);

        // Collect real plugins from the selected track.
        juce::Array<tracktion::ExternalPlugin*> externals;
        juce::Array<tracktion::AuxSendPlugin*>  sends;
        if (selectedTrack != nullptr)
        {
            for (auto* p : selectedTrack->pluginList)
            {
                if (auto* e = dynamic_cast<tracktion::ExternalPlugin*> (p))      externals.add (e);
                else if (auto* a = dynamic_cast<tracktion::AuxSendPlugin*> (p))  sends.add (a);
            }
        }

        // Inserts
        const int insertsH = juce::jmax (60, 40 + externals.size() * 36);
        drawInsertSection (g, b.removeFromTop (insertsH), externals);
        b.removeFromTop (10);

        // Sends
        const int sendsH = juce::jmax (60, 40 + sends.size() * 36);
        drawSendSection (g, b.removeFromTop (sendsH), sends, 0.4f);
        b.removeFromTop (20);

        // AI DSP Workflow at bottom (still scaffolded — buttons are visual only).
        g.setColour (Theme::recordRed.withAlpha (0.3f));
        g.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
        g.drawText ("AI DSP WORKFLOW", b.getX(), b.getY(), b.getWidth(), 20, juce::Justification::left);
        b.removeFromTop (22);

        Theme::drawRoundedPanel (g, b.removeFromTop (36).toFloat(), Theme::surface.withAlpha (0.3f));
        g.setColour (Theme::textMain.withAlpha (0.3f));
        g.setFont (11.0f);
        g.drawText ("Separate Stems", b.getX(), b.getY() - 36, b.getWidth(), 36, juce::Justification::centred);
        b.removeFromTop (8);
        Theme::drawRoundedPanel (g, b.removeFromTop (36).toFloat(), Theme::surface.withAlpha (0.3f));
        g.setColour (Theme::textMain.withAlpha (0.3f));
        g.drawText ("Audio to MIDI", b.getX(), b.getY() - 36, b.getWidth(), 36, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Fader interaction
        if (faderArea.contains (e.getPosition()))
        {
            if (auto* audio = dynamic_cast<tracktion::AudioTrack*> (selectedTrack))
            {
                ::setFaderFromY (audioEngine, audio, faderArea, e.y);
                return;
            }
        }

        // "+" buttons add a plugin / send.
        if (insertAddBtn.contains (e.getPosition()))
        {
            if (selectedTrack == nullptr) return;
            auto track = selectedTrack;
            auto sp = e.getScreenPosition();
            juce::Rectangle<int> anchor (sp.x, sp.y, 1, 1);
            PluginPicker::show (audioEngine, anchor,
                [this, track] (const juce::PluginDescription& d)
                {
                    if (auto p = audioEngine.addPluginToTrack (track, d))
                        p->showWindowExplicitly();
                    repaint();
                });
            return;
        }

        // Click an insert row -> open its editor; right-click -> remove.
        for (int i = 0; i < insertRows.size(); ++i)
        {
            if (insertRows[i].contains (e.getPosition()))
            {
                if (auto* plug = insertPlugins[i])
                {
                    if (e.mods.isPopupMenu())
                    {
                        audioEngine.removePlugin (plug);
                        repaint();
                    }
                    else
                    {
                        plug->showWindowExplicitly();
                    }
                }
                return;
            }
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (faderArea.contains (e.getPosition()))
        {
            audioEngine.setTrackVolumeDb (selectedTrack, 0.0f);
            repaint (faderArea);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (faderArea.contains (e.getPosition()) || (e.getMouseDownX() >= faderArea.getX() && e.getMouseDownX() < faderArea.getRight()))
        {
            ::setFaderFromY (audioEngine, selectedTrack, faderArea, e.y);
            return;
        }
    }

    static void drawPill (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label,
                          bool on, juce::Colour activeColour)
    {
        g.setColour (on ? activeColour.withAlpha (0.8f) : Theme::surface);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (on ? activeColour : Theme::border);
        g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);
        g.setColour (on ? juce::Colours::black : Theme::textMuted);
        g.setFont (juce::Font (9.0f).withStyle (juce::Font::bold));
        g.drawText (label, r, juce::Justification::centred);
    }

private:
    AudioEngineManager& audioEngine;
    ProjectData& projectData;
    juce::Array<juce::Rectangle<int>> insertRows;
    juce::Array<tracktion::ExternalPlugin*> insertPlugins;
    juce::Array<juce::Rectangle<int>> sendRows;
    juce::Rectangle<int> insertAddBtn;
    juce::Rectangle<int> faderArea;

    void drawSectionHeader (juce::Graphics& g, juce::Rectangle<int> headerArea,
                            const juce::String& title, juce::Rectangle<int>& addBtnOut,
                            float alpha = 1.0f)
    {
        g.setColour (Theme::textMuted.withMultipliedAlpha (alpha));
        g.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
        g.drawText (title, headerArea.getX(), headerArea.getY(), headerArea.getWidth() - 20, 18, juce::Justification::left);

        addBtnOut = juce::Rectangle<int> (headerArea.getRight() - 18, headerArea.getY(), 16, 18);
        g.setColour (Theme::active.withMultipliedAlpha (0.18f * alpha));
        g.fillRoundedRectangle (addBtnOut.toFloat(), 3.0f);
        g.setColour (Theme::active.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (addBtnOut.toFloat(), 3.0f, 1.0f);
        g.setFont (juce::Font (12.0f).withStyle (juce::Font::bold));
        g.setColour (Theme::textMain.withMultipliedAlpha (alpha));
        g.drawText ("+", addBtnOut, juce::Justification::centred);
    }

    void drawInsertSection (juce::Graphics& g, juce::Rectangle<int> b,
                            const juce::Array<tracktion::ExternalPlugin*>& plugins)
    {
        drawSectionHeader (g, b.withHeight (18), "INSERTS", insertAddBtn);

        int y = b.getY() + 22;
        if (plugins.isEmpty())
        {
            g.setColour (Theme::textMuted);
            g.setFont (10.5f);
            g.drawText (selectedTrack == nullptr ? "Select a track."
                                                 : "No plugins. Click + to add.",
                        b.getX(), y, b.getWidth(), 24, juce::Justification::centredLeft);
            return;
        }

        for (auto* plug : plugins)
        {
            juce::Rectangle<int> row ((int) b.getX(), y, b.getWidth(), 30);
            Theme::drawRoundedPanel (g, row.toFloat(), Theme::bgBase);
            g.setColour (Theme::active);
            g.fillEllipse ((float) row.getX() + 8.0f, (float) y + 11.0f, 6.0f, 6.0f);
            g.setColour (Theme::textMain);
            g.setFont (11.0f);
            g.drawText (plug->getName(), row.getX() + 20, y, row.getWidth() - 30, 30, juce::Justification::centredLeft);

            insertRows.add (row);
            insertPlugins.add (plug);
            y += 36;
        }
    }

    void drawSendSection (juce::Graphics& g, juce::Rectangle<int> b,
                          const juce::Array<tracktion::AuxSendPlugin*>& sends,
                          float alpha = 1.0f)
    {
        juce::Rectangle<int> dummy;
        drawSectionHeader (g, b.withHeight (18), "SENDS", dummy, alpha);

        int y = b.getY() + 22;
        if (sends.isEmpty())
        {
            g.setColour (Theme::textMuted.withMultipliedAlpha (alpha));
            g.setFont (10.5f);
            g.drawText ("No sends.",
                        b.getX(), y, b.getWidth(), 24, juce::Justification::centredLeft);
            return;
        }

        for (auto* s : sends)
        {
            juce::Rectangle<int> row ((int) b.getX(), y, b.getWidth(), 30);
            Theme::drawRoundedPanel (g, row.toFloat(), Theme::bgBase, alpha);
            g.setColour (Theme::active.withMultipliedAlpha (alpha));
            g.fillEllipse ((float) row.getX() + 8.0f, (float) y + 11.0f, 6.0f, 6.0f);
            g.setColour (Theme::textMain.withMultipliedAlpha (alpha));
            g.setFont (11.0f);
            g.drawText (s->getBusName(), row.getX() + 20, y, row.getWidth() - 70, 30, juce::Justification::centredLeft);

            float gainDb = s->getGainDb();
            g.setColour (Theme::textMuted.withMultipliedAlpha (alpha));
            g.setFont (10.5f);
            g.drawText (juce::String::formatted ("%.1f dB", gainDb), row.getRight() - 60, y, 50, 30, juce::Justification::centredRight);

            sendRows.add (row);
            y += 36;
        }
    }
};

//==============================================================================
// Right-hand placeholder browser.
class Browser : public juce::Component,
                public juce::ChangeListener
{
public:
    Browser (AudioEngineManager& ae) : audioEngine (ae),
                                       thumb (512, formatManager, thumbCache)
    {
        formatManager.registerBasicFormats();
        thumb.addChangeListener (this);

        currentDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Aerion Projects");
        if (! currentDir.isDirectory())
            currentDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! currentDir.isDirectory())
            currentDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    ~Browser() override { thumb.removeChangeListener (this); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    enum class Tab { plugins, files, cloud };

    std::function<void (const juce::PluginDescription&)> onPluginPicked;
    std::function<void (const juce::File&)>              onFilePicked;
    std::function<void (const juce::File&)>              onFileDoubleClicked;
    std::function<void()>                                onRescanRequested;

    void setDriveClient (GoogleDriveClient* client)
    {
        driveClient = client;
        if (driveClient != nullptr)
        {
            driveClient->onLoginStateChanged = [this] (bool) { repaint(); };
            driveClient->onFilesListed = [this] (juce::Array<GoogleDriveClient::DriveFile> files)
            {
                driveFiles    = std::move (files);
                driveLoading  = false;
                repaint();
            };
        }
    }

    void setDriveFiles (juce::Array<GoogleDriveClient::DriveFile> files)
    {
        driveFiles = std::move (files);
        driveLoading = false;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bgPanel);
        g.setColour(Theme::border);
        g.drawRect(getLocalBounds(), 1);

        auto header = getLocalBounds().removeFromTop(32);
        g.setColour(Theme::surface);
        g.fillRect(header);
        g.setColour(Theme::border);
        g.drawLine(0.0f, 32.0f, (float)getWidth(), 32.0f);

        // Rescan button (Plugins tab only).
        if (tab == Tab::plugins)
        {
            rescanBtn = header.removeFromRight (60).reduced (4, 6);
            bool scanning = audioEngine.isScanningPlugins();
            g.setColour (scanning ? Theme::active.withAlpha (0.2f) : Theme::surface);
            g.fillRoundedRectangle (rescanBtn.toFloat(), 3.0f);
            g.setColour (scanning ? Theme::active : Theme::border);
            g.drawRoundedRectangle (rescanBtn.toFloat(), 3.0f, 1.0f);
            g.setColour (scanning ? Theme::active : Theme::textMuted);
            g.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
            g.drawText (scanning ? "SCANNING" : "RESCAN", rescanBtn, juce::Justification::centred);
        }
        else
        {
            rescanBtn = {};
        }

        int third = header.getWidth() / 3;
        pluginsTabBounds = header.removeFromLeft (third);
        filesTabBounds   = header.removeFromLeft (third);
        cloudTabBounds   = header;

        g.setFont (juce::Font (11.0f).withStyle (juce::Font::bold));
        for (auto [b, label, t] : { std::tuple { pluginsTabBounds, "Plugins", Tab::plugins },
                                    std::tuple { filesTabBounds,   "Files",   Tab::files   },
                                    std::tuple { cloudTabBounds,   "Cloud",   Tab::cloud   } })
        {
            bool active = (tab == t);
            g.setColour (active ? Theme::active : Theme::textMuted);
            g.drawText (label, b, juce::Justification::centred);
            if (active)
                g.fillRect ((float)b.getX(), (float)(b.getBottom() - 2), (float)b.getWidth(), 2.0f);
        }

        rowBounds.clearQuick();
        rowDescs.clearQuick();
        rowFiles.clearQuick();

        if (tab == Tab::plugins) paintPluginList (g);
        else if (tab == Tab::files) paintFileList (g);
        else paintCloudTab (g);

        // Waveform preview strip at the bottom (files tab only)
        if (tab == Tab::files)
        {
            auto pa = getLocalBounds().removeFromBottom (kPreviewH);
            g.setColour (Theme::bgBase);
            g.fillRect (pa);
            g.setColour (Theme::border);
            g.drawLine (0.0f, (float) pa.getY(), (float) getWidth(), (float) pa.getY(), 1.0f);

            if (selectedFile.existsAsFile() && thumb.isFullyLoaded() && thumb.getTotalLength() > 0.0)
            {
                auto wa = pa.reduced (8, 10);
                g.setColour (Theme::active.withAlpha (0.55f));
                thumb.drawChannels (g, wa.withTrimmedBottom (14), 0.0, thumb.getTotalLength(), 1.0f);
                g.setColour (Theme::textMuted);
                g.setFont (juce::Font (9.0f));
                g.drawText (selectedFile.getFileName(), wa.withTop (wa.getBottom() - 13),
                            juce::Justification::centred);
            }
            else if (selectedFile.existsAsFile())
            {
                g.setColour (Theme::textMuted);
                g.setFont (11.0f);
                g.drawText ("Loading preview...", pa, juce::Justification::centred);
            }
            else
            {
                g.setColour (Theme::textMuted.withAlpha (0.5f));
                g.setFont (11.0f);
                g.drawText ("Click an audio file to preview", pa, juce::Justification::centred);
            }
        }
    }

    void paintPluginList (juce::Graphics& g)
    {
        auto& fm    = audioEngine.getEngine().getPluginManager().pluginFormatManager;
        auto& known = audioEngine.getEngine().getPluginManager().knownPluginList;
        auto types  = known.getTypes();

        int y = 40;
        g.setFont (juce::Font (11.0f));

        if (types.isEmpty())
        {
            g.setColour (Theme::textMuted);
            g.drawText (audioEngine.isScanningPlugins() ? "Scanning plugins..." : "No plugins found.",
                        12, y, getWidth() - 16, 22, juce::Justification::centredLeft);
            y += 22;
            g.setFont (juce::Font (9.0f));
            g.drawText (juce::String::formatted ("Formats registered: %d", fm.getNumFormats()),
                        12, y, getWidth() - 16, 18, juce::Justification::centredLeft);
            y += 18;
            for (int i = 0; i < fm.getNumFormats(); ++i)
                if (auto* fmt = fm.getFormat (i))
                {
                    g.drawText ("  " + fmt->getName(), 12, y, getWidth() - 16, 16, juce::Justification::centredLeft);
                    y += 16;
                }
            return;
        }

        juce::StringArray seenManufacturers;
        for (auto& d : types)
        {
            if (seenManufacturers.indexOf (d.manufacturerName) < 0)
            {
                seenManufacturers.add (d.manufacturerName);
                g.setColour (Theme::textMuted);
                g.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
                g.drawText (d.manufacturerName.toUpperCase(), 12, y, getWidth() - 16, 18,
                            juce::Justification::centredLeft);
                y += 18;
            }

            juce::Rectangle<int> r (8, y, getWidth() - 16, 22);
            rowBounds.add (r);
            rowDescs.add (d);

            g.setColour (Theme::textMain);
            g.setFont (juce::Font (11.0f));
            g.drawText (d.name, r.withTrimmedLeft (12), juce::Justification::centredLeft);
            g.setColour (Theme::textMuted);
            g.setFont (juce::Font (9.0f));
            g.drawText (d.pluginFormatName, r.withTrimmedRight (8), juce::Justification::centredRight);

            y += 22;
            if (y > getHeight()) break;
        }
    }

    void paintFileList (juce::Graphics& g)
    {
        // Path strip.
        juce::Rectangle<int> path (8, 38, getWidth() - 16, 20);
        g.setColour (Theme::textMuted);
        g.setFont (juce::Font (10.0f));
        g.drawText (currentDir.getFullPathName(), path, juce::Justification::centredLeft);

        int y = 64;
        g.setFont (juce::Font (11.0f));

        // Up directory entry.
        if (currentDir.getParentDirectory() != currentDir)
        {
            juce::Rectangle<int> r (8, y, getWidth() - 16, 22);
            rowBounds.add (r);
            rowFiles.add (currentDir.getParentDirectory());
            rowDescs.add ({});

            g.setColour (Theme::active);
            g.drawText ("..", r.withTrimmedLeft (12), juce::Justification::centredLeft);
            y += 22;
        }

        juce::Array<juce::File> children;
        currentDir.findChildFiles (children, juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);

        // Directories first, then audio files, alphabetised.
        std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b) {
            if (a.isDirectory() != b.isDirectory()) return a.isDirectory();
            return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
        });

        for (auto& f : children)
        {
            bool isAudio = f.hasFileExtension ("wav;mp3;aif;aiff;flac;ogg");
            if (! f.isDirectory() && ! isAudio) continue;

            juce::Rectangle<int> r (8, y, getWidth() - 16, 22);
            rowBounds.add (r);
            rowFiles.add (f);
            rowDescs.add ({});

            g.setColour (f.isDirectory() ? Theme::active : Theme::textMain);
            g.drawText ((f.isDirectory() ? juce::String ("[D] ") : juce::String ("    ")) + f.getFileName(),
                        r.withTrimmedLeft (12), juce::Justification::centredLeft);

            y += 22;
            if (y > getHeight() - kPreviewH) break;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 8) return;

        for (int i = 0; i < rowBounds.size(); ++i)
        {
            if (! rowBounds[i].contains (e.getMouseDownPosition()))
                continue;

            if (tab == Tab::plugins && i < rowDescs.size())
            {
                // Internal JUCE drag so Timeline/Mixer can receive it with position data
                auto* ddc = juce::DragAndDropContainer::findParentDragContainerFor (this);
                if (ddc != nullptr)
                {
                    juce::String payload = "PLUGIN:" + rowDescs[i].createIdentifierString();
                    ddc->startDragging (payload, this, juce::ScaledImage{}, true);
                }
                return;
            }

            if (tab == Tab::files && i < rowFiles.size())
            {
                auto& f = rowFiles.getReference (i);
                if (f.existsAsFile() && ! f.isDirectory())
                {
                    // Start internal drag so Timeline can show ghost preview
                    auto* ddc = juce::DragAndDropContainer::findParentDragContainerFor (this);
                    if (ddc != nullptr)
                        ddc->startDragging ("AUDIOFILE:" + f.getFullPathName(), this);

                    // Also start external drag for dropping into other apps/OS
                    juce::DragAndDropContainer::performExternalDragDropOfFiles (
                        { f.getFullPathName() }, false, this);
                }
                return;
            }
            break;
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < rowBounds.size(); ++i)
        {
            if (rowBounds[i].contains (e.getPosition()))
            {
                if (tab == Tab::files && i < rowFiles.size())
                {
                    auto& f = rowFiles.getReference (i);
                    if (f.existsAsFile() && ! f.isDirectory())
                        if (onFileDoubleClicked) onFileDoubleClicked (f);
                }
                return;
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (pluginsTabBounds.contains (e.getPosition())) { tab = Tab::plugins; repaint(); return; }
        if (filesTabBounds.contains   (e.getPosition())) { tab = Tab::files;   repaint(); return; }
        if (cloudTabBounds.contains   (e.getPosition())) { tab = Tab::cloud;   repaint(); return; }

        if (tab == Tab::cloud)
        {
            return;
        }

        if (! rescanBtn.isEmpty() && rescanBtn.contains (e.getPosition()))
        {
            if (onRescanRequested) onRescanRequested();
            repaint();
            return;
        }

        for (int i = 0; i < rowBounds.size(); ++i)
            if (rowBounds[i].contains (e.getPosition()))
            {
                if (tab == Tab::plugins) {
                    if (onPluginPicked) onPluginPicked (rowDescs[i]);
                } else {
                    auto& f = rowFiles.getReference (i);
                    if (f.isDirectory()) {
                        currentDir = f;
                        selectedFile = juce::File();
                        repaint();
                    } else {
                        if (f != selectedFile) {
                            selectedFile = f;
                            thumb.setSource (new juce::FileInputSource (f));
                        }
                        if (onFilePicked) onFilePicked (f);
                        repaint();
                    }
                }
                return;
            }
    }

    void paintCloudTab (juce::Graphics& g)
    {
        int y = 60;
        int W = getWidth();

        g.setColour (Theme::textMuted);
        g.setFont (juce::Font (14.0f).withStyle (juce::Font::bold));
        g.drawText ("CLOUD SYNC", 0, y, W, 22, juce::Justification::centred);
        y += 30;
        
        g.setFont (12.0f);
        g.drawText ("Coming Soon", 0, y, W, 20, juce::Justification::centred);
        
        y += 40;
        g.setFont (10.0f);
        g.drawText ("Google Drive integration", 0, y, W, 18, juce::Justification::centred);
        y += 14;
        g.drawText ("is currently inactive for", 0, y, W, 18, juce::Justification::centred);
        y += 14;
        g.drawText ("further tuning.", 0, y, W, 18, juce::Justification::centred);
    }

private:
    static constexpr int kPreviewH = 80;

    AudioEngineManager& audioEngine;
    Tab                  tab { Tab::plugins };
    juce::File           currentDir;
    juce::File           selectedFile;

    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache thumbCache { 10 };
    juce::AudioThumbnail      thumb;

    GoogleDriveClient*                        driveClient         = nullptr;
    juce::Array<GoogleDriveClient::DriveFile> driveFiles;
    bool                                      driveLoading        = false;
    juce::Rectangle<int>                      loginBtnBounds;
    juce::Rectangle<int>                      driveRefreshBtnBounds;
    juce::Array<juce::Rectangle<int>>         driveRowBounds;

    juce::Rectangle<int> rescanBtn, pluginsTabBounds, filesTabBounds, cloudTabBounds;
    juce::Array<juce::Rectangle<int>>    rowBounds;
    juce::Array<juce::PluginDescription> rowDescs;
    juce::Array<juce::File>              rowFiles;
};

//==============================================================================
// Piano Roll editor — opens when the user double-clicks an existing MIDI clip.
class PianoRollEditor : public juce::Component,
                        public juce::Timer,
                        public juce::ScrollBar::Listener
{
public:
    PianoRollEditor (tracktion::MidiClip& clip, tracktion::Edit& edit)
        : midiClip (clip), edit (edit)
    {
        addAndMakeVisible (hScroll);
        addAndMakeVisible (vScroll);
        hScroll.setAutoHide (false);
        vScroll.setAutoHide (false);
        hScroll.addListener (this);
        vScroll.addListener (this);
        scrollY = (127 - 72) * kRowH;   // open near C4
        startTimerHz (30);
    }

    ~PianoRollEditor() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bgBase);
        auto ga = gridArea();
        drawGrid (g, ga);
        drawNotes (g, ga);
        drawPianoKeys (g);
        // Clip-length end marker
        g.setColour (Theme::active.withAlpha (0.4f));
        float endX = beatToX (clipLengthBeats());
        if (endX > (float) kKeyW && endX < (float) ga.getRight())
            g.fillRect (endX, (float) kHdrH, 2.0f, (float) ga.getHeight());
    }

    void resized() override
    {
        hScroll.setBounds (getLocalBounds().removeFromBottom (14).withTrimmedLeft (kKeyW));
        vScroll.setBounds (getLocalBounds().removeFromRight (14).withTrimmedTop (kHdrH));
        updateScrollRanges();
    }

    void scrollBarMoved (juce::ScrollBar* bar, double v) override
    {
        if (bar == &hScroll) viewBeat = v;
        else                 scrollY  = (int) v;
        repaint();
    }

    void timerCallback() override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto ga = gridArea();
        if (e.y < kHdrH || e.x < kKeyW) return;

        float beat = xToBeat (e.x);
        int   note = yToNote (e.y);

        if (e.mods.isRightButtonDown())
        {
            for (auto* n : midiClip.getSequence().getNotes())
                if (noteRect (n, ga).contains (e.position))
                    { midiClip.getSequence().removeNote (*n, &edit.getUndoManager()); repaint(); return; }
            return;
        }

        for (auto* n : midiClip.getSequence().getNotes())
        {
            if (noteRect (n, ga).contains (e.position))
            {
                dragging = n;
                dragBeat0 = beat;
                dragNote0 = note;
                origStart = n->getStartBeat().inBeats();
                origNote  = n->getNoteNumber();
                origLen   = n->getLengthBeats().inBeats();
                // Check if near the right edge for resize
                auto r = noteRect (n, ga);
                resizing = (e.x > r.getRight() - 6);
                return;
            }
        }

        // Add note
        double snapped = snapBeat (beat);
        midiClip.getSequence().addNote (note,
            tracktion::BeatPosition::fromBeats (snapped),
            tracktion::BeatDuration::fromBeats (noteLen),
            100, 0, &edit.getUndoManager());
        updateScrollRanges();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == nullptr) return;
        float beat = xToBeat (e.x);
        int   note = yToNote (e.y);

        if (resizing)
        {
            double newLen = juce::jmax (noteLen * 0.5, (double)(beat - origStart));
            dragging->setStartAndLength (
                tracktion::BeatPosition::fromBeats (origStart),
                tracktion::BeatDuration::fromBeats (snapBeat (newLen)),
                &edit.getUndoManager());
        }
        else
        {
            double newStart = juce::jmax (0.0, origStart + (beat - dragBeat0));
            int    newNote  = juce::jlimit (0, 127, origNote + (note - dragNote0));
            dragging->setStartAndLength (
                tracktion::BeatPosition::fromBeats (snapBeat (newStart)),
                tracktion::BeatDuration::fromBeats (origLen),
                &edit.getUndoManager());
            dragging->setNoteNumber (newNote, &edit.getUndoManager());
        }
        updateScrollRanges();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = nullptr; resizing = false; }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
            pxPerBeat = juce::jlimit (20.0, 400.0, pxPerBeat * (1.0 + w.deltaY * 0.15));
        else if (e.mods.isShiftDown())
            viewBeat = juce::jmax (0.0, viewBeat - w.deltaY * 2.0);
        else
            scrollY = juce::jlimit (0, juce::jmax (0, 128 * kRowH - gridArea().getHeight()),
                                    scrollY - (int)(w.deltaY * 40.0));
        updateScrollRanges();
        repaint();
    }

    // Note grid snap interval (in beats): 1=quarter, 0.5=8th, 0.25=16th
    double noteLen = 0.5;

private:
    static constexpr int kKeyW = 44;
    static constexpr int kRowH = 12;
    static constexpr int kHdrH = 24;

    tracktion::MidiClip& midiClip;
    tracktion::Edit&     edit;

    juce::ScrollBar hScroll { false }, vScroll { true };
    double viewBeat = 0.0;
    int    scrollY  = 0;
    double pxPerBeat = 80.0;

    tracktion::MidiNote* dragging  = nullptr;
    bool resizing = false;
    float  dragBeat0 = 0;
    int    dragNote0 = 0;
    double origStart = 0, origLen = 0;
    int    origNote  = 0;

    juce::Rectangle<int> gridArea() const
    {
        return { kKeyW, kHdrH, getWidth() - kKeyW - 14, getHeight() - kHdrH - 14 };
    }

    double visibleBeats() const { return gridArea().getWidth() / pxPerBeat; }

    double clipLengthBeats() const
    {
        auto& ts = edit.tempoSequence;
        auto  t  = midiClip.getPosition().getLength();
        return ts.toBeats (midiClip.getPosition().getStart() + t).inBeats()
             - ts.toBeats (midiClip.getPosition().getStart()).inBeats();
    }

    double contentBeats() const
    {
        double mx = clipLengthBeats();
        for (auto* n : midiClip.getSequence().getNotes())
            mx = juce::jmax (mx, n->getEndBeat().inBeats());
        return mx + 4.0;
    }

    float beatToX (double beat) const { return (float)(kKeyW + (beat - viewBeat) * pxPerBeat); }
    float xToBeat (int x)       const { return (float)((x - kKeyW) / pxPerBeat + viewBeat); }
    int   noteToY (int note)    const { return kHdrH + (127 - note) * kRowH - scrollY; }
    int   yToNote (int y)       const { return juce::jlimit (0, 127, 127 - (y - kHdrH + scrollY) / kRowH); }
    double snapBeat (double b)  const { return std::round (b / noteLen) * noteLen; }

    juce::Rectangle<float> noteRect (tracktion::MidiNote* n, juce::Rectangle<int>) const
    {
        float x = beatToX (n->getStartBeat().inBeats());
        float y = (float) noteToY (n->getNoteNumber());
        float w = juce::jmax (3.0f, (float)(n->getLengthBeats().inBeats() * pxPerBeat) - 1.0f);
        return { x, y + 1.0f, w, (float) kRowH - 2.0f };
    }

    void updateScrollRanges()
    {
        double total = contentBeats();
        hScroll.setRangeLimits (0.0, total);
        hScroll.setCurrentRange (viewBeat, visibleBeats(), juce::dontSendNotification);
        int totalH = 128 * kRowH;
        int visH   = gridArea().getHeight();
        vScroll.setRangeLimits (0, totalH);
        vScroll.setCurrentRange (scrollY, visH, juce::dontSendNotification);
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<int> ga)
    {
        // Row backgrounds
        for (int note = 0; note <= 127; ++note)
        {
            int y = noteToY (note);
            if (y + kRowH < ga.getY() || y > ga.getBottom()) continue;
            bool black = isBlackKey (note);
            g.setColour (black ? Theme::bgBase.darker (0.25f) : Theme::bgPanel.withAlpha (0.35f));
            g.fillRect (ga.getX(), y, ga.getWidth(), kRowH - 1);
            if (note % 12 == 0)
            {
                g.setColour (Theme::border.withAlpha (0.6f));
                g.drawLine ((float) ga.getX(), (float) y, (float) ga.getRight(), (float) y, 1.0f);
            }
        }

        // Ruler + beat lines
        g.setColour (Theme::bgPanel.darker (0.3f));
        g.fillRect (kKeyW, 0, getWidth() - kKeyW, kHdrH);

        double firstBeat = std::floor (viewBeat);
        double lastBeat  = viewBeat + visibleBeats() + 1.0;
        for (double b = firstBeat; b <= lastBeat; b += noteLen)
        {
            float x = beatToX (b);
            if (x < ga.getX() || x > ga.getRight()) continue;
            bool isBar = (std::fmod (b, 4.0) < 0.001);
            bool isBeat = (std::fmod (b, 1.0) < 0.001);
            g.setColour (isBar ? Theme::border.brighter (0.2f)
                               : isBeat ? Theme::border : Theme::border.withAlpha (0.2f));
            g.drawLine (x, (float) kHdrH, x, (float) ga.getBottom(), 1.0f);
            if (isBeat)
            {
                g.setColour (isBar ? Theme::textMain : Theme::textMuted);
                g.setFont (isBar ? juce::Font (10.0f).boldened() : juce::Font (9.0f));
                g.drawText (juce::String ((int) std::round (b) + 1), (int) x + 3, 5, 30, 14, juce::Justification::left);
            }
        }
        g.setColour (Theme::border);
        g.drawLine ((float) kKeyW, (float) kHdrH, (float) kKeyW, (float) ga.getBottom());
    }

    void drawNotes (juce::Graphics& g, juce::Rectangle<int> ga)
    {
        for (auto* n : midiClip.getSequence().getNotes())
        {
            auto r = noteRect (n, ga);
            if (r.getRight() < ga.getX() || r.getX() > ga.getRight()) continue;
            if (r.getBottom() < ga.getY() || r.getY() > ga.getBottom()) continue;
            auto col = (n == dragging) ? Theme::active.brighter (0.3f) : Theme::active;
            g.setColour (col);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (col.darker (0.4f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
            if (r.getWidth() > 18.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.setFont (juce::Font (8.5f));
                g.drawText (noteName (n->getNoteNumber()), r.getX() + 3, r.getY(), (int) r.getWidth(), kRowH, juce::Justification::centredLeft);
            }
        }
    }

    void drawPianoKeys (juce::Graphics& g)
    {
        g.setColour (Theme::surface);
        g.fillRect (0, kHdrH, kKeyW, getHeight() - kHdrH);

        for (int note = 0; note <= 127; ++note)
        {
            int y = noteToY (note);
            if (y + kRowH < 0 || y > getHeight()) continue;
            bool black = isBlackKey (note);
            if (!black)
            {
                g.setColour (juce::Colours::white.withAlpha (0.88f));
                g.fillRect (1, y + 1, kKeyW - 3, kRowH - 2);
                g.setColour (Theme::border.withAlpha (0.4f));
                g.drawRect (1, y + 1, kKeyW - 3, kRowH - 2);
                if (note % 12 == 0)
                {
                    g.setColour (Theme::bgBase.withAlpha (0.75f));
                    g.setFont (juce::Font (7.5f));
                    g.drawText ("C" + juce::String (note / 12 - 1), 3, y + 2, kKeyW - 8, kRowH - 4, juce::Justification::left);
                }
            }
            else
            {
                g.setColour (juce::Colour (0xff1a1a1a));
                g.fillRect (1, y + 1, kKeyW * 2 / 3, kRowH - 1);
            }
        }
        g.setColour (Theme::border);
        g.drawLine ((float) kKeyW, (float) kHdrH, (float) kKeyW, (float) getHeight(), 1.5f);
    }

    static bool isBlackKey (int note) noexcept
    {
        int n = note % 12;
        return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
    }

    static juce::String noteName (int note)
    {
        const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollEditor)
};

class PianoRollWindow : public juce::DocumentWindow
{
public:
    PianoRollWindow (tracktion::MidiClip& clip, tracktion::Edit& edit)
        : DocumentWindow (clip.getName() + "  —  Piano Roll",
                          Theme::bgBase, DocumentWindow::allButtons, true)
    {
        editor = std::make_unique<PianoRollEditor> (clip, edit);
        setUsingNativeTitleBar (false);
        setResizable (true, false);
        setContentNonOwned (editor.get(), true);
        centreWithSize (960, 560);
        setVisible (true);
    }

    void closeButtonPressed() override { delete this; }

private:
    std::unique_ptr<PianoRollEditor> editor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollWindow)
};

//==============================================================================
class Timeline : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 public juce::DragAndDropTarget,
                 public juce::ScrollBar::Listener,
                 public juce::ValueTree::Listener
{
public:
    static constexpr int kHeaderWidth = 250;
    static constexpr int kRulerH      = 32;
    static constexpr int kHeaderBarH  = 32;
    static constexpr int kTrackH      = 80;
    static constexpr int kFooterH     = 28;
    static constexpr int kVScrollW    = 12;

    std::function<void(int)> onTrackSelected;
    std::function<void(juce::Array<tracktion::Track*>)> onSelectionChanged;
    std::function<void()> onAddTrack;
    std::function<void()> onAddMidiTrack;
    std::function<void()> onAddFolder;
    std::function<void(const juce::File&)> onImportFile; // legacy single-file path (menu import)
    // Studio-One-style drop: files + target track (nullptr = create new) + time position
    std::function<void(const juce::Array<juce::File>&,
                       tracktion::AudioTrack*,
                       double)> onImportFiles;
    // Plugin dropped on a track header from the Browser Plugins tab
    std::function<void(tracktion::Track*, const juce::PluginDescription&)> onPluginDroppedOnTrack;

    Timeline(AudioEngineManager& ae, ProjectData& pd) : audioEngine(ae), projectData(pd)
    {
        projectData.getProjectTree().addListener (this);

        addAndMakeVisible (horizontalScrollBar);
        horizontalScrollBar.setRangeLimits (0.0, 3600.0); // 1 hour max
        horizontalScrollBar.addListener (this);

        addAndMakeVisible (verticalScrollBar);
        verticalScrollBar.addListener (this);

        addAndMakeVisible (zoomSlider);
        zoomSlider.setRange (1.0, 2000.0, 1.0);
        zoomSlider.setValue (pxPerSec);
        zoomSlider.onValueChange = [this] {
            pxPerSec = zoomSlider.getValue();
            updateScrollBar();
            repaint();
        };

        addChildComponent (trackNameEditor);
        trackNameEditor.setColour (juce::TextEditor::backgroundColourId, Theme::surface);
        trackNameEditor.setColour (juce::TextEditor::outlineColourId, Theme::active);
        trackNameEditor.setColour (juce::TextEditor::textColourId, Theme::textMain);
        trackNameEditor.onReturnKey = [this] { commitTrackRename(); };
        trackNameEditor.onFocusLost = [this] { commitTrackRename(); };
        trackNameEditor.onEscapeKey = [this] { trackNameEditor.setVisible (false); };

        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    juce::MouseCursor getMouseCursor() override
    {
        if (activeTool == EditTool::razor)
            return juce::MouseCursor::CrosshairCursor;
        return juce::MouseCursor::NormalCursor;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        lastMouseX = e.x;
        repaint();
    }

    void commitTrackRename()
    {
        if (trackBeingRenamed != nullptr)
        {
            trackBeingRenamed->setName (trackNameEditor.getText());
            if (onSelectionChanged) onSelectionChanged (getSelectedTracks());
            trackBeingRenamed = nullptr;
        }
        trackNameEditor.setVisible (false);
        repaint();
    }

    enum class DragMode { none, move, trimLeft, trimRight };
    DragMode dragMode = DragMode::none;

    // ── FileDragAndDropTarget ────────────────────────────────────────────────
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }

    void fileDragEnter (const juce::StringArray& files, int x, int y) override
    {
        fileDragFiles  = files;
        fileDragActive = true;
        updateFileDragState (x, y);
        repaint();
    }

    void fileDragMove (const juce::StringArray& files, int x, int y) override
    {
        fileDragFiles = files;
        updateFileDragState (x, y);
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        fileDragActive       = false;
        fileDragTargetRowIdx = -1;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        fileDragActive = false;
        updateFileDragState (x, y);

        juce::Array<juce::File> fileArray;
        for (auto& s : files)
            fileArray.add (juce::File (s));

        tracktion::AudioTrack* targetTrack = nullptr;
        if (fileDragTargetRowIdx >= 0)
        {
            auto rows = getVisibleRows();
            if (fileDragTargetRowIdx < rows.size())
                targetTrack = dynamic_cast<tracktion::AudioTrack*> (rows[fileDragTargetRowIdx].track);
        }

        if (onImportFiles)
            onImportFiles (fileArray, targetTrack, fileDragSnappedTime);
        else if (onImportFile)               // fallback for legacy callers
            for (auto& f : fileArray)
                onImportFile (f);

        fileDragTargetRowIdx = -1;
        repaint();
    }

    // ── DragAndDropTarget (plugin drag from Browser) ─────────────────────────
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        return d.description.toString().startsWith ("PLUGIN:");
    }

    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragActive = true;
        updatePluginDragTarget (d.localPosition.x, d.localPosition.y);
        repaint();
    }

    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        updatePluginDragTarget (d.localPosition.x, d.localPosition.y);
        repaint();
    }

    void itemDragExit (const juce::DragAndDropTarget::SourceDetails&) override
    {
        pluginDragActive    = false;
        pluginDragTargetRow = -1;
        repaint();
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragActive = false;
        updatePluginDragTarget (d.localPosition.x, d.localPosition.y);

        if (pluginDragTargetRow >= 0 && onPluginDroppedOnTrack)
        {
            auto rows = getVisibleRows();
            if (pluginDragTargetRow < rows.size())
            {
                auto* track = rows[pluginDragTargetRow].track;
                juce::String idStr = d.description.toString()
                                         .fromFirstOccurrenceOf ("PLUGIN:", false, false);
                auto& known = audioEngine.getEngine().getPluginManager().knownPluginList;
                for (auto& t : known.getTypes())
                {
                    if (t.createIdentifierString() == idStr)
                    {
                        onPluginDroppedOnTrack (track, t);
                        break;
                    }
                }
            }
        }

        pluginDragTargetRow = -1;
        repaint();
    }

    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override
    {
        if (bar == &horizontalScrollBar) { startTime = newRangeStart; repaint(); }
        else if (bar == &verticalScrollBar) { scrollY = (int) newRangeStart; repaint(); }
    }

    int contentHeight() const
    {
        int totalH = 0;
        auto top = audioEngine.getTopLevelTracks();
        
        std::function<void(tracktion::Track*)> addHeight = [&](tracktion::Track* t) {
            totalH += getTrackHeight (t);
            if (auto* f = dynamic_cast<tracktion::FolderTrack*>(t)) {
                for (auto* child : f->getAllAudioSubTracks(false))
                    addHeight (child);
            }
        };

        for (auto* t : top)
            addHeight (t);

        return totalH;
    }

    int laneTop()    const { return kRulerH; }
    int laneBottom() const { return getHeight() - kFooterH; }

    void updateScrollBar()
    {
        double totalLen = audioEngine.getEdit().getLength().inSeconds() + 60.0;
        double viewLen  = (getWidth() - kHeaderWidth - kVScrollW) / pxPerSec;
        horizontalScrollBar.setRangeLimits (0.0, totalLen);
        horizontalScrollBar.setCurrentRange (startTime, viewLen);

        int visibleH = juce::jmax (1, laneBottom() - laneTop());
        int totalH   = juce::jmax (visibleH, contentHeight());
        verticalScrollBar.setRangeLimits (0.0, (double) totalH);
        verticalScrollBar.setCurrentRange ((double) scrollY, (double) visibleH);

        // Clamp scrollY when content shrinks.
        scrollY = juce::jlimit (0, juce::jmax (0, totalH - visibleH), scrollY);
    }

    void resized() override
    {
        const int barY = getHeight() - kFooterH + 6;
        horizontalScrollBar.setBounds (kHeaderWidth, barY, getWidth() - kHeaderWidth - kVScrollW, 14);
        zoomSlider.setBounds (10, barY, kHeaderWidth - 20, 14);
        verticalScrollBar.setBounds (getWidth() - kVScrollW, laneTop(), kVScrollW, laneBottom() - laneTop());
        updateScrollBar();
    }

    float timeToX (double t) const { return (float)kHeaderWidth + (float)((t - startTime) * pxPerSec); }
    double xToTime (float x) const { return (double)(x - kHeaderWidth) / pxPerSec + startTime; }

    juce::Array<tracktion::Track*> getSelectedTracks() const
    {
        juce::Array<tracktion::Track*> result;
        auto top = audioEngine.getTopLevelTracks();
        for (auto& id : selectedIds)
            for (auto* t : top)
                if (t->itemID.toString() == id) { result.add(t); break; }
        return result;
    }

    int getSelectedIndex() const
    {
        if (selectedIds.isEmpty()) return -1;
        auto top = audioEngine.getTopLevelTracks();
        for (int i = 0; i < top.size(); ++i)
            if (top[i]->itemID.toString() == selectedIds[0]) return i;
        return -1;
    }

    void paint(juce::Graphics& g) override
    {
        currentTooltip.isValid = false;
        g.fillAll(Theme::bgBase);

        // Header column action bar (+ Track / + Folder)
        addTrackBtn  = juce::Rectangle<int>(8,  4, kHeaderWidth/2 - 12, kHeaderBarH - 8);
        addFolderBtn = juce::Rectangle<int>(kHeaderWidth/2 + 4, 4, kHeaderWidth/2 - 12, kHeaderBarH - 8);

        g.setColour(Theme::bgPanel);
        g.fillRect(0, 0, kHeaderWidth, kHeaderBarH);
        g.setColour(Theme::border);
        g.drawLine((float)kHeaderWidth, 0.0f, (float)kHeaderWidth, (float)kHeaderBarH);

        drawHeaderButton(g, addTrackBtn,  "+ Track",  Theme::accent);
        drawHeaderButton(g, addFolderBtn, "+ Folder", Theme::active);

        // Ruler (right of header bar)
        g.setColour(Theme::bgPanel);
        g.fillRect(kHeaderWidth, 0, getWidth() - kHeaderWidth, kRulerH);
        g.setColour(Theme::border);
        g.drawLine(0.0f, (float)kRulerH, (float)getWidth(), (float)kRulerH);
        g.drawLine((float)kHeaderWidth, 0.0f, (float)getWidth(), 0.0f);

        g.setColour(Theme::textMuted);
        g.setFont(10.0f);
        
        auto& ts = audioEngine.getEdit().tempoSequence;
        double startBeat = ts.toBeats (tracktion::TimePosition::fromSeconds (startTime)).inBeats();
        double endBeat   = ts.toBeats (tracktion::TimePosition::fromSeconds (xToTime ((float)getWidth()))).inBeats();

        // Determine beat step based on zoom
        double beatStep = 1.0;
        if (pxPerSec < 15.0)      beatStep = 16.0; // 4 bars
        else if (pxPerSec < 40.0) beatStep = 4.0;  // 1 bar
        else if (pxPerSec < 120.0) beatStep = 1.0; // 1 beat
        else                       beatStep = 0.25; // 1/4 beat

        for (double b = std::floor (startBeat / beatStep) * beatStep; b <= endBeat; b += beatStep)
        {
            float x = timeToX (ts.toTime (tracktion::BeatPosition::fromBeats (b)).inSeconds());
            if (x < kHeaderWidth) continue;
            if (x > getWidth()) break;

            auto bb = ts.toBarsAndBeats (ts.toTime (tracktion::BeatPosition::fromBeats (b)));
            bool isBar = (bb.beats.inBeats() < 0.001);
            
            g.setColour (Theme::border.withAlpha (isBar ? 1.0f : 0.4f));
            g.drawLine (x, kRulerH - (isBar ? 12.0f : 6.0f), x, (float)kRulerH);
            
            if (isBar || (beatStep < 4.0 && bb.beats.inBeats() > 0.001))
            {
                g.setColour (Theme::textMuted);
                juce::String label = isBar ? juce::String ((int)bb.bars + 1) : juce::String::formatted ("%d.%d", (int)bb.bars + 1, (int)bb.beats.inBeats() + 1);
                g.drawText (label, (int)x + 4, 8, 40, 18, juce::Justification::left);
            }
        }

        auto top = audioEngine.getTopLevelTracks();

        // Clip the lane area so rows don't paint over the footer (zoom slider / hscroll bar).
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (0, laneTop(), getWidth() - kVScrollW, laneBottom() - laneTop());

            int y = laneTop() - scrollY;
            for (int i = 0; i < top.size(); ++i)
                y = drawTrackRow (g, top[i], i, /*indent*/ 0, y);

            if (top.isEmpty()) {
                g.setColour (Theme::textMuted);
                g.setFont (juce::Font (13.0f));
                g.drawText ("No tracks yet. Click  + Track  to begin.",
                            0, kRulerH, getWidth(), 60, juce::Justification::centred);
            }

            if (dragging)
            {
                if (dropFolderTarget != nullptr)
                {
                    for (auto& row : getVisibleRows())
                    {
                        if (row.track == dropFolderTarget)
                        {
                            int ry = kRulerH + row.y - scrollY;
                            g.setColour (Theme::active.withAlpha (0.18f));
                            g.fillRect (0, ry, getWidth() - kVScrollW, row.height);
                            g.setColour (Theme::active);
                            g.drawRect (0, ry, getWidth() - kVScrollW, row.height, 2);
                            break;
                        }
                    }
                }
                else if (dropPreviewY >= 0)
                {
                    g.setColour (Theme::active);
                    g.fillRect (0.0f, (float) dropPreviewY - 1.0f, (float) (getWidth() - kVScrollW), 2.0f);
                }
            }

            // ── File-drag ghost preview ───────────────────────────────────
            if (fileDragActive)
            {
                auto rows = getVisibleRows();
                float gx  = timeToX (fileDragSnappedTime);
                float gw  = juce::jmax (20.0f, (float) (fileDragPreviewLength * pxPerSec));

                if (fileDragTargetRowIdx >= 0 && fileDragTargetRowIdx < rows.size())
                {
                    auto& row = rows.getReference (fileDragTargetRowIdx);
                    int ry = kRulerH + row.y - scrollY;

                    // Row highlight
                    g.setColour (Theme::active.withAlpha (0.12f));
                    g.fillRect (kHeaderWidth, ry, getWidth() - kHeaderWidth - kVScrollW, row.height);
                    g.setColour (Theme::active.withAlpha (0.65f));
                    g.drawRect (kHeaderWidth, ry, getWidth() - kHeaderWidth - kVScrollW, row.height, 1);

                    // Ghost clip
                    juce::Rectangle<float> gc (gx, (float) ry + 4.0f, gw, (float) row.height - 8.0f);
                    g.setColour (Theme::active.withAlpha (0.30f));
                    g.fillRoundedRectangle (gc, 4.0f);
                    g.setColour (Theme::active.withAlpha (0.85f));
                    g.drawRoundedRectangle (gc, 4.0f, 1.5f);
                    g.setColour (Theme::textMain.withAlpha (0.85f));
                    g.setFont (10.0f);
                    g.drawText (juce::String (fileDragSnappedTime, 2) + "s",
                                (int) gx + 5, (int) gc.getY() + 3, 60, 12,
                                juce::Justification::left);
                }
                else
                {
                    // New-track zone: draw accent line below last track + ghost shape
                    int lineY = rows.isEmpty()
                                    ? laneTop() + 4
                                    : kRulerH + rows.getLast().y + rows.getLast().height - scrollY + 2;
                    g.setColour (Theme::active.withAlpha (0.85f));
                    g.fillRect (kHeaderWidth, lineY, getWidth() - kHeaderWidth - kVScrollW, 2);

                    juce::Rectangle<float> gc (gx, (float) lineY + 3.0f, gw, (float) kTrackH - 8.0f);
                    g.setColour (Theme::active.withAlpha (0.25f));
                    g.fillRoundedRectangle (gc, 4.0f);
                    g.setColour (Theme::active.withAlpha (0.75f));
                    g.drawRoundedRectangle (gc, 4.0f, 1.5f);
                }
            }

            // ── Plugin-drag highlight on track header ─────────────────────
            if (pluginDragActive && pluginDragTargetRow >= 0)
            {
                auto rows = getVisibleRows();
                if (pluginDragTargetRow < rows.size())
                {
                    auto& row = rows.getReference (pluginDragTargetRow);
                    int ry = kRulerH + row.y - scrollY;
                    g.setColour (Theme::accent.withAlpha (0.22f));
                    g.fillRect (0, ry, kHeaderWidth, row.height);
                    g.setColour (Theme::accent.withAlpha (0.90f));
                    g.drawRect (0, ry, kHeaderWidth, row.height, 2);
                    g.setColour (Theme::textMain);
                    g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f).withStyle ("Bold")));
                    g.drawText ("DROP PLUGIN", 0, ry + row.height / 2 - 7,
                                kHeaderWidth, 14, juce::Justification::centred);
                }
            }
        }

        // Footer band background.
        g.setColour (Theme::bgPanel);
        g.fillRect (0, getHeight() - kFooterH, getWidth(), kFooterH);
        g.setColour (Theme::border);
        g.drawLine (0.0f, (float)(getHeight() - kFooterH), (float) getWidth(), (float)(getHeight() - kFooterH));

        // Playhead — drawn over lanes but not over the footer.
        float phX = timeToX(audioEngine.getTransportPosition());
        if (phX > kHeaderWidth && phX < getWidth() - kVScrollW) {
            g.setColour (Theme::playhead);
            g.drawLine (phX, 0.0f, phX, (float) (getHeight() - kFooterH), 1.5f);
            juce::Path head;
            head.addTriangle (phX - 6.0f, 0.0f, phX + 6.0f, 0.0f, phX, 10.0f);
            g.fillPath (head);
        }

        // Razor hairline preview
        if (activeTool == EditTool::razor && lastMouseX >= kHeaderWidth && lastMouseX < getWidth() - kVScrollW)
        {
            float drawX = (float) lastMouseX;
            if (snapEnabled)
            {
                auto& ts = audioEngine.getEdit().tempoSequence;
                auto t = tracktion::TimePosition::fromSeconds (xToTime (drawX));
                auto beats = ts.toBeats (t);
                double snappedBeats = std::round (beats.inBeats());
                drawX = timeToX (ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds());
            }

            g.setColour (Theme::accent.withAlpha (0.6f));
            g.drawLine (drawX, (float) kRulerH, drawX, (float) (getHeight() - kFooterH), 1.0f);
        }

        // Draw the tooltip after all other drawing is done
        if (currentTooltip.isValid)
        {
            juce::Font font (12.0f);
            g.setFont (font);
            g.setColour (Theme::surface);
            g.fillRoundedRectangle (currentTooltip.bounds.toFloat(), 3.0f);
            g.setColour (Theme::border);
            g.drawRoundedRectangle (currentTooltip.bounds.toFloat(), 3.0f, 1.0f);
            g.setColour (Theme::textMain);
            g.drawText (currentTooltip.text, currentTooltip.bounds, juce::Justification::centred);
        }
    }

    static void drawHeaderButton (juce::Graphics& g, juce::Rectangle<int> b,
                                  const juce::String& label, juce::Colour col)
    {
        g.setColour(col.withAlpha(0.18f));
        g.fillRoundedRectangle(b.toFloat(), 4.0f);
        g.setColour(col);
        g.drawRoundedRectangle(b.toFloat(), 4.0f, 1.0f);
        g.setFont(juce::Font(11.0f).withStyle(juce::Font::bold));
        g.drawText(label, b, juce::Justification::centred);
    }

    struct RowInfo {
        tracktion::Track* track;
        tracktion::FolderTrack* parent;  // nullptr = top-level
        int y;
        int height;
        int indent;
    };

    juce::Array<RowInfo> getVisibleRows()
    {
        juce::Array<RowInfo> rows;
        int currentRowY = 0;
        auto top = audioEngine.getTopLevelTracks();

        std::function<void(tracktion::Track*, tracktion::FolderTrack*, int)> addTrack =
            [&](tracktion::Track* t, tracktion::FolderTrack* parentFolder, int indent) {
            int th = getTrackHeight (t);
            rows.add ({ t, parentFolder, currentRowY, th, indent });
            currentRowY += th;

            if (auto* f = dynamic_cast<tracktion::FolderTrack*>(t))
                for (auto* child : f->getAllAudioSubTracks (false))
                    addTrack (child, f, indent + 16);
        };

        for (auto* t : top)
            addTrack (t, nullptr, 0);

        return rows;
    }

    int getTrackHeight (tracktion::Track* t) const
    {
        if (automationVisibleTracks.contains (t->itemID.toString()))
            return kTrackH * 2;
        return kTrackH;
    }

    int drawTrackRow(juce::Graphics& g, tracktion::Track* track, int topIndex, int indent, int y)
    {
        auto* folder = dynamic_cast<tracktion::FolderTrack*>(track);
        auto* audio  = dynamic_cast<tracktion::AudioTrack*>(track);
        int   rowH   = getTrackHeight(track);
        juce::Colour tColor = Theme::colourForTrack(topIndex);
        bool isSel = selectedIds.contains(track->itemID.toString());
        bool isAuto = automationVisibleTracks.contains (track->itemID.toString());

        // Lane bg
        g.setColour((topIndex % 2 == 1 ? Theme::bgPanel.withAlpha(0.2f) : juce::Colours::transparentBlack));
        g.fillRect(kHeaderWidth, y, getWidth() - kHeaderWidth, rowH);
        g.setColour(Theme::border.withAlpha(0.3f));
        g.drawLine((float)kHeaderWidth, (float)(y + rowH), (float)getWidth(), (float)(y + rowH));

        // Header
        juce::Rectangle<int> hb(0, y, kHeaderWidth, rowH);
        g.setColour(isSel ? Theme::surface : Theme::bgPanel);
        g.fillRect(hb);
        g.setColour(Theme::border);
        g.drawLine(0.0f, (float)(y + rowH), (float)kHeaderWidth, (float)(y + rowH));
        g.drawLine((float)kHeaderWidth, (float)y, (float)kHeaderWidth, (float)(y + rowH));

        g.setColour(tColor);
        g.fillRect((float)indent, (float)y, 4.0f, (float)rowH);

        const int textX = 14 + indent;
        g.setColour(Theme::textMain);
        g.setFont(juce::Font(13.0f).withStyle(juce::Font::bold));
        juce::String label = track->getName()
                             + (folder ? juce::String("  [GROUP]") : juce::String());
        g.drawText(label, textX, y + 8, kHeaderWidth - textX - 8, 20, juce::Justification::left);

        // M / S / R / A buttons (top row), FX button below.
        int btnY = y + 32;
        auto mB = juce::Rectangle<int>(textX,        btnY, 24, 22);
        auto sB = juce::Rectangle<int>(textX + 28,   btnY, 24, 22);
        auto rB = juce::Rectangle<int>(textX + 56,   btnY, 24, 22);
        auto aB = juce::Rectangle<int>(textX + 84,   btnY, 24, 22);

        bool isMute = track->isMuted(false);
        bool isSolo = track->isSolo(false);
        bool isArm  = audioEngine.isTrackArmed(track);

        drawTrackBtn(g, mB, "M", isMute, Theme::meterYellow);
        drawTrackBtn(g, sB, "S", isSolo, Theme::accent);
        drawTrackBtn(g, rB, "R", isArm,  Theme::recordRed);
        drawTrackBtn(g, aB, "A", isAuto, Theme::active);

        int fxY = btnY + 24;
        auto fxB = juce::Rectangle<int>(textX, fxY, 76, 20);
        int  numFx = track->pluginList.size();
        drawFxBadge (g, fxB, numFx);

        // Automation lane (volume / pan curve, editable).
        if (isAuto)
            drawAutomationLane (g, track, topIndex, y + kTrackH);

        // Clips (audio tracks only)
        if (audio != nullptr)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (kHeaderWidth, y, getWidth() - kHeaderWidth, rowH);

            for (auto* clip : audio->getClips())
            {
                auto start = (float)clip->getPosition().getStart().inSeconds();
                auto len   = (float)clip->getPosition().getLength().inSeconds();
                juce::Rectangle<float> cb(timeToX(start),
                                          (float)y + 2.0f, len * pxPerSec, (float)kTrackH - 4.0f);

                if (cb.getRight() < kHeaderWidth || cb.getX() > getWidth()) continue;

                juce::ColourGradient grad(tColor.brighter(0.2f), 0, cb.getY(),
                                          tColor.darker(0.2f),   0, cb.getBottom(), false);
                g.setGradientFill(grad);
                g.fillRoundedRectangle(cb, 4.0f);
                g.setColour(selectedClip == clip ? Theme::active : tColor.brighter(0.5f).withAlpha(0.4f));
                g.drawRoundedRectangle(cb, 4.0f, (selectedClip == clip ? 2.0f : 1.0f));

                if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*>(clip))
                {
                    auto& thumb = audioEngine.getThumbnailForClip(*wave, *this);
                    g.setColour(juce::Colours::white.withAlpha(0.6f));
                    double offset = wave->getPosition().getOffset().inSeconds();
                    tracktion::TimeRange range(tracktion::TimePosition::fromSeconds(offset), wave->getPosition().getLength());
                    thumb.drawChannel(g, cb.reduced(2).toNearestInt(), range, 0, 1.0f);
                }

                g.setColour(Theme::textMain);
                g.setFont(10.0f);
                g.drawText(clip->getName(), cb.reduced(6, 2).toNearestInt(), juce::Justification::topLeft);
            }
        }

        y += rowH;

        if (folder != nullptr)
            for (auto* child : folder->getAllAudioSubTracks(false))
                y = drawTrackRow(g, child, topIndex, indent + 16, y);

        return y;
    }

    static void drawTrackBtn(juce::Graphics& g, juce::Rectangle<int> b,
                             const juce::String& letter, bool on, juce::Colour activeColour)
    {
        g.setColour(on ? activeColour.withAlpha(0.9f) : Theme::surface);
        g.fillRoundedRectangle(b.toFloat(), 2.0f);
        g.setColour(on ? activeColour : Theme::border);
        g.drawRoundedRectangle(b.toFloat(), 2.0f, 1.0f);
        g.setColour(on ? juce::Colours::black : Theme::textMuted);
        g.setFont(juce::Font(10.0f).withStyle(juce::Font::bold));
        g.drawText(letter, b, juce::Justification::centred);
    }

    static void drawFxBadge (juce::Graphics& g, juce::Rectangle<int> b, int numPlugins)
    {
        bool on = numPlugins > 0;
        juce::ColourGradient grad (Theme::active.withAlpha (on ? 0.35f : 0.18f), b.getX(), b.getY(),
                                    Theme::active.withAlpha (on ? 0.18f : 0.08f), b.getX(), b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b.toFloat(), 3.0f);
        g.setColour (Theme::active.withAlpha (on ? 1.0f : 0.6f));
        g.drawRoundedRectangle (b.toFloat(), 3.0f, 1.0f);
        g.setColour (on ? Theme::active : Theme::textMuted);
        g.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
        g.drawText ("FX", b, juce::Justification::centred);
    }

    // ---- Automation lane helpers ----
    // The lane is split: a header column on the left with a Vol/Pan toggle, and
    // the curve area to the right (aligned to the timeline ruler).
    AudioEngineManager::AutomationParamKind paramKindFor (tracktion::Track* t) const
    {
        auto id = t->itemID.toString();
        if (automationParamChoice.contains (id) && automationParamChoice[id] == 1)
            return AudioEngineManager::AutomationParamKind::Pan;
        return AudioEngineManager::AutomationParamKind::Volume;
    }

    juce::Rectangle<int> getAutomationCurveArea (int laneTopY) const
    {
        return { kHeaderWidth, laneTopY,
                 juce::jmax (0, getWidth() - kHeaderWidth - kVScrollW), kTrackH };
    }

    juce::Rectangle<int> getAutomationToggleArea (int laneTopY) const
    {
        // Pill in the lane header column
        return { 14, laneTopY + kTrackH / 2 - 11, 70, 22 };
    }

    float valueToY (tracktion::AutomatableParameter* param,
                    float value,
                    juce::Rectangle<int> area) const
    {
        if (param->getParameterName().contains ("Pan"))
        {
            float norm = juce::jlimit (-1.0f, 1.0f, value);
            return (float) area.getCentreY() - (norm * (float) area.getHeight() * 0.45f);
        }

        // Volume: native param value → dB via Tracktion formula, then linear fader position → Y
        float db = (value > 0.0f) ? (20.0f * std::log (value)) + 6.0f : -100.0f;
        float sPos = AudioEngineManager::getFaderPosFromDb (db);
        return (float) area.getBottom() - (sPos * (float) area.getHeight());
    }

    float yToValue (tracktion::AutomatableParameter* param,
                    float y,
                    juce::Rectangle<int> area) const
    {
        if (param->getParameterName().contains ("Pan"))
        {
            float norm = (float) (area.getCentreY() - y) / (area.getHeight() * 0.45f);
            return juce::jlimit (-1.0f, 1.0f, norm);
        }

        // Volume: Y → linear fader position → dB → native param value via Tracktion formula
        float sPos = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - y) / (float) area.getHeight());
        float db = AudioEngineManager::getDbFromFaderPos (sPos);
        return (db > -100.0f) ? std::exp ((db - 6.0f) / 20.0f) : 0.0f;
    }

    void drawAutomationLane (juce::Graphics& g, tracktion::Track* track, int /*topIndex*/, int laneTopY)
    {
        auto curveArea = getAutomationCurveArea (laneTopY);

        // Background bands (header + curve area).
        g.setColour (Theme::bgPanel.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<int> (0, laneTopY, kHeaderWidth, kTrackH));
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRect (curveArea);
        g.setColour (Theme::border);
        g.drawLine (0.0f, (float) laneTopY, (float) getWidth(), (float) laneTopY);
        g.drawLine ((float) kHeaderWidth, (float) laneTopY,
                    (float) kHeaderWidth, (float) (laneTopY + kTrackH));

        // Header: param selector + label.
        auto kind = paramKindFor (track);
        bool isPan = (kind == AudioEngineManager::AutomationParamKind::Pan);

        auto pill = getAutomationToggleArea (laneTopY);
        g.setColour (Theme::surface);
        g.fillRoundedRectangle (pill.toFloat(), 3.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (pill.toFloat(), 3.0f, 1.0f);
        auto half = pill.getWidth() / 2;
        juce::Rectangle<int> volR (pill.getX(),         pill.getY(), half,                    pill.getHeight());
        juce::Rectangle<int> panR (pill.getX() + half,  pill.getY(), pill.getWidth() - half,  pill.getHeight());
        g.setColour (! isPan ? Theme::active.withAlpha (0.85f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (volR.toFloat(), 3.0f);
        g.setColour (isPan ? Theme::active.withAlpha (0.85f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (panR.toFloat(), 3.0f);
        g.setFont (juce::Font (9.5f).withStyle (juce::Font::bold));
        g.setColour (! isPan ? juce::Colours::black : Theme::textMuted);
        g.drawText ("VOL", volR, juce::Justification::centred);
        g.setColour (isPan ? juce::Colours::black : Theme::textMuted);
        g.drawText ("PAN", panR, juce::Justification::centred);

        g.setColour (Theme::textMuted);
        g.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
        g.drawText ("AUTOMATION", 14, laneTopY + 6, kHeaderWidth - 28, 14, juce::Justification::left);

        auto* param = audioEngine.getAutomationParam (track, kind);
        if (param == nullptr)
        {
            g.setColour (Theme::textMuted);
            g.setFont (11.0f);
            g.drawText ("(no parameter for this track)", curveArea, juce::Justification::centred);
            return;
        }

        auto& curve = param->getCurve();
        const int n = curve.getNumPoints();

        // Build path. Always anchor at left/right edges using getValueAt().
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (curveArea);

        juce::Path path;
        const double tLeft  = juce::jmax (0.0, xToTime ((float) curveArea.getX()));
        const double tRight = xToTime ((float) curveArea.getRight());

        float yLeft  = valueToY (param, curve.getValueAt (tracktion::TimePosition::fromSeconds (tLeft)), curveArea);
        path.startNewSubPath ((float) curveArea.getX(), yLeft);

        for (int i = 0; i < n; ++i)
        {
            double pt = curve.getPointTime (i).inSeconds();
            if (pt < tLeft) continue;
            if (pt > tRight) break;
            float px = timeToX (pt);
            float py = valueToY (param, curve.getPointValue (i), curveArea);
            path.lineTo (px, py);
        }

        float yRight = valueToY (param, curve.getValueAt (tracktion::TimePosition::fromSeconds (tRight)), curveArea);
        path.lineTo ((float) curveArea.getRight(), yRight);

        g.setColour (Theme::active.withAlpha (0.7f));
        g.strokePath (path, juce::PathStrokeType (1.6f));

        // Centre line for pan (helps visualise centre).
        if (isPan)
        {
            float yMid = valueToY (param, 0.0f, curveArea);
            g.setColour (Theme::textMuted.withAlpha (0.35f));
            g.drawLine ((float) curveArea.getX(), yMid, (float) curveArea.getRight(), yMid, 1.0f);
        }

        // Points.
        for (int i = 0; i < n; ++i)
        {
            double pt = curve.getPointTime (i).inSeconds();
            if (pt < tLeft || pt > tRight) continue;
            float px = timeToX (pt);
            float py = valueToY (param, curve.getPointValue (i), curveArea);
            const float r = (draggingParam == param && draggingPointIdx == i) ? 5.0f : 3.5f;
            g.setColour (Theme::bgBase);
            g.fillEllipse (px - r - 1.0f, py - r - 1.0f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);
            g.setColour (Theme::active);
            g.fillEllipse (px - r, py - r, r * 2.0f, r * 2.0f);

            if (draggingParam == param && draggingPointIdx == i)
            {
                float val = curve.getPointValue (i);
                if (isPan)
                {
                    int panPercent = (int)std::round(val * 100.0f);
                    if (panPercent == 0) currentTooltip.text = "C";
                    else if (panPercent < 0) currentTooltip.text = juce::String::formatted ("L %d", -panPercent);
                    else currentTooltip.text = juce::String::formatted ("R %d", panPercent);
                }
                else
                {
                    float db = (val > 0.0f) ? (20.0f * std::log (val)) + 6.0f : -100.0f;
                    if (db < AudioEngineManager::kMinVolumeDb + 1.0f) currentTooltip.text = "-inf dB";
                    else currentTooltip.text = juce::String::formatted ("%+.1f dB", db);
                }

                juce::Font font (12.0f);
                int tw = font.getStringWidth (currentTooltip.text) + 10;
                currentTooltip.bounds = juce::Rectangle<int> ((int)px - tw / 2, (int)py - 24, tw, 18);
                currentTooltip.isValid = true;
            }
        }

        // Current parameter value indicator on the right edge.
        float yNow = valueToY (param, param->getCurrentBaseValue(), curveArea);
        g.setColour (Theme::active.withAlpha (0.45f));
        g.fillRect ((float) (curveArea.getRight() - 4), yNow - 1.0f, 4.0f, 2.0f);
    }

    int hitTestAutomationPoint (tracktion::AutomatableParameter* param,
                                juce::Rectangle<int> curveArea,
                                juce::Point<int> p) const
    {
        if (param == nullptr) return -1;
        auto& curve = param->getCurve();
        const int n = curve.getNumPoints();
        const float radius = 7.0f;
        for (int i = 0; i < n; ++i)
        {
            double pt = curve.getPointTime (i).inSeconds();
            float px = timeToX (pt);
            float py = valueToY (param, curve.getPointValue (i), curveArea);
            if (juce::Point<float> (px, py).getDistanceFrom (p.toFloat()) <= radius)
                return i;
        }
        return -1;
    }

    bool handleAutomationMouseDown (const juce::MouseEvent& e)
    {
        const int targetY = e.y - kRulerH + scrollY;
        auto rows = getVisibleRows();

        for (int rIdx = 0; rIdx < rows.size(); ++rIdx)
        {
            auto& row = rows.getReference (rIdx);
            if (! automationVisibleTracks.contains (row.track->itemID.toString()))
                continue;
            // Lane occupies the bottom half of an expanded row.
            if (targetY < row.y + kTrackH || targetY >= row.y + row.height)
                continue;

            const int laneTopScreenY = kRulerH + row.y + kTrackH - scrollY;

            // Vol/Pan toggle in lane header column.
            if (e.x < kHeaderWidth)
            {
                auto pill = getAutomationToggleArea (laneTopScreenY);
                if (pill.contains (e.getPosition()))
                {
                    auto id = row.track->itemID.toString();
                    int half = pill.getWidth() / 2;
                    bool clickedPan = (e.x >= pill.getX() + half);
                    automationParamChoice.set (id, clickedPan ? 1 : 0);
                    repaint();
                    return true;
                }
                return true; // consume other header-column clicks inside the lane
            }

            auto curveArea = getAutomationCurveArea (laneTopScreenY);
            if (! curveArea.contains (e.getPosition()))
                return true;

            auto* param = audioEngine.getAutomationParam (row.track, paramKindFor (row.track));
            if (param == nullptr)
                return true;

            auto& curve = param->getCurve();
            int hit = hitTestAutomationPoint (param, curveArea, e.getPosition());

            if (e.mods.isPopupMenu())
            {
                if (hit >= 0)
                {
                    curve.removePoint (hit);
                    repaint();
                }
                return true;
            }

            if (hit < 0)
            {
                // Ensure the volume param range is extended before storing the point value.
                if (paramKindFor (row.track) == AudioEngineManager::AutomationParamKind::Volume)
                    audioEngine.ensureVolumeRange (row.track);
                float val = yToValue (param, (float) e.y, curveArea);
                auto t = tracktion::TimePosition::fromSeconds (juce::jmax (0.0, xToTime ((float) e.x)));
                hit = curve.addPoint (t, val, 0.0f);
            }

            param->parameterChangeGestureBegin();
            automationGestureActive = true;
            draggingParam     = param;
            draggingPointIdx  = hit;
            draggingCurveArea = curveArea;
            automationDragStartY   = e.y;
            automationDragStartVal = curve.getPointValue (hit);
            repaint();
            return true;
        }

        return false;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Footer / vertical scrollbar — don't interfere.
        if (e.y >= laneBottom() || e.x >= getWidth() - kVScrollW) return;

        // + Audio / + MIDI / + Folder bar
        if (e.y < kHeaderBarH && e.x < kHeaderWidth)
        {
            if (addTrackBtn.contains(e.getPosition()))  { if (onAddTrack)  onAddTrack();  return; }
            if (addMidiTrackBtn.contains(e.getPosition())) { if (onAddMidiTrack) onAddMidiTrack(); return; }
            if (addFolderBtn.contains(e.getPosition())) { if (onAddFolder) onAddFolder(); return; }
            return;
        }

        // Ruler -> seek
        if (e.y < kRulerH) {
            audioEngine.setTransportPosition(juce::jmax(0.0, xToTime((float)e.x)));
            repaint();
            return;
        }

        if (handleAutomationMouseDown (e))
            return;

        if (e.x < kHeaderWidth)
        {
            int targetY = e.y - kRulerH + scrollY;
            auto rows = getVisibleRows();
            
            for (int i = 0; i < rows.size(); ++i)
            {
                auto& row = rows.getReference (i);
                if (targetY >= row.y && targetY < row.y + row.height)
                {
                    tracktion::Track* clickedTrack = row.track;
                    int rowTop = kRulerH + row.y - scrollY;
                    int btnY    = rowTop + 32;
                    int fxY     = btnY + 24;
                    int textX   = 14 + row.indent;

                    // Right-click — context menu.
                    if (e.mods.isPopupMenu())
                    {
                        showTrackContextMenu (clickedTrack, e.getScreenPosition());
                        return;
                    }

                    // M / S / R / A hits.
                    if (e.y >= btnY && e.y < btnY + 22)
                    {
                        if (juce::Rectangle<int>(textX,      btnY, 24, 22).contains(e.getPosition())) {
                            audioEngine.toggleTrackMute(clickedTrack); repaint(); return;
                        }
                        if (juce::Rectangle<int>(textX + 28, btnY, 24, 22).contains(e.getPosition())) {
                            audioEngine.toggleTrackSolo(clickedTrack); repaint(); return;
                        }
                        if (juce::Rectangle<int>(textX + 56, btnY, 24, 22).contains(e.getPosition())) {
                            audioEngine.setTrackArmed(clickedTrack, ! audioEngine.isTrackArmed(clickedTrack));
                            repaint();
                            return;
                        }
                        if (juce::Rectangle<int>(textX + 84, btnY, 24, 22).contains(e.getPosition())) {
                            juce::String id = clickedTrack->itemID.toString();
                            if (automationVisibleTracks.contains (id)) automationVisibleTracks.removeString (id);
                            else                                       automationVisibleTracks.add (id);
                            updateScrollBar();
                            repaint();
                            return;
                        }
                    }

                    // FX hit.
                    if (juce::Rectangle<int>(textX, fxY, 76, 20).contains (e.getPosition()))
                    {
                        auto* w = new PluginManagerWindow (clickedTrack, audioEngine);
                        w->toFront(true);
                        return;
                    }

                    auto id = clickedTrack->itemID.toString();
                    if (e.mods.isShiftDown()) {
                        if (selectedIds.contains(id)) selectedIds.removeString(id);
                        else                          selectedIds.add(id);
                    } else {
                        selectedIds.clearQuick();
                        selectedIds.add(id);
                    }
                    if (onSelectionChanged) onSelectionChanged(getSelectedTracks());
                    
                    // find top-level index for onTrackSelected
                    int topIdx = 0;
                    auto top = audioEngine.getTopLevelTracks();
                    for (int j = 0; j < top.size(); ++j)
                    {
                        if (top[j] == clickedTrack) { topIdx = j; break; }
                        if (auto* f = dynamic_cast<tracktion::FolderTrack*>(top[j]))
                        {
                            bool found = false;
                            for (auto* child : f->getAllAudioSubTracks(false))
                                if (child == clickedTrack) { topIdx = j; found = true; break; }
                            if (found) break;
                        }
                    }

                    if (onTrackSelected)    onTrackSelected(topIdx);

                    dragging             = true;
                    dragSourceTrack      = clickedTrack;
                    dropInsertBeforeRowIdx = -1;
                    dropFolderTarget     = nullptr;
                    dropPreviewY         = -1;
                    repaint();
                    return;
                }
            }
            
            selectedIds.clearQuick();
            if (onSelectionChanged) onSelectionChanged({});
            if (onTrackSelected)    onTrackSelected(-1);
            repaint();
            return;
        }

        if (activeTool == EditTool::razor)
        {
            if (auto* clip = getClipAt(e.getPosition()))
            {
                if (auto* ct = clip->getClipTrack())
                {
                    ct->splitClip (*clip, tracktion::TimePosition::fromSeconds (xToTime ((float) e.x)));
                    repaint();
                }
            }
            return;
        }

        selectedClip = getClipAt(e.getPosition());
        if (selectedClip)
        {
            dragOffset = selectedClip->getPosition().getStart().inSeconds() - xToTime((float)e.x);
            
            // Edge detection for trimming
            float clipX = timeToX((float)selectedClip->getPosition().getStart().inSeconds());
            float clipW = (float)selectedClip->getPosition().getLength().inSeconds() * pxPerSec;
            float edgeThreshold = 6.0f;

            if (e.x >= clipX && e.x <= clipX + edgeThreshold)
                dragMode = DragMode::trimLeft;
            else if (e.x >= clipX + clipW - edgeThreshold && e.x <= clipX + clipW)
                dragMode = DragMode::trimRight;
            else
                dragMode = DragMode::move;
        }
        else
        {
            dragMode = DragMode::none;
        }
    }

    void showTrackContextMenu (tracktion::Track* track, juce::Point<int> screenPos)
    {
        auto rows = getVisibleRows();

        // Find the row for this track to check its parent
        tracktion::FolderTrack* parentFolder = nullptr;
        for (auto& row : rows)
            if (row.track == track) { parentFolder = row.parent; break; }

        // Collect available folder targets (exclude the track itself if it is a folder)
        juce::Array<tracktion::FolderTrack*> folders;
        for (auto& row : rows)
            if (auto* f = dynamic_cast<tracktion::FolderTrack*>(row.track))
                if (f != track) folders.add (f);

        juce::PopupMenu m;
        m.addItem (1, "Add Plugin...");
        m.addSeparator();

        if (parentFolder != nullptr)
            m.addItem (3, "Detach from folder");

        if (!folders.isEmpty())
        {
            juce::PopupMenu folderSub;
            for (int i = 0; i < folders.size(); ++i)
                folderSub.addItem (100 + i, folders[i]->getName());
            m.addSubMenu ("Move into folder", folderSub);
        }

        m.addSeparator();
        m.addItem (2, "Delete Track");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
            [this, track, parentFolder, folders] (int chosen) {
                if (chosen == 1)
                {
                    auto here = juce::Rectangle<int> (juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition()).expanded (8);
                    PluginPicker::show (audioEngine, here,
                        [this, track] (const juce::PluginDescription& d) {
                            audioEngine.addPluginToTrack (track, d);
                            repaint();
                        });
                }
                else if (chosen == 2)
                {
                    selectedIds.removeString (track->itemID.toString());
                    audioEngine.deleteTrack (track);
                    if (onTrackSelected) onTrackSelected (-1);
                    repaint();
                }
                else if (chosen == 3)
                {
                    // Detach: move to top level after the parent folder
                    audioEngine.moveTrackAfter (track, parentFolder, nullptr);
                    repaint();
                }
                else if (chosen >= 100 && chosen < 100 + folders.size())
                {
                    auto* targetFolder = folders[chosen - 100];
                    tracktion::Track* lastChild = nullptr;
                    for (auto* child : targetFolder->getAllAudioSubTracks (false))
                        lastChild = child;
                    audioEngine.moveTrackAfter (track, lastChild, targetFolder);
                    repaint();
                }
            });
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        lastMouseX = e.x;
        if (automationGestureActive && draggingParam != nullptr && draggingPointIdx >= 0)
        {
            auto& curve = draggingParam->getCurve();
            if (draggingPointIdx < curve.getNumPoints())
            {
                double t = juce::jmax (0.0, xToTime ((float) e.x));
                float v;
                
                // Delta-based drag: default 0.1 dB/pixel, Shift = 0.01 dB/pixel (fine)
                bool isPanDrag = draggingParam->getParameterName().contains ("Pan");
                int deltaY = e.y - automationDragStartY;

                if (isPanDrag)
                {
                    float sens = e.mods.isShiftDown() ? 0.001f : 0.01f;
                    v = juce::jlimit (-1.0f, 1.0f, automationDragStartVal - (float) deltaY * sens);
                }
                else
                {
                    float sens = e.mods.isShiftDown() ? 0.01f : 0.1f;
                    float startDb = (automationDragStartVal > 0.0f)
                                  ? (20.0f * std::log (automationDragStartVal)) + 6.0f
                                  : AudioEngineManager::kMinVolumeDb;
                    float newDb = juce::jlimit (AudioEngineManager::kMinVolumeDb,
                                               AudioEngineManager::kMaxVolumeDb,
                                               startDb - (float) deltaY * sens);
                    v = (newDb > AudioEngineManager::kMinVolumeDb)
                      ? std::exp ((newDb - 6.0f) / 20.0f) : 0.0f;
                }

                draggingPointIdx = curve.movePoint (draggingPointIdx,
                                                    tracktion::TimePosition::fromSeconds (t),
                                                    v, false);
                repaint();
            }
            return;
        }

        if (e.y < kRulerH && e.y >= kHeaderBarH) {
            audioEngine.setTransportPosition(juce::jmax(0.0, xToTime((float)e.x)));
            repaint();
            return;
        }
        if (dragging && dragSourceTrack != nullptr)
        {
            int virtualY = e.y - kRulerH + scrollY;
            auto rows = getVisibleRows();

            dropInsertBeforeRowIdx = rows.size();  // default: after all rows
            dropFolderTarget       = nullptr;

            for (int i = 0; i < rows.size(); ++i)
            {
                auto& row = rows.getReference (i);
                if (virtualY < row.y + row.height)
                {
                    // Hovering over the middle third of a folder → drop inside it
                    if (auto* f = dynamic_cast<tracktion::FolderTrack*>(row.track))
                    {
                        int midTop = row.y + row.height / 4;
                        int midBot = row.y + row.height * 3 / 4;
                        if (virtualY >= midTop && virtualY < midBot && f != dragSourceTrack)
                        {
                            dropFolderTarget       = f;
                            dropInsertBeforeRowIdx = -1;
                            break;
                        }
                    }
                    // Top half → insert before; bottom half → insert after
                    dropInsertBeforeRowIdx = (virtualY < row.y + row.height / 2) ? i : i + 1;
                    break;
                }
            }

            // Compute the screen Y of the insert line
            if (dropInsertBeforeRowIdx >= 0)
            {
                if (rows.isEmpty())
                    dropPreviewY = laneTop();
                else if (dropInsertBeforeRowIdx == 0)
                    dropPreviewY = kRulerH + rows[0].y - scrollY;
                else if (dropInsertBeforeRowIdx >= rows.size())
                    dropPreviewY = kRulerH + rows.getLast().y + rows.getLast().height - scrollY;
                else
                    dropPreviewY = kRulerH + rows[dropInsertBeforeRowIdx].y - scrollY;
            }
            else
            {
                dropPreviewY = -1;
            }

            repaint();
            return;
        }
        if (selectedClip && dragMode != DragMode::none) {
            double mouseTime = xToTime((float)e.x);
            
            if (dragMode == DragMode::move)
            {
                double newStart = mouseTime + dragOffset;
                if (snapEnabled)
                {
                    auto& ts = audioEngine.getEdit().tempoSequence;
                    auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (newStart));
                    double snappedBeats = std::round (beats.inBeats());
                    newStart = ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds();
                }
                selectedClip->setStart(tracktion::TimePosition::fromSeconds(juce::jmax(0.0, newStart)), false, true);
            }
            else if (dragMode == DragMode::trimLeft)
            {
                auto oldEnd = selectedClip->getPosition().getEnd();
                double newStart = mouseTime;
                if (snapEnabled)
                {
                    auto& ts = audioEngine.getEdit().tempoSequence;
                    auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (newStart));
                    double snappedBeats = std::round (beats.inBeats());
                    newStart = ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds();
                }
                newStart = juce::jmin(newStart, oldEnd.inSeconds() - 0.01);
                selectedClip->setStart(tracktion::TimePosition::fromSeconds(juce::jmax(0.0, newStart)), true, false);
            }
            else if (dragMode == DragMode::trimRight)
            {
                auto start = selectedClip->getPosition().getStart();
                double newEnd = mouseTime;
                if (snapEnabled)
                {
                    auto& ts = audioEngine.getEdit().tempoSequence;
                    auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (newEnd));
                    double snappedBeats = std::round (beats.inBeats());
                    newEnd = ts.toTime (tracktion::BeatPosition::fromBeats (snappedBeats)).inSeconds();
                }
                double newLen = juce::jmax(0.01, newEnd - start.inSeconds());
                selectedClip->setLength(tracktion::TimeDuration::fromSeconds(newLen), true);
            }
            
            repaint();
        }
    }

    bool snapEnabled = true;
    EditTool activeTool = EditTool::select;
    tracktion::Clip* selectedClip = nullptr;
    tracktion::Track* trackBeingRenamed = nullptr;
    juce::TextEditor  trackNameEditor;

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (automationGestureActive)
        {
            if (draggingParam != nullptr)
                draggingParam->parameterChangeGestureEnd();
            automationGestureActive = false;
            draggingParam    = nullptr;
            draggingPointIdx = -1;
            repaint();
            return;
        }

        if (dragging)
        {
            if (dragSourceTrack != nullptr)
            {
                auto rows = getVisibleRows();

                if (dropFolderTarget != nullptr)
                {
                    // Drop into folder as its last child
                    tracktion::Track* lastChild = nullptr;
                    for (auto* child : dropFolderTarget->getAllAudioSubTracks (false))
                        lastChild = child;
                    audioEngine.moveTrackAfter (dragSourceTrack, lastChild, dropFolderTarget);
                }
                else if (dropInsertBeforeRowIdx >= 0)
                {
                    // Determine parent context at the insertion point
                    tracktion::FolderTrack* targetParent =
                        (dropInsertBeforeRowIdx < rows.size()) ? rows[dropInsertBeforeRowIdx].parent : nullptr;

                    // Walk backwards to find the nearest preceding track at the same level
                    tracktion::Track* preceding = nullptr;
                    for (int j = dropInsertBeforeRowIdx - 1; j >= 0; --j)
                    {
                        if (rows[j].parent == targetParent)
                        {
                            preceding = rows[j].track;
                            break;
                        }
                    }

                    // Don't move if nothing changed
                    bool alreadyThere = (preceding == dragSourceTrack) ||
                                        (preceding == nullptr && dropInsertBeforeRowIdx == 0 &&
                                         !rows.isEmpty() && rows[0].track == dragSourceTrack);
                    if (!alreadyThere)
                        audioEngine.moveTrackAfter (dragSourceTrack, preceding, targetParent);
                }
            }

            dragging             = false;
            dragSourceTrack      = nullptr;
            dropInsertBeforeRowIdx = -1;
            dropFolderTarget     = nullptr;
            dropPreviewY         = -1;
            repaint();
            return;
        }

        if (selectedClip && dragMode == DragMode::move)
        {
            if (e.x >= kHeaderWidth && e.y >= kRulerH && e.y < laneBottom())
            {
                const int targetY = e.y - kRulerH + scrollY;
                auto rows = getVisibleRows();
                for (auto& row : rows)
                {
                    if (targetY >= row.y && targetY < row.y + row.height)
                    {
                        if (auto* newTrack = dynamic_cast<tracktion::AudioTrack*>(row.track))
                        {
                            if (newTrack != selectedClip->getTrack())
                                selectedClip->moveTo(*newTrack);
                        }
                        break;
                    }
                }
            }
        }

        dragMode = DragMode::none;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.y < kRulerH) return;

        if (e.x < kHeaderWidth)
        {
            const int targetY = e.y - kRulerH + scrollY;
            auto rows = getVisibleRows();
            for (auto& row : rows)
            {
                if (targetY >= row.y && targetY < row.y + row.height)
                {
                    trackBeingRenamed = row.track;
                    trackNameEditor.setText (trackBeingRenamed->getName(), false);
                    trackNameEditor.setBounds (8, kRulerH + row.y - scrollY + 8, kHeaderWidth - 80, 24);
                    trackNameEditor.setVisible (true);
                    trackNameEditor.grabKeyboardFocus();
                    trackNameEditor.selectAll();
                    return;
                }
            }
            return;
        }

        const int targetY = e.y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        for (int rIdx = 0; rIdx < rows.size(); ++rIdx)
        {
            auto& row = rows.getReference (rIdx);
            if (targetY < row.y || targetY >= row.y + row.height) continue;

            // Don't add MIDI clips when double-clicking inside an automation lane.
            if (automationVisibleTracks.contains (row.track->itemID.toString())
                && targetY >= row.y + kTrackH)
                return;

            if (auto* a = dynamic_cast<tracktion::AudioTrack*>(row.track))
            {
                // If clicking an existing MIDI clip, open the Piano Roll.
                if (auto* existingClip = getClipAt (e.getPosition()))
                {
                    if (auto* midi = dynamic_cast<tracktion::MidiClip*>(existingClip))
                    {
                        auto* win = new PianoRollWindow (*midi, audioEngine.getEdit());
                        (void) win;   // owned by the OS window system; closes via delete this
                        return;
                    }
                }
                // Empty space — insert a new 2-bar MIDI clip.
                const double t0 = xToTime ((float) e.x);
                tracktion::TimeRange range (tracktion::TimePosition::fromSeconds (juce::jmax (0.0, t0)),
                                            tracktion::TimeDuration::fromSeconds (2.0));
                a->insertMIDIClip (range, nullptr);
                repaint();
            }
            return;
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        bool inRuler = (e.y < kRulerH && e.x >= kHeaderWidth);
        bool ctrlDown = e.mods.isCtrlDown();
        bool shiftDown = e.mods.isShiftDown();

        if (inRuler || ctrlDown)
        {
            double mouseTime = xToTime ((float)e.x);
            double zoomFactor = 1.0 + wheel.deltaY * 0.1;
            pxPerSec = juce::jlimit (1.0, 2000.0, pxPerSec * zoomFactor);
            zoomSlider.setValue (pxPerSec, juce::dontSendNotification);

            startTime = mouseTime - (e.x - kHeaderWidth) / pxPerSec;
            startTime = juce::jmax (0.0, startTime);

            updateScrollBar();
            repaint();
        }
        else if (shiftDown)
        {
            startTime -= wheel.deltaY * (100.0 / pxPerSec);
            startTime = juce::jmax (0.0, startTime);
            updateScrollBar();
            repaint();
        }
        else
        {
            // Default vertical scroll through the track list.
            scrollY -= (int) (wheel.deltaY * 60.0);
            updateScrollBar();
            repaint();
        }
    }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { updateScrollBar(); repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { updateScrollBar(); repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { updateScrollBar(); repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { updateScrollBar(); repaint(); }

private:
    tracktion::Clip* getClipAt(juce::Point<int> p)
    {
        if (p.x < kHeaderWidth || p.y < kRulerH || p.y >= laneBottom()) return nullptr;
        const int targetY = p.y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        for (int rIdx = 0; rIdx < rows.size(); ++rIdx)
        {
            auto& row = rows.getReference (rIdx);
            if (targetY < row.y || targetY >= row.y + row.height) continue;
            // Ignore clicks inside the track's automation lane area.
            if (automationVisibleTracks.contains (row.track->itemID.toString())
                && targetY >= row.y + kTrackH)
                return nullptr;
            if (auto* a = dynamic_cast<tracktion::AudioTrack*> (row.track)) {
                double time = xToTime ((float) p.x);
                for (auto* c : a->getClips())
                    if (c->getPosition().time.contains (tracktion::TimePosition::fromSeconds (time)))
                        return c;
            }
            return nullptr;
        }
        return nullptr;
    }

    // ── File-drag ghost state ────────────────────────────────────────────────
    bool              fileDragActive        = false;
    juce::StringArray fileDragFiles;
    double            fileDragSnappedTime   = 0.0;
    int               fileDragTargetRowIdx  = -1;   // -1 = drop creates new track
    double            fileDragPreviewLength = 0.0;  // sum of dragged file durations (seconds)

    // ── Plugin-drag state (DragAndDropTarget from Browser) ──────────────────
    bool pluginDragActive    = false;
    int  pluginDragTargetRow = -1;

    AudioEngineManager& audioEngine;
    ProjectData& projectData;
    double dragOffset = 0.0;
    int lastMouseX = -1;
    juce::StringArray selectedIds;
    juce::StringArray automationVisibleTracks;
    juce::HashMap<juce::String, int> automationParamChoice;  // 0=Volume, 1=Pan
    tracktion::AutomatableParameter* draggingParam = nullptr;
    int draggingPointIdx = -1;
    juce::Rectangle<int> draggingCurveArea;
    float automationDragStartVal = 0.0f;
    int   automationDragStartY   = 0;
    bool automationGestureActive = false;
    bool dragging = false;
    tracktion::Track*      dragSourceTrack      = nullptr;
    int                    dropInsertBeforeRowIdx = -1;
    tracktion::FolderTrack* dropFolderTarget    = nullptr;
    int  dropPreviewY  = -1;
    juce::Rectangle<int> addTrackBtn, addMidiTrackBtn, addFolderBtn;

    double startTime = 0.0;
    double pxPerSec  = 100.0;
    int    scrollY   = 0;
    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar   { true };
    juce::Slider    zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    struct TooltipInfo
    {
        juce::String text;
        juce::Rectangle<int> bounds;
        bool isValid = false;
    };
    TooltipInfo currentTooltip;

    // ── Private helpers ──────────────────────────────────────────────────────

    void updateFileDragState (int x, int y)
    {
        // Time position (snapped to beat grid if snap is on)
        double rawTime = xToTime ((float) x);
        if (snapEnabled)
        {
            auto& ts = audioEngine.getEdit().tempoSequence;
            auto beats = ts.toBeats (tracktion::TimePosition::fromSeconds (rawTime));
            double snapped = std::round (beats.inBeats());
            rawTime = ts.toTime (tracktion::BeatPosition::fromBeats (snapped)).inSeconds();
        }
        fileDragSnappedTime = juce::jmax (0.0, rawTime);

        // Which track row is under the cursor?
        const int virtualY = y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        fileDragTargetRowIdx = -1;
        for (int i = 0; i < rows.size(); ++i)
        {
            if (virtualY >= rows[i].y && virtualY < rows[i].y + rows[i].height)
            {
                // Only land on audio tracks; folders and others → create new track
                if (dynamic_cast<tracktion::AudioTrack*> (rows[i].track) != nullptr)
                    fileDragTargetRowIdx = i;
                break;
            }
        }

        // Ghost clip width = sum of dragged audio file durations
        fileDragPreviewLength = 0.0;
        for (auto& s : fileDragFiles)
        {
            juce::File f (s);
            if (f.existsAsFile())
            {
                tracktion::AudioFile af (audioEngine.getEngine(), f);
                double len = af.getLength();
                if (len > 0.0) fileDragPreviewLength += len;
            }
        }
        if (fileDragPreviewLength <= 0.0)
            fileDragPreviewLength = 2.0; // fallback width so ghost is visible
    }

    void updatePluginDragTarget (int x, int y)
    {
        if (x >= kHeaderWidth || y < kRulerH || y >= laneBottom())
        {
            pluginDragTargetRow = -1;
            return;
        }
        const int virtualY = y - kRulerH + scrollY;
        auto rows = getVisibleRows();
        pluginDragTargetRow = -1;
        for (int i = 0; i < rows.size(); ++i)
            if (virtualY >= rows[i].y && virtualY < rows[i].y + rows[i].height)
                { pluginDragTargetRow = i; break; }
    }
};

//==============================================================================
// Reaper-style mixer: each strip has a clickable inserts list, a functional pan
// knob, M/S buttons, a fader with dB readout and meter. Detachable to a floating
// window via the header pop-out button.
class Mixer : public juce::Component,
              public juce::DragAndDropTarget,
              public juce::ValueTree::Listener,
              private juce::Timer
{
public:
    void timerCallback() override { repaint(); }

    static constexpr int kStripW       = 110;
    static constexpr int kHeaderH      = 28;
    static constexpr int kColorBandH   = 4;
    static constexpr int kNameH        = 20;
    static constexpr int kInsertSlots  = 4;
    static constexpr int kInsertSlotH  = 16;
    static constexpr int kPanH         = 22;   // horizontal pan slider, much smaller than the old knob
    static constexpr int kBottomH      = 22;
    static constexpr int kStripGap     = 6;
    static constexpr int kMasterGap    = 18;

    std::function<void()> onDetachRequested;
    std::function<void(tracktion::Track*, const juce::PluginDescription&)> onPluginDroppedOnStrip;
    bool detached = false;

    std::unique_ptr<juce::Drawable> faderKnobDrawable;

    Mixer(AudioEngineManager& ae, ProjectData& pd) : audioEngine(ae), projectData(pd)
    {
        projectData.getProjectTree().addListener (this);
        startTimerHz (30); // drives meter animation

        if (auto svgXml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::aerion_fader_svg, BinaryData::aerion_fader_svgSize)))
            faderKnobDrawable = juce::Drawable::createFromSVG (*svgXml);
    }

    ~Mixer() override { projectData.getProjectTree().removeListener (this); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { repaint(); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bgBase);

        // Header strip.
        auto header = getLocalBounds().removeFromTop(kHeaderH);
        g.setColour(Theme::bgPanel);
        g.fillRect(header);
        g.setColour(Theme::border);
        g.drawLine(0.0f, (float)kHeaderH, (float)getWidth(), (float)kHeaderH);
        g.setColour(Theme::active);
        g.setFont(juce::Font(10.0f).withStyle(juce::Font::bold));
        g.drawText("CONSOLE", 16, 0, 100, kHeaderH, juce::Justification::centredLeft);

        // Detach / dock button.
        detachBtn = header.removeFromRight(72).reduced(4, 4);
        g.setColour(Theme::surface);
        g.fillRoundedRectangle(detachBtn.toFloat(), 3.0f);
        g.setColour(Theme::active);
        g.drawRoundedRectangle(detachBtn.toFloat(), 3.0f, 1.0f);
        g.setColour(Theme::active);
        g.setFont(juce::Font(9.0f).withStyle(juce::Font::bold));
        g.drawText(detached ? juce::String("DOCK") : juce::String("POP OUT"), detachBtn, juce::Justification::centred);

        auto tracks = audioEngine.getAudioTracks();
        if (tracks.isEmpty())
        {
            g.setColour(Theme::textMuted);
            g.setFont(12.0f);
            g.drawText("Add a track to see the console.",
                       0, kHeaderH, getWidth(), getHeight() - kHeaderH, juce::Justification::centred);
            stripHits.clearQuick();
            return;
        }

        stripHits.clearQuick();

        int x = 12;
        int y = kHeaderH + 8;
        int h = getHeight() - y - 12;

        for (int i = 0; i <= tracks.size(); ++i)
        {
            bool isMaster = (i == tracks.size());
            tracktion::Track* track = isMaster ? audioEngine.getMasterTrack() : (tracktion::Track*) tracks[i];
            juce::Colour tColor = isMaster ? Theme::meterRed : Theme::colourForTrack(i);

            paintStrip(g, juce::Rectangle<int>(x, y, kStripW, h), track, tColor, isMaster);

            x += kStripW + kStripGap;
            if (isMaster) break;
            if (i == tracks.size() - 1) x += kMasterGap;
        }
    }

    void paintStrip(juce::Graphics& g, juce::Rectangle<int> cb,
                    tracktion::Track* track, juce::Colour tColor, bool isMaster)
    {
        StripHit hit;
        hit.track    = track;
        hit.isMaster = isMaster;

        // Background panel.
        g.setColour(Theme::bgPanel);
        g.fillRoundedRectangle(cb.toFloat(), 4.0f);
        g.setColour(Theme::border);
        g.drawRoundedRectangle(cb.toFloat(), 4.0f, 1.0f);

        // Color band at the top.
        auto inner = cb.reduced(4);
        auto band = inner.removeFromTop(kColorBandH);
        g.setColour(tColor);
        g.fillRoundedRectangle(band.toFloat(), 2.0f);

        // Name.
        auto name = inner.removeFromTop(kNameH);
        g.setColour(Theme::textMain);
        g.setFont(juce::Font(11.0f).withStyle(juce::Font::bold));
        g.drawText(isMaster ? juce::String("MASTER") : track->getName(),
                   name, juce::Justification::centred);

        inner.removeFromTop(2);

        // Inserts area: kInsertSlots clickable rows.
        auto insertsBox = inner.removeFromTop(kInsertSlots * kInsertSlotH + 4);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(insertsBox.toFloat(), 2.0f);
        g.setColour(Theme::border);
        g.drawRoundedRectangle(insertsBox.toFloat(), 2.0f, 1.0f);

        auto rowsArea = insertsBox.reduced(2);
        int  pluginIdx = 0;
        for (int s = 0; s < kInsertSlots; ++s)
        {
            juce::Rectangle<int> slot = rowsArea.removeFromTop(kInsertSlotH);
            tracktion::Plugin* plugin = nullptr;

            // Walk the plugin list, skipping internal utility plugins.
            int seen = 0;
            for (auto* p : track->pluginList)
            {
                if (dynamic_cast<tracktion::ExternalPlugin*>(p) == nullptr) continue;
                if (seen == s) { plugin = p; break; }
                ++seen;
            }

            if (plugin != nullptr)
            {
                g.setColour(Theme::surface.brighter(0.1f));
                g.fillRect(slot.reduced(1));
                g.setColour(Theme::active);
                g.fillRect(slot.getX() + 2, slot.getY() + 2, 2, slot.getHeight() - 4);
                g.setColour(Theme::textMain);
                g.setFont(juce::Font(10.0f));
                g.drawText(plugin->getName(), slot.withTrimmedLeft(8).withTrimmedRight(2),
                           juce::Justification::centredLeft);

                hit.insertSlots.add (slot);
                hit.insertPlugins.add (plugin);
            }
            else
            {
                if (s == pluginIdx) // first empty slot — show "+ FX" hint
                {
                    g.setColour(Theme::textMuted.withAlpha(0.4f));
                    g.setFont(juce::Font(9.0f));
                    g.drawText("+ FX", slot, juce::Justification::centred);
                }
            }

            // Always make the slot clickable so we can pick the plugin add-target.
            hit.insertEmptySlots.add (slot);
            ++pluginIdx;
        }

        // Pan: horizontal slider centred at 0.
        inner.removeFromTop(2);
        auto panArea = inner.removeFromTop(kPanH);
        float pan = audioEngine.getTrackPan(track);
        drawPanSlider(g, panArea, pan, true);
        hit.panArea = panArea;

        // M / S row at the very bottom.
        auto msRow = inner.removeFromBottom(kBottomH).reduced(0, 2);
        int msW = (msRow.getWidth() - 4) / 2;
        auto mB = msRow.removeFromLeft(msW);
        msRow.removeFromLeft(4);
        auto sB = msRow.removeFromLeft(msW);

        Timeline::drawTrackBtn(g, mB, "M", track->isMuted(false), Theme::meterYellow);
        Timeline::drawTrackBtn(g, sB, "S", track->isSolo(false), Theme::accent);
        hit.muteBtn = mB;
        hit.soloBtn = sB;

        // Fader area = whatever is left between pan and M/S row.
        auto faderArea = inner;
        ::paintFader(g, faderArea, audioEngine, track, tColor, isMaster, faderKnobDrawable.get(), &hit.peakReadoutArea);
        hit.faderArea = faderArea;

        // Plugin-drag hover highlight
        if (! isMaster && pluginDragHoverTrack == track)
        {
            g.setColour (Theme::accent.withAlpha (0.18f));
            g.fillRoundedRectangle (cb.toFloat(), 4.0f);
            g.setColour (Theme::accent.withAlpha (0.90f));
            g.drawRoundedRectangle (cb.toFloat(), 4.0f, 2.0f);
        }

        stripHits.add (hit);
        }

        static void drawPanSlider (juce::Graphics& g, juce::Rectangle<int> b, float pan, bool enabled)
        {
        // Track.
        auto track = juce::Rectangle<int> (b.getX() + 6, b.getY() + 4, b.getWidth() - 12, 4);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (track.toFloat(), 2.0f);

        // Centre tick.
        g.setColour (Theme::border);
        g.drawLine ((float) track.getCentreX(), (float) (track.getY() - 2),
                    (float) track.getCentreX(), (float) (track.getBottom() + 2), 1.0f);

        // Filled segment from centre to current value.
        if (enabled)
        {
            float t = juce::jlimit (-1.0f, 1.0f, pan);
            int   half = track.getWidth() / 2;
            int   cx   = track.getCentreX();
            int   x0   = t < 0.0f ? cx + (int) (t * half) : cx;
            int   w    = (int) (std::abs (t) * half);
            g.setColour (Theme::active);
            g.fillRoundedRectangle ((float) x0, (float) track.getY(),
                                    (float) w, (float) track.getHeight(), 2.0f);

            // Thumb.
            int thumbX = cx + (int) (t * half);
            juce::Rectangle<float> thumb ((float) (thumbX - 4), (float) (track.getY() - 3),
                                           8.0f, (float) (track.getHeight() + 6));
            g.setColour (Theme::textMain);
            g.fillRoundedRectangle (thumb, 2.0f);
        }

        // Label below the slider.
        g.setColour (Theme::textMuted);
        g.setFont (juce::Font (9.0f).withStyle (juce::Font::bold));
        juce::String label = enabled
            ? (juce::approximatelyEqual (pan, 0.0f) ? juce::String ("C")
              : juce::String::formatted (pan < 0 ? "L%d" : "R%d", (int) std::round (std::abs (pan) * 100.0f)))
            : juce::String ("---");
        g.drawText (label,
                    juce::Rectangle<int> (b.getX(), b.getY() + 10, b.getWidth(), b.getHeight() - 10),
                    juce::Justification::centred);
        }


        void mouseDown(const juce::MouseEvent& e) override
        {
        if (detachBtn.contains(e.getPosition())) {
            if (onDetachRequested) onDetachRequested();
            return;
        }

        for (auto& hit : stripHits)
        {
            if (hit.muteBtn.contains(e.getPosition())) { audioEngine.toggleTrackMute(hit.track); repaint(); return; }
            if (hit.soloBtn.contains(e.getPosition())) { audioEngine.toggleTrackSolo(hit.track); repaint(); return; }

            // Peak readout click -> reset peak.
            if (hit.peakReadoutArea.contains (e.getPosition()))
            {
                audioEngine.clearTrackMaxPeak (hit.track);
                repaint();
                return;
            }

            // Pan slider: click sets initial position, drag continues from there.
            if (hit.panArea.contains(e.getPosition())) {
                activePanTrack  = hit.track;
                activePanArea   = hit.panArea;
                dragStartY      = e.y;
                panAtDragStart  = audioEngine.getTrackPan (hit.track);
                setPanFromX(activePanTrack, activePanArea, e.x);
                return;
            }

            // Existing plugin slot — double-click handled in mouseDoubleClick.
            // Right-click on a populated slot → remove plugin.
            for (int i = 0; i < hit.insertSlots.size(); ++i)
            {
                if (hit.insertSlots[i].contains(e.getPosition()))
                {
                    if (e.mods.isPopupMenu())
                    {
                        showInsertContextMenu(hit.insertPlugins[i], hit.track, e.getScreenPosition());
                        return;
                    }
                    return;
                }
            }

            // Empty slot click — open plugin picker for this track.
            for (auto& slot : hit.insertEmptySlots)
            {
                if (slot.contains(e.getPosition()))
                {
                    bool slotIsPopulated = false;
                    for (auto& used : hit.insertSlots)
                        if (used == slot) { slotIsPopulated = true; break; }
                    if (slotIsPopulated) continue;

                    auto* trk = hit.track;
                    auto screen = localAreaToGlobal(slot);
                    PluginPicker::show(audioEngine, screen, [this, trk] (const juce::PluginDescription& d) {
                        if (auto p = audioEngine.addPluginToTrack(trk, d))
                            p->showWindowExplicitly();
                        repaint();
                    });
                    return;
                }
            }

            // Fader area: capture for drag only, do not jump on click.
            if (hit.faderArea.contains(e.getPosition()))
            {
                activeFaderTrack = hit.track;
                return;
            }
        }
        }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (activePanTrack)
        {
            float delta = (dragStartY - e.y) / 100.0f; // up = right, down = left
            audioEngine.setTrackPan(activePanTrack, juce::jlimit(-1.0f, 1.0f, panAtDragStart + delta));
            repaint();
            return;
        }

        if (activeFaderTrack)
        {
            for (auto& hit : stripHits)
                if (hit.track == activeFaderTrack)
                { ::setFaderFromY(audioEngine, activeFaderTrack, hit.faderArea, e.y); break; }
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        activePanTrack   = nullptr;
        activeFaderTrack = nullptr;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        for (auto& hit : stripHits)
        {
            if (hit.faderArea.contains (e.getPosition()))
            {
                audioEngine.setTrackVolumeDb (hit.track, 0.0f);
                repaint();
                return;
            }

            if (hit.panArea.contains (e.getPosition()))
            {
                audioEngine.setTrackPan (hit.track, 0.0f);
                repaint();
                return;
            }

            for (int i = 0; i < hit.insertSlots.size(); ++i)
                if (hit.insertSlots[i].contains(e.getPosition()))
                {
                    auto p = hit.insertPlugins[i];
                    p->setEnabled(true);
                    p->setProcessingEnabled(true);
                    p->showWindowExplicitly();
                    return;
                }
        }
    }

private:
    struct StripHit
    {
        tracktion::Track* track = nullptr;
        bool isMaster = false;
        juce::Rectangle<int> muteBtn, soloBtn, panArea, faderArea, peakReadoutArea;
        juce::Array<juce::Rectangle<int>>      insertSlots;
        juce::Array<juce::Rectangle<int>>      insertEmptySlots;
        juce::Array<tracktion::Plugin*>        insertPlugins;
    };

    void setPanFromX(tracktion::Track* t, juce::Rectangle<int> area, int x)
    {
        if (! t) return;
        // The pan slider track is smaller than the full panArea
        auto sliderTrack = juce::Rectangle<int>(area.getX() + 6, area.getY() + 4, area.getWidth() - 12, 4);
        float pPos = juce::jlimit(0.0f, 1.0f, (float)(x - sliderTrack.getX()) / (float)sliderTrack.getWidth());
        audioEngine.setTrackPan(t, (pPos * 2.0f) - 1.0f);
    }

    void showInsertContextMenu(tracktion::Plugin* plugin, tracktion::Track* track, juce::Point<int> screenPos)
    {
        juce::PopupMenu m;
        m.addItem (1, "Open Editor");
        m.addSeparator();
        m.addItem (2, "Remove Plugin");

        m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
            [this, plugin] (int chosen) {
                if (chosen == 1 && plugin) plugin->showWindowExplicitly();
                else if (chosen == 2 && plugin) {
                    audioEngine.removePlugin(plugin);
                    repaint();
                }
            });
    }

    AudioEngineManager& audioEngine;
    ProjectData& projectData;

    juce::Rectangle<int>     detachBtn;
    juce::Array<StripHit>    stripHits;

    tracktion::Track*       activePanTrack   = nullptr;
    juce::Rectangle<int>    activePanArea;
    tracktion::Track*       activeFaderTrack = nullptr;
    float panAtDragStart = 0.0f;
    int   dragStartY     = 0;

    // Plugin drag hover state
    tracktion::Track* pluginDragHoverTrack = nullptr;

    // ── DragAndDropTarget (plugin drag from Browser) ─────────────────────────
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        return d.description.toString().startsWith ("PLUGIN:");
    }
    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragHoverTrack = getStripTrackAt (d.localPosition);
        repaint();
    }
    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        pluginDragHoverTrack = getStripTrackAt (d.localPosition);
        repaint();
    }
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails&) override
    {
        pluginDragHoverTrack = nullptr;
        repaint();
    }
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        auto* track = getStripTrackAt (d.localPosition);
        pluginDragHoverTrack = nullptr;

        if (track != nullptr && onPluginDroppedOnStrip)
        {
            juce::String idStr = d.description.toString()
                                     .fromFirstOccurrenceOf ("PLUGIN:", false, false);
            auto& known = audioEngine.getEngine().getPluginManager().knownPluginList;
            for (auto& t : known.getTypes())
            {
                if (t.createIdentifierString() == idStr)
                {
                    onPluginDroppedOnStrip (track, t);
                    break;
                }
            }
        }
        repaint();
    }

    tracktion::Track* getStripTrackAt (juce::Point<int> pos)
    {
        // Re-derive strip geometry (stripHits is only valid within a paint() call)
        auto tracks = audioEngine.getAudioTracks();
        int x = 12;
        for (int i = 0; i < tracks.size(); ++i)
        {
            juce::Rectangle<int> strip (x, kHeaderH, kStripW, getHeight() - kHeaderH - 8);
            if (strip.contains (pos))
                return tracks[i];
            x += kStripW + kStripGap;
        }
        return nullptr;
    }
};

//==============================================================================
// Bottom transport bar — buttons + bars/beats counter + tempo + time signature.
class Transport : public juce::Component,
                  public juce::ValueTree::Listener
{
public:
    Transport(AudioEngineManager& ae, ProjectData& pd) : audioEngine(ae), projectData(pd)
    {
        projectData.getProjectTree().addListener (this);

        auto setupLabel = [this] (juce::Label& l, const char* suffix = nullptr) {
            l.setFont (juce::Font (20.0f).withStyle (juce::Font::bold));
            l.setJustificationType (juce::Justification::centredLeft);
            l.setColour (juce::Label::textColourId, Theme::active);
            l.setEditable (true, false, false);
            l.setColour (juce::Label::textWhenEditingColourId, Theme::textMain);
            l.setColour (juce::Label::backgroundWhenEditingColourId, Theme::surface);
            addAndMakeVisible (l);
        };

        setupLabel (tempoLabel);
        tempoLabel.onTextChange = [this] {
            double bpm = tempoLabel.getText().getDoubleValue();
            if (bpm > 0) audioEngine.setTempo (bpm);
        };

        setupLabel (timeSigLabel);
        timeSigLabel.onTextChange = [this] {
            juce::String s = timeSigLabel.getText();
            int n = s.upToFirstOccurrenceOf ("/", false, false).getIntValue();
            int d = s.fromFirstOccurrenceOf ("/", false, false).getIntValue();
            if (n > 0 && d > 0) audioEngine.setTimeSig (n, d);
        };
    }

    ~Transport() override { projectData.getProjectTree().removeListener (this); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { repaint(); }

    void resized() override
    {
        // Right side counters.
        int rx = getWidth() - 360;
        rx += 210;
        tempoBounds = juce::Rectangle<int>(rx, 8, 80, 28);
        tempoLabel.setBounds (tempoBounds);

        rx += 90;
        timeSigBounds = juce::Rectangle<int>(rx, 8, 60, 28);
        timeSigLabel.setBounds (timeSigBounds);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::bgPanel);
        g.setColour(Theme::border);
        g.drawLine(0.0f, 0.0f, (float)getWidth(), (float)0.0f);

        // Update labels if not editing
        double pos = audioEngine.getTransportPosition();
        if (! tempoLabel.isBeingEdited())
            tempoLabel.setText (juce::String::formatted ("%.2f", audioEngine.getTempoAtPosition (pos)), juce::dontSendNotification);
        
        if (! timeSigLabel.isBeingEdited())
            timeSigLabel.setText (audioEngine.getTimeSigAtPosition (pos), juce::dontSendNotification);

        // Layout: [perf/cpu] [transport buttons] [time/tempo/sig] [metronome]
        const int btnW = 44;
        const int btnH = 36;
        int cy = (getHeight() - btnH) / 2;
        int cx = getWidth() / 2 - (btnW * 6 + 5 * 6) / 2;

        rewindBounds = juce::Rectangle<int>(cx,                cy, btnW, btnH); cx += btnW + 6;
        forwardBounds= juce::Rectangle<int>(cx,                cy, btnW, btnH); cx += btnW + 6;
        stopBounds   = juce::Rectangle<int>(cx,                cy, btnW, btnH); cx += btnW + 6;
        playBounds   = juce::Rectangle<int>(cx,                cy, btnW, btnH); cx += btnW + 6;
        recBounds    = juce::Rectangle<int>(cx,                cy, btnW, btnH); cx += btnW + 6;
        loopBounds   = juce::Rectangle<int>(cx,                cy, btnW, btnH);

        drawBtn(g, rewindBounds.toFloat(),  Glyph::rewind);
        drawBtn(g, forwardBounds.toFloat(), Glyph::forward);
        drawBtn(g, stopBounds.toFloat(),    Glyph::stop);
        drawBtn(g, playBounds.toFloat(),    Glyph::play,   audioEngine.isPlaying());
        drawBtn(g, recBounds.toFloat(),     Glyph::record, audioEngine.isRecording());
        drawBtn(g, loopBounds.toFloat(),    Glyph::loop,   audioEngine.getEdit().getTransport().looping);

        // Right side counters.
        juce::String counter = audioEngine.getBarsBeatsString(pos);

        int rx = getWidth() - 360;
        g.setColour(Theme::active);
        g.setFont(juce::Font(20.0f).withStyle(juce::Font::bold));
        g.drawText(counter, rx, 8, 200, 28, juce::Justification::centredLeft);
        g.setColour(Theme::textMuted);
        g.setFont(juce::Font(8.0f).withStyle(juce::Font::bold));
        g.drawText("BARS . BEATS . TICKS", rx, getHeight() - 16, 200, 12, juce::Justification::centredLeft);

        rx += 210;
        g.setColour(Theme::textMuted.withAlpha(0.5f));
        g.setFont(juce::Font(8.0f).withStyle(juce::Font::bold));
        g.drawText("TEMPO", rx, getHeight() - 16, 80, 12, juce::Justification::centredLeft);

        rx += 90;
        g.setColour(Theme::textMuted.withAlpha(0.5f));
        g.setFont(juce::Font(8.0f).withStyle(juce::Font::bold));
        g.drawText("TIME", rx, getHeight() - 16, 60, 12, juce::Justification::centredLeft);

        // Left side: Perform / CPU mock
        g.setColour(Theme::textMuted.withAlpha(0.4f));
        g.setFont(juce::Font(9.0f).withStyle(juce::Font::bold));
        g.drawText("PERFORM", 16, 12, 60, 12, juce::Justification::left);
        g.drawText("CPU",     16, 32, 60, 12, juce::Justification::left);
        g.setColour(Theme::surface.withAlpha(0.5f));
        g.fillRoundedRectangle(70.0f, 14.0f, 80.0f, 6.0f, 2.0f);
        g.fillRoundedRectangle(70.0f, 34.0f, 80.0f, 6.0f, 2.0f);
        g.setColour(Theme::accent.withAlpha(0.2f));
        g.fillRoundedRectangle(70.0f, 14.0f, 30.0f, 6.0f, 2.0f);
        g.fillRoundedRectangle(70.0f, 34.0f, 22.0f, 6.0f, 2.0f);
    }

    enum class Glyph { play, stop, record, rewind, forward, loop };

    static void drawBtn(juce::Graphics& g, juce::Rectangle<float> b, Glyph k, bool active = false)
    {
        juce::Colour col = (k == Glyph::record) ? Theme::recordRed : Theme::active;
        g.setColour(active ? col.withAlpha(0.2f) : Theme::surface);
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(active ? col : Theme::border);
        g.drawRoundedRectangle(b, 4.0f, 1.0f);
        g.setColour(active ? col : Theme::textMain);

        auto cx = b.getCentreX();
        auto cy = b.getCentreY();
        switch (k)
        {
            case Glyph::play: {
                juce::Path p;
                p.addTriangle(cx - 5, cy - 7, cx - 5, cy + 7, cx + 7, cy);
                g.fillPath(p);
            } break;
            case Glyph::stop:
                g.fillRect(cx - 6, cy - 6, 12.0f, 12.0f);
                break;
            case Glyph::record:
                g.setColour(Theme::recordRed);
                g.fillEllipse(cx - 7, cy - 7, 14.0f, 14.0f);
                break;
            case Glyph::rewind: {
                juce::Path p;
                p.addTriangle(cx - 8, cy, cx,     cy - 6, cx,     cy + 6);
                p.addTriangle(cx,     cy, cx + 8, cy - 6, cx + 8, cy + 6);
                g.fillPath(p);
            } break;
            case Glyph::forward: {
                juce::Path p;
                p.addTriangle(cx - 8, cy - 6, cx - 8, cy + 6, cx,     cy);
                p.addTriangle(cx,     cy - 6, cx,     cy + 6, cx + 8, cy);
                g.fillPath(p);
            } break;
            case Glyph::loop: {
                g.drawEllipse(cx - 8, cy - 6, 16.0f, 12.0f, 1.5f);
                juce::Path arrow;
                arrow.addTriangle(cx + 6, cy - 8, cx + 10, cy, cx + 2, cy);
                g.fillPath(arrow);
            } break;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (playBounds.contains(e.getPosition())) audioEngine.play();
        if (stopBounds.contains(e.getPosition())) audioEngine.stop();
        if (recBounds.contains(e.getPosition()))  audioEngine.record();
        
        if (rewindBounds.contains(e.getPosition())) 
        {
            if (audioEngine.getTransportPosition() > 0.1)
                audioEngine.setTransportPosition(0.0);
            else
                audioEngine.setTransportPosition(juce::jmax(0.0, audioEngine.getTransportPosition() - 4.0));
        }
        
        if (forwardBounds.contains(e.getPosition()))
            audioEngine.setTransportPosition(audioEngine.getTransportPosition() + 4.0);
            
        if (loopBounds.contains(e.getPosition())) {
            auto& t = audioEngine.getEdit().getTransport();
            t.looping = ! t.looping;
        }

        repaint();
    }

private:
    AudioEngineManager& audioEngine;
    ProjectData& projectData;
    juce::Rectangle<int> playBounds, stopBounds, recBounds, rewindBounds, forwardBounds, loopBounds, tempoBounds, timeSigBounds;
    juce::Label tempoLabel, timeSigLabel;
};

//==============================================================================
// Small popup for metronome volume settings.
class MetronomeSettingsPopup : public juce::Component
{
public:
    MetronomeSettingsPopup (AudioEngineManager& ae) : audioEngine (ae)
    {
        addAndMakeVisible (slider);
        slider.setRange (AudioEngineManager::kMinVolumeDb, AudioEngineManager::kMaxVolumeDb, 0.1);
        slider.setValue (audioEngine.getMetronomeVolumeDb());
        slider.setTextValueSuffix (" dB");
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 20);
        slider.onValueChange = [this] {
            audioEngine.setMetronomeVolumeDb ((float) slider.getValue());
        };

        addAndMakeVisible (label);
        label.setText ("CLICK LEVEL", juce::dontSendNotification);
        label.setFont (juce::Font (10.0f).withStyle (juce::Font::bold));
        label.setColour (juce::Label::textColourId, Theme::textMuted);

        setSize (200, 60);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bgPanel);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10);
        label.setBounds (b.removeFromTop (20));
        slider.setBounds (b);
    }

private:
    AudioEngineManager& audioEngine;
    juce::Slider slider;
    juce::Label  label;
};
