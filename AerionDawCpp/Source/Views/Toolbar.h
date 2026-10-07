#pragma once

// Top toolbar.

#include "ViewShared.h"

//==============================================================================
// Top toolbar  -  tools on the left, view modes + snap on the right.
class DAWToolbar : public juce::Component,
                   public juce::TooltipClient
{
public:
    juce::String getTooltip() override
    {
        switch (computeToolbarHoverZone (hoverPos))
        {
            case 1:  return inspectorVisible ? "Hide Inspector" : "Show Inspector";
            case 2:  return "Select tool";
            case 3:  return "Razor / split tool";
            case 4:  return "Comp / takes tool";
            case 5:  return punchEnabled ? "Punch in/out (on)" : "Punch in/out (off)";
            case 6:  return pdcEnabled   ? "Plugin delay compensation (on)"
                                         : "Plugin delay compensation (off)";
            case 7:  return browserVisible ? "Hide Browser" : "Show Browser";
            case 8:  return metronomeEnabled ? "Metronome (on) - Right-click for settings"
                                             : "Metronome (off) - Right-click for settings";
            case 9:  return countInBars > 0
                         ? juce::String::formatted ("Count-in: %d bar%s", countInBars, countInBars == 1 ? "" : "s")
                         : juce::String ("Count-in: off");
            case 10: return (snapEnabled ? juce::String ("Snap: ") + getSnapIntervalText (snapInterval)
                                         : juce::String ("Snap: off"))
                          + " - Click to toggle, right-click for interval";
            case 11: return autoCrossfadeEnabled ? "Auto-crossfade (on)" : "Auto-crossfade (off)";
            case 12: return followPlayback ? "Follow playback (on): the view keeps the playhead in sight"
                                           : "Follow playback (off)";
            default: return {};
        }
    }

    std::function<void()> onToggleSnap;
    std::function<void(double)> onSnapIntervalChanged;
    std::function<void()> onToggleInspector;
    std::function<void()> onToggleBrowser;
    std::function<void(EditTool)> onToolChanged;
    std::function<void()> onToggleMetronome;
    std::function<void()> onShowMetronomeSettings;
    std::function<void(bool)> onPunchChanged;
    std::function<void(bool)> onPdcChanged;
    std::function<void(int)>  onCountInChanged;
    std::function<void()> onToggleAutoCrossfade;
    std::function<void()> onToggleFollowPlayback;

    bool followPlayback = false;
    bool snapEnabled = true;
    double snapInterval = 1.0;
    bool inspectorVisible = true;
    bool browserVisible = true;
    bool metronomeEnabled = false;
    bool punchEnabled  = false;
    bool pdcEnabled    = true;
    int  countInBars   = 0;
    EditTool activeTool = EditTool::select;
    bool autoCrossfadeEnabled = true;

    DAWToolbar()
    {
        auto load = &Icons::load;
        iconInspector = load (BinaryData::aerion_inspector_svg, BinaryData::aerion_inspector_svgSize);
        iconSelect    = load (BinaryData::aerion_select_svg,    BinaryData::aerion_select_svgSize);
        iconCut       = load (BinaryData::aerion_cut_svg,       BinaryData::aerion_cut_svgSize);
        iconComp      = load (BinaryData::aerion_comp_svg,      BinaryData::aerion_comp_svgSize);
        iconPunch     = load (BinaryData::aerion_punch_svg,     BinaryData::aerion_punch_svgSize);
        iconPdc       = load (BinaryData::aerion_pdc_svg,       BinaryData::aerion_pdc_svgSize);
        iconMagnet    = load (BinaryData::aerion_magnet_svg,    BinaryData::aerion_magnet_svgSize);
        iconMetronome = load (BinaryData::aerion_metronome_svg, BinaryData::aerion_metronome_svgSize);
        iconCountIn   = load (BinaryData::aerion_countin_svg,   BinaryData::aerion_countin_svgSize);
        iconBrowser   = load (BinaryData::aerion_browser_svg,   BinaryData::aerion_browser_svgSize);
        iconXfade     = load (BinaryData::aerion_xfade_svg,     BinaryData::aerion_xfade_svgSize);
        iconFollow    = load (BinaryData::aerion_follow_svg,    BinaryData::aerion_follow_svgSize);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        setTitle ("Toolbar");
        setFocusContainerType (juce::Component::FocusContainerType::focusContainer);
    }

    void resized() override
    {
        const int btnY = 6, btnS = 28, W = getWidth();

        inspectorBtn = { 8,       btnY, btnS, btnS };
        selectBounds = { 52,      btnY, btnS, btnS };
        razorBounds  = { 84,      btnY, btnS, btnS };
        compBounds   = { 116,     btnY, btnS, btnS };
        punchBtn     = { 160,     btnY, btnS, btnS };
        pdcBtn       = { 192,     btnY, btnS, btnS };
        followBtn    = { 236,     btnY, btnS, btnS };
        browserBtn   = { W - 36,  btnY, btnS, btnS };
        clickBtn     = { W - 86,  btnY, btnS, btnS };
        countInBtn   = { W - 118, btnY, btnS, btnS };
        snapBounds   = { W - 164, btnY, btnS, btnS };
        xfadeBounds  = { W - 196, btnY, btnS, btnS };

        auto toggle = [this] (const juce::String& id, const juce::String& title, juce::Rectangle<int> r,
                              std::function<bool()> isOn)
        {
            Accessibility::Control c;
            c.id = id;
            c.title = title;
            c.role = Accessibility::Role::toggle;
            c.bounds = r;
            c.isOn = std::move (isOn);
            c.press = [this, centre = r.getCentre()] { Accessibility::clickAt (*this, centre); };
            return c;
        };

        proxies.sync ({ toggle ("inspector", "Inspector",                   inspectorBtn, [this] { return inspectorVisible; }),
                        toggle ("select",    "Select tool",                 selectBounds, [this] { return activeTool == EditTool::select; }),
                        toggle ("razor",     "Razor tool",                  razorBounds,  [this] { return activeTool == EditTool::razor; }),
                        toggle ("comp",      "Comp tool",                   compBounds,   [this] { return activeTool == EditTool::comp; }),
                        toggle ("punch",     "Punch in and out",            punchBtn,     [this] { return punchEnabled; }),
                        toggle ("pdc",       "Plugin delay compensation",   pdcBtn,       [this] { return pdcEnabled; }),
                        toggle ("follow",    "Follow playback",             followBtn,    [this] { return followPlayback; }),
                        toggle ("xfade",     "Auto-crossfade",              xfadeBounds,  [this] { return autoCrossfadeEnabled; }),
                        toggle ("snap",      "Snap to grid",                snapBounds,   [this] { return snapEnabled; }),
                        toggle ("countin",   "Count-in",                    countInBtn,   [this] { return countInBars > 0; }),
                        toggle ("metronome", "Metronome",                   clickBtn,     [this] { return metronomeEnabled; }),
                        toggle ("browser",   "Browser",                     browserBtn,   [this] { return browserVisible; }) });
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        proxies.paintFocusRing (g);
    }

    const Accessibility::ProxyPool& getAccessibleControls() const noexcept { return proxies; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bgBase);
        g.setColour (Theme::border.withAlpha (0.6f));
        g.drawLine (0.0f, (float)(getHeight() - 1), (float)getWidth(), (float)(getHeight() - 1));

        const int btnY = 6, btnS = 28, h = getHeight();

        // -- Left side ---------------------------------------------------------
        // Group 1: Inspector toggle
        drawIconBtn (g, inspectorBtn, iconInspector.get(), inspectorVisible);

        drawDivider (g, 44, btnY, h - btnY);

        // Group 2: Edit tools
        drawIconBtn (g, selectBounds, iconSelect.get(), activeTool == EditTool::select);
        drawIconBtn (g, razorBounds,  iconCut.get(),    activeTool == EditTool::razor);
        drawIconBtn (g, compBounds,   iconComp.get(),   activeTool == EditTool::comp);

        drawDivider (g, 152, btnY, h - btnY);

        // Group 3: Recording setup
        drawIconBtn (g, punchBtn, iconPunch.get(), punchEnabled, Theme::recordRed);
        drawIconBtn (g, pdcBtn,   iconPdc.get(),   pdcEnabled);

        drawDivider (g, 228, btnY, h - btnY);

        // Group 3b: Follow playback
        drawIconBtn (g, followBtn, iconFollow.get(), followPlayback);

        // -- Right side --------------------------------------------------------
        const int W = getWidth();

        // Group 6: Browser toggle (far right)
        drawIconBtn (g, browserBtn, iconBrowser.get(), browserVisible);

        drawDivider (g, W - 50, btnY, h - btnY);

        // Group 5: Metronome + CountIn
        drawIconBtn (g, clickBtn,   iconMetronome.get(), metronomeEnabled);
        drawIconBtn (g, countInBtn, iconCountIn.get(),   countInBars > 0, Theme::active, true);

        const char* countLabels[] = { "OFF", "1", "2" };
        drawIconLabel (g, countInBtn, countLabels[countInBars], countInBars > 0);

        drawDivider (g, W - 132, btnY, h - btnY);

        // Group 4: Snap (magnet with the interval below it)
        drawIconBtn (g, snapBounds, iconMagnet.get(), snapEnabled, Theme::active, true);
        drawIconLabel (g, snapBounds, getSnapIntervalText (snapInterval), snapEnabled);

        // Group 4b: Auto-crossfade (left of Snap)
        drawIconBtn (g, xfadeBounds, iconXfade.get(), autoCrossfadeEnabled, Theme::active);
    }

    static juce::String getSnapIntervalText (double interval)
    {
        if (interval >= 4.0)   return "Bar";
        if (interval >= 2.0)   return "1/2";
        if (interval >= 1.0)   return "1/4";
        if (interval >= 0.5)   return "1/8";
        if (interval >= 0.25)  return "1/16";
        if (interval >= 0.125) return "1/32";
        return "1/64";
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverPos = e.getPosition();
        const int z = computeToolbarHoverZone (hoverPos);
        if (z != hoverZoneId) { hoverZoneId = z; repaint(); }
    }

    void mouseExit (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);
        hoverPos = { -1, -1 };
        if (hoverZoneId != -1) { hoverZoneId = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (followBtn.contains (e.getPosition()))
        {
            followPlayback = ! followPlayback;
            repaint();
            if (onToggleFollowPlayback) onToggleFollowPlayback();
            return;
        }

        if (xfadeBounds.contains (e.getPosition()))
        {
            autoCrossfadeEnabled = ! autoCrossfadeEnabled;
            repaint();
            if (onToggleAutoCrossfade) onToggleAutoCrossfade();
            return;
        }

        if (inspectorBtn.contains (e.getPosition())) {
            inspectorVisible = !inspectorVisible;
            repaint();
            if (onToggleInspector) onToggleInspector();
            return;
        }
        if (browserBtn.contains (e.getPosition())) {
            browserVisible = !browserVisible;
            repaint();
            if (onToggleBrowser) onToggleBrowser();
            return;
        }
        if (selectBounds.contains (e.getPosition())) {
            activeTool = EditTool::select;
            repaint();
            if (onToolChanged) onToolChanged (activeTool);
            return;
        }
        if (razorBounds.contains (e.getPosition())) {
            activeTool = EditTool::razor;
            repaint();
            if (onToolChanged) onToolChanged (activeTool);
            return;
        }
        if (compBounds.contains (e.getPosition())) {
            activeTool = EditTool::comp;
            repaint();
            if (onToolChanged) onToolChanged (activeTool);
            return;
        }
        if (clickBtn.contains (e.getPosition())) {
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
        if (punchBtn.contains (e.getPosition())) {
            punchEnabled = !punchEnabled;
            repaint();
            if (onPunchChanged) onPunchChanged (punchEnabled);
            return;
        }
        if (pdcBtn.contains (e.getPosition())) {
            pdcEnabled = !pdcEnabled;
            repaint();
            if (onPdcChanged) onPdcChanged (pdcEnabled);
            return;
        }
        if (countInBtn.contains (e.getPosition())) {
            countInBars = (countInBars + 1) % 3;
            repaint();
            if (onCountInChanged) onCountInChanged (countInBars);
            return;
        }
        if (snapBounds.contains (e.getPosition())) {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem (1, "Bar",  true, juce::approximatelyEqual (snapInterval, 4.0));
                m.addItem (2, "1/2",  true, juce::approximatelyEqual (snapInterval, 2.0));
                m.addItem (3, "1/4",  true, juce::approximatelyEqual (snapInterval, 1.0));
                m.addItem (4, "1/8",  true, juce::approximatelyEqual (snapInterval, 0.5));
                m.addItem (5, "1/16", true, juce::approximatelyEqual (snapInterval, 0.25));
                m.addItem (6, "1/32", true, juce::approximatelyEqual (snapInterval, 0.125));
                m.addItem (7, "1/64", true, juce::approximatelyEqual (snapInterval, 0.0625));

                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (snapBounds)),
                    [this] (int result) {
                        if (result == 0) return;
                        double intervals[] = { 0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
                        snapInterval = intervals[result];
                        if (onSnapIntervalChanged) onSnapIntervalChanged (snapInterval);
                        repaint();
                    });
            }
            else
            {
                snapEnabled = !snapEnabled;
                repaint();
                if (onToggleSnap) onToggleSnap();
            }
        }
    }

    juce::Rectangle<int> getClickBtnBounds() const { return clickBtn; }

private:
    // Icon drawables
    std::unique_ptr<juce::Drawable> iconInspector, iconSelect, iconCut, iconComp;
    std::unique_ptr<juce::Drawable> iconPunch, iconPdc;
    std::unique_ptr<juce::Drawable> iconMagnet, iconMetronome, iconCountIn, iconBrowser, iconXfade, iconFollow;

    // Hit-test rectangles (laid out in resized, read in mouseDown)
    juce::Rectangle<int> snapBounds, selectBounds, razorBounds, compBounds;
    juce::Rectangle<int> inspectorBtn, browserBtn, clickBtn, punchBtn, pdcBtn, countInBtn;
    juce::Rectangle<int> xfadeBounds, followBtn;

    Accessibility::ProxyPool proxies { *this };

    // Hover tracking (repaint only when the hovered control changes)
    juce::Point<int> hoverPos { -1, -1 };
    int hoverZoneId = -1;

    /** Which toolbar control is under p (same geometry as paint). 0 = none. */
    int computeToolbarHoverZone (juce::Point<int> p) const
    {
        const int btnY = 6, btnS = 28;
        const int W = getWidth();
        if (W < 200) return 0;

        if (juce::Rectangle<int> (8,   btnY, btnS, btnS).contains (p)) return 1;
        if (juce::Rectangle<int> (52,  btnY, btnS, btnS).contains (p)) return 2;
        if (juce::Rectangle<int> (84,  btnY, btnS, btnS).contains (p)) return 3;
        if (juce::Rectangle<int> (116, btnY, btnS, btnS).contains (p)) return 4;
        if (juce::Rectangle<int> (160, btnY, btnS, btnS).contains (p)) return 5;
        if (juce::Rectangle<int> (192, btnY, btnS, btnS).contains (p)) return 6;
        if (juce::Rectangle<int> (236, btnY, btnS, btnS).contains (p)) return 12;
        if (juce::Rectangle<int> (W - 36,  btnY, btnS, btnS).contains (p)) return 7;
        if (juce::Rectangle<int> (W - 86,  btnY, btnS, btnS).contains (p)) return 8;
        if (juce::Rectangle<int> (W - 118, btnY, btnS, btnS).contains (p)) return 9;
        if (juce::Rectangle<int> (W - 164, btnY, btnS, btnS).contains (p)) return 10;
        if (juce::Rectangle<int> (W - 196, btnY, btnS, btnS).contains (p)) return 11;
        return 0;
    }

    /** `labelBelow` leaves the bottom of the button free for a state label
        (count-in bars), so the icon sits above it instead of under it. */
    void drawIconBtn (juce::Graphics& g, juce::Rectangle<int> b,
                      juce::Drawable* icon, bool active,
                      juce::Colour activeColour = Theme::active,
                      bool labelBelow = false) const
    {
        bool hov = b.contains (hoverPos);
        auto bf = b.toFloat();
        g.setColour (active ? activeColour.withAlpha (0.2f)
                            : hov ? Theme::surface.brighter (0.08f) : Theme::surface);
        g.fillRoundedRectangle (bf, 4.0f);
        g.setColour (active ? activeColour
                            : hov ? Theme::border.brighter (0.3f) : Theme::border);
        g.drawRoundedRectangle (bf, 4.0f, 1.0f);

        if (icon != nullptr)
        {
            auto area = bf.reduced (4.0f);
            if (labelBelow)
                area = area.withTrimmedBottom (kIconLabelH - 2.0f);

            Icons::draw (g, *icon, area, Icons::inkFor (active, false, activeColour));
        }
    }

    // Height of the state label under a toolbar icon (count-in, snap interval).
    static constexpr float kIconLabelH = 9.0f;

    /** The state label inside the bottom of a button drawn with `labelBelow`,
        kept clear of the border. */
    static void drawIconLabel (juce::Graphics& g, juce::Rectangle<int> b,
                               const juce::String& text, bool active)
    {
        g.setColour (active ? Theme::active : Theme::textMuted.withAlpha (0.6f));
        g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
        g.drawText (text, b.getX() + 1, b.getBottom() - (int) kIconLabelH - 2,
                    b.getWidth() - 2, (int) kIconLabelH, juce::Justification::centred);
    }

    static void drawDivider (juce::Graphics& g, int x, int y, int h)
    {
        g.setColour (Theme::border);
        g.drawLine ((float)x, (float)(y + 4), (float)x, (float)(y + h - 4), 1.0f);
    }
};
