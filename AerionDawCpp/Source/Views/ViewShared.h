#pragma once

// Includes, the edit tool enum, the plugin picker and the insert-row helpers
// shared by the views in this folder.

#include <JuceHeader.h>
#include "../ProjectData.h"
#include "../AudioEngine.h"
#include "../GoogleDriveClient.h"
#include "../UI/ThemeTokens.h"
#include "../UI/Primitives.h"
#include "../UI/Icons.h"
#include "../UI/CachedLayer.h"
#include "../UI/ClipFrame.h"
#include "../UI/LookAndFeel.h"
#include "../UI/Accessibility.h"
#include "../UI/ThemeTypefaces.h"
#include "../UI/Profiling.h"
#include <limits>

enum class EditTool { select, razor, comp };

//==============================================================================
// (Fader primitives + LookAndFeel extracted to UI/)

//==============================================================================
// Shared plugin picker  -  shows a manufacturer-grouped popup of every
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
// Shared pill button drawing helper (used by Inspector, PianoRollEditor, etc.)
inline void drawPill (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label,
                     bool on, juce::Colour activeColour)
{
    g.setColour (on ? activeColour.withAlpha (0.8f) : Theme::surface);
    g.fillRoundedRectangle (r.toFloat(), 3.0f);
    g.setColour (on ? activeColour : Theme::border);
    g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);
    g.setColour (on ? juce::Colours::black : Theme::textMuted);
    g.setFont (Theme::uiSize (9.0f).withStyle (juce::Font::bold));
    g.drawText (label, r, juce::Justification::centred);
}

struct InsertRowHitAreas
{
    juce::Rectangle<int> row, dragHandle, bypassBtn, labelArea;
    tracktion::ExternalPlugin* plugin = nullptr;
};

struct InsertRowDragState
{
    int draggingExternalIndex = -1;
    int dropBeforeExternalIndex = -1;
    int dropPreviewY = -1;
};

inline bool isInsertTrackFrozen (AudioEngineManager& audioEngine, tracktion::Track* track)
{
    if (auto* at = dynamic_cast<tracktion::AudioTrack*> (track))
        return audioEngine.isTrackFrozen (at) || audioEngine.isTrackFreezing (at);
    return false;
}

inline bool isClipTrackFrozenOrFreezing (AudioEngineManager& audioEngine, tracktion::Clip* clip)
{
    if (clip == nullptr)
        return false;

    if (auto* at = dynamic_cast<tracktion::AudioTrack*> (clip->getTrack()))
        return audioEngine.isTrackFrozen (at) || audioEngine.isTrackFreezing (at);

    return false;
}

inline void showFrozenTrackInsertAlert()
{
    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                            "Track Unavailable",
                                            "Cannot modify plugins on frozen tracks or tracks that are currently freezing.");
}

inline InsertRowHitAreas paintInsertRow (juce::Graphics& g, juce::Rectangle<int> row,
                                         tracktion::ExternalPlugin& plug, AudioEngineManager& audioEngine,
                                         float alpha = 1.0f)
{
    InsertRowHitAreas hit;
    hit.row    = row;
    hit.plugin = &plug;

    const bool bypassed = audioEngine.isExternalPluginBypassed (&plug);
    Theme::drawRoundedPanel (g, row.toFloat(), Theme::bgBase, alpha * (bypassed ? 0.45f : 1.0f));

    hit.dragHandle = row.removeFromLeft (16).reduced (2, 6);
    g.setColour (Theme::textMuted.withMultipliedAlpha (alpha));
    for (int i = 0; i < 3; ++i)
    {
        const float dotY = (float) hit.dragHandle.getY() + 4.0f + (float) i * 5.0f;
        g.fillEllipse ((float) hit.dragHandle.getX() + 4.0f, dotY, 2.0f, 2.0f);
        g.fillEllipse ((float) hit.dragHandle.getRight() - 6.0f, dotY, 2.0f, 2.0f);
    }

    hit.bypassBtn = row.removeFromRight (34).reduced (4, 5);
    drawPill (g, hit.bypassBtn, "BYP", bypassed, Theme::meterYellow);

    hit.labelArea = row.reduced (4, 0);
    g.setColour ((bypassed ? Theme::textMuted : Theme::textMain).withMultipliedAlpha (alpha));
    g.setFont (Theme::uiSize (11.0f));
    g.drawText (plug.getName(), hit.labelArea, juce::Justification::centredLeft, true);

    return hit;
}

inline InsertRowHitAreas paintCompactInsertSlot (juce::Graphics& g, juce::Rectangle<int> row,
                                                 tracktion::ExternalPlugin& plug,
                                                 AudioEngineManager& audioEngine)
{
    InsertRowHitAreas hit;
    hit.row    = row;
    hit.plugin = &plug;

    const bool bypassed = audioEngine.isExternalPluginBypassed (&plug);
    Theme::drawRoundedPanel (g, row.toFloat(), Theme::bgBase, bypassed ? 0.45f : 1.0f);

    hit.bypassBtn = row.removeFromLeft (8).reduced (1, 3);
    g.setColour (bypassed ? Theme::meterYellow : Theme::textMuted.withAlpha (0.55f));
    g.fillEllipse (hit.bypassBtn.toFloat().reduced (1.5f));

    hit.dragHandle = row.removeFromLeft (6).reduced (0, 3);
    g.setColour (Theme::textMuted.withAlpha (0.7f));
    for (int i = 0; i < 3; ++i)
    {
        const float dotY = (float) hit.dragHandle.getY() + 2.0f + (float) i * 3.5f;
        g.fillEllipse ((float) hit.dragHandle.getCentreX() - 1.0f, dotY, 2.0f, 2.0f);
    }

    hit.labelArea = row.reduced (1, 2);

    g.setColour (bypassed ? Theme::textMuted : Theme::textMain);
    g.setFont (Theme::uiSize (8.5f).withStyle (juce::Font::bold));
    g.drawText (plug.getName().substring (0, 1).toUpperCase(), hit.labelArea, juce::Justification::centred);

    return hit;
}

inline void paintInsertDropLine (juce::Graphics& g, int y, int x, int width)
{
    g.setColour (Theme::accent.withAlpha (0.95f));
    g.fillRect ((float) x, (float) y, (float) width, 2.0f);
}

inline int insertDropBeforeIndexFromY (const juce::Array<InsertRowHitAreas>& hits, int y)
{
    for (int i = 0; i < hits.size(); ++i)
    {
        if (y < hits[i].row.getCentreY())
            return i;
    }
    return hits.size();
}

inline int insertDropPreviewYFromIndex (const juce::Array<InsertRowHitAreas>& hits, int dropBeforeIndex)
{
    if (hits.isEmpty())
        return -1;

    if (dropBeforeIndex <= 0)
        return hits.getFirst().row.getY() - 1;

    if (dropBeforeIndex >= hits.size())
        return hits.getLast().row.getBottom() + 1;

    return hits[dropBeforeIndex].row.getY() - 1;
}

inline void showInsertContextMenu (AudioEngineManager& audioEngine, tracktion::Track* track,
                                   tracktion::ExternalPlugin* plugin, juce::Point<int> screenPos,
                                   std::function<void()> onChanged)
{
    if (plugin == nullptr || track == nullptr)
        return;

    juce::PopupMenu m;
    m.addItem (1, "Open Editor");
    m.addItem (2, "Bypass", true, ! plugin->isEnabled());
    m.addSeparator();
    m.addItem (4, "Move Up", plugin != nullptr);
    m.addItem (5, "Move Down", plugin != nullptr);
    m.addSeparator();
    m.addItem (3, "Remove Plugin");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [&audioEngine, track, plugin, onChanged] (int chosen)
                     {
                         if (chosen <= 0 || plugin == nullptr)
                             return;

                         if (isInsertTrackFrozen (audioEngine, track))
                         {
                             showFrozenTrackInsertAlert();
                             return;
                         }

                         juce::Array<tracktion::ExternalPlugin*> externals;
                         for (auto* p : track->pluginList)
                             if (auto* e = dynamic_cast<tracktion::ExternalPlugin*> (p))
                                 externals.add (e);

                         const int idx = externals.indexOf (plugin);

                         if (chosen == 1)
                             plugin->showWindowExplicitly();
                         else if (chosen == 2)
                             audioEngine.setPluginBypassed (plugin, plugin->isEnabled());
                         else if (chosen == 3)
                             audioEngine.removePlugin (plugin);
                         else if (chosen == 4 && idx > 0)
                             audioEngine.moveExternalPlugin (track, plugin, idx - 1);
                         else if (chosen == 5 && idx >= 0 && idx < externals.size() - 1)
                             audioEngine.moveExternalPlugin (track, plugin, idx + 1);

                         if (onChanged)
                             onChanged();
                     });
}

inline bool handleInsertRowMouseDown (const juce::MouseEvent& e,
                                      const juce::Array<InsertRowHitAreas>& hits,
                                      AudioEngineManager& audioEngine,
                                      tracktion::Track* track,
                                      InsertRowDragState& dragState,
                                      std::function<void()> onChanged)
{
    if (track == nullptr)
        return false;

    if (e.mods.isPopupMenu())
    {
        for (auto& hit : hits)
        {
            if (hit.row.contains (e.getPosition()))
            {
                showInsertContextMenu (audioEngine, track, hit.plugin, e.getScreenPosition(), onChanged);
                return true;
            }
        }
        return false;
    }

    for (int i = 0; i < hits.size(); ++i)
    {
        auto& hit = hits.getReference (i);
        if (! hit.row.contains (e.getPosition()))
            continue;

        if (hit.bypassBtn.contains (e.getPosition()))
        {
            if (isInsertTrackFrozen (audioEngine, track))
            {
                showFrozenTrackInsertAlert();
                return true;
            }

            audioEngine.setPluginBypassed (hit.plugin, hit.plugin->isEnabled());
            if (onChanged) onChanged();
            return true;
        }

        if (hit.dragHandle.contains (e.getPosition()))
        {
            if (isInsertTrackFrozen (audioEngine, track))
            {
                showFrozenTrackInsertAlert();
                return true;
            }

            dragState.draggingExternalIndex = i;
            dragState.dropBeforeExternalIndex = i;
            dragState.dropPreviewY = insertDropPreviewYFromIndex (hits, i);
            return true;
        }

        if (hit.labelArea.contains (e.getPosition()) && hit.plugin != nullptr)
        {
            hit.plugin->showWindowExplicitly();
            return true;
        }

        return true;
    }

    return false;
}

inline bool handleInsertRowMouseDrag (const juce::MouseEvent& e,
                                      const juce::Array<InsertRowHitAreas>& hits,
                                      InsertRowDragState& dragState,
                                      std::function<void()> onChanged)
{
    if (dragState.draggingExternalIndex < 0)
        return false;

    dragState.dropBeforeExternalIndex = insertDropBeforeIndexFromY (hits, e.y);
    dragState.dropPreviewY = insertDropPreviewYFromIndex (hits, dragState.dropBeforeExternalIndex);

    if (onChanged)
        onChanged();

    return true;
}

inline bool handleInsertRowMouseUp (AudioEngineManager& audioEngine,
                                    tracktion::Track* track,
                                    const juce::Array<InsertRowHitAreas>& hits,
                                    InsertRowDragState& dragState,
                                    std::function<void()> onChanged)
{
    if (dragState.draggingExternalIndex < 0 || track == nullptr)
        return false;

    const int from = dragState.draggingExternalIndex;
    int to = dragState.dropBeforeExternalIndex;
    if (to > from) --to;

    dragState.draggingExternalIndex = -1;
    dragState.dropBeforeExternalIndex = -1;
    dragState.dropPreviewY = -1;

    if (to != from && to >= 0 && to < hits.size() && hits[from].plugin != nullptr)
        audioEngine.moveExternalPlugin (track, hits[from].plugin, to);

    if (onChanged)
        onChanged();

    return true;
}
