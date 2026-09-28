#pragma once
#include <JuceHeader.h>

//==============================================================================
// Which renderer Aerion's windows paint with.
//
// JUCE 8 creates every window on Windows with Direct2D. Measured with
// AerionBench (see Documentation/PERFORMANCE.md), Direct2D is faster for full
// repaints but costs far more than the software renderer for the small
// repaints that dominate interaction: a 16 px playhead strip took 4.1 ms vs
// 0.37 ms, a clip-drag repaint 1.9 ms vs 0.08 ms. On machines without a GPU
// driver Direct2D also falls back to WARP, a software rasteriser behind the
// Direct2D API. So Auto uses the software renderer, except on very large
// displays where full repaints (window resizes, scrolling) cost more pixels
// than the CPU renderer handles well.
//
// JUCE has no app-wide setting, so the Policy applies the choice to every
// window: at startup, when the setting changes, and whenever focus moves to a
// newly opened window (dialogs, plugin editors, the Piano Roll).
//==============================================================================

namespace GraphicsEngine
{
    enum class Choice { automatic = 0, hardware = 1, software = 2 };

    inline constexpr const char* settingsKey = "graphicsEngine";

    // Above this many physical pixels on the largest display, Auto prefers the
    // hardware renderer. Software full-repaint cost grows with pixel count
    // (Timeline at 1080p: 31 ms software vs 19 ms Direct2D), while the small
    // partial repaints where software wins do not. Up to 2560 x 1600 the
    // partial repaints dominate interaction; at 4K the full ones start to.
    // A heuristic: revisit once Timeline layers make full repaints cheap.
    inline constexpr double kHardwareAutoPixels = 2560.0 * 1600.0;

    inline Choice choiceFromInt (int v)
    {
        return v == 1 ? Choice::hardware : v == 2 ? Choice::software : Choice::automatic;
    }

    inline double largestDisplayPixels()
    {
        double largest = 0.0;
        for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
            largest = juce::jmax (largest, (double) d.totalArea.getWidth()  * d.scale
                                         * (double) d.totalArea.getHeight() * d.scale);
        return largest;
    }

    /** True when `choice` resolves to the software renderer on this machine. */
    inline bool wantsSoftware (Choice choice)
    {
        switch (choice)
        {
            case Choice::software:  return true;
            case Choice::hardware:  return false;
            case Choice::automatic: break;
        }

        return largestDisplayPixels() <= kHardwareAutoPixels;
    }

    /** Index of the engine `peer` should use, or -1 if it offers no match. */
    inline int engineIndexFor (juce::ComponentPeer& peer, bool software)
    {
        const auto engines = peer.getAvailableRenderingEngines();
        for (int i = 0; i < engines.size(); ++i)
            if (engines[i].containsIgnoreCase ("software") == software)
                return i;
        return -1;
    }

    inline void applyToAllWindows (Choice choice)
    {
        const bool software = wantsSoftware (choice);

        for (int i = juce::ComponentPeer::getNumPeers(); --i >= 0;)
        {
            if (auto* peer = juce::ComponentPeer::getPeer (i))
            {
                const int index = engineIndexFor (*peer, software);
                if (index >= 0 && peer->getCurrentRenderingEngine() != index)
                {
                    peer->setCurrentRenderingEngine (index);
                    peer->getComponent().repaint();
                }
            }
        }
    }

    /** For menus and logs: e.g. "Software Renderer" or "Direct2D". */
    inline juce::String resolvedEngineName (Choice choice)
    {
        const bool software = wantsSoftware (choice);
        if (auto* peer = juce::ComponentPeer::getPeer (0))
        {
            const int index = engineIndexFor (*peer, software);
            if (index >= 0)
                return peer->getAvailableRenderingEngines()[index];
        }
        return software ? "Software Renderer" : "Hardware";
    }

    /** Keeps every window on the chosen engine for the lifetime of the app. */
    class Policy : private juce::FocusChangeListener
    {
    public:
        Policy()  { juce::Desktop::getInstance().addFocusChangeListener (this); }
        ~Policy() override { juce::Desktop::getInstance().removeFocusChangeListener (this); }

        void setChoice (Choice c)
        {
            choice = c;
            apply();
            juce::Logger::writeToLog ("Graphics engine: "
                                      + juce::String (c == Choice::automatic ? "Auto" : c == Choice::hardware ? "Hardware" : "Software")
                                      + " -> " + resolvedEngineName (c));
        }

        Choice getChoice() const noexcept { return choice; }

        void apply() const { applyToAllWindows (choice); }

    private:
        // A window opening (dialog, plugin editor, Piano Roll) takes focus, so
        // this catches new windows without polling.
        void globalFocusChanged (juce::Component*) override { apply(); }

        Choice choice = Choice::automatic;

        JUCE_DECLARE_NON_COPYABLE (Policy)
    };
}
