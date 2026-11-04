#pragma once
#include <JuceHeader.h>

/** The automation lanes a track shows below it in the Timeline, saved with the
    project on the track's state:

        <TRACK ...>
          <AERIONAUTOMATION shown="1">
            <LANE param="volume"/>
            <LANE param="1042/cutoff" height="120"/>
          </AERIONAUTOMATION>

    `param` is an AudioEngineManager automation key. A lane whose parameter no
    longer exists (its plugin was removed) is kept, and shows that. Changes go
    past the undo manager, as track heights do: showing a lane is not an edit. */
namespace AutomationLanes
{
    static const juce::Identifier kTree   ("AERIONAUTOMATION");
    static const juce::Identifier kLane   ("LANE");
    static const juce::Identifier kShown  ("shown");
    static const juce::Identifier kParam  ("param");
    static const juce::Identifier kHeight ("height");

    static constexpr int kDefaultHeight = 80;
    static constexpr int kMinHeight     = 40;
    static constexpr int kMaxHeight     = 300;

    struct Lane
    {
        juce::String param;
        int height = kDefaultHeight;
    };

    inline juce::ValueTree getTree (const tracktion::Track& t)
    {
        return t.state.getChildWithName (kTree);
    }

    inline juce::ValueTree getOrCreateTree (tracktion::Track& t)
    {
        return t.state.getOrCreateChildWithName (kTree, nullptr);
    }

    inline std::vector<Lane> get (const tracktion::Track& t)
    {
        std::vector<Lane> lanes;
        for (auto lane : getTree (t))
            if (lane.hasType (kLane))
                lanes.push_back ({ lane[kParam].toString(),
                                   juce::jlimit (kMinHeight, kMaxHeight, (int) lane.getProperty (kHeight, kDefaultHeight)) });
        return lanes;
    }

    inline bool isShown (const tracktion::Track& t)
    {
        return (bool) getTree (t).getProperty (kShown, false);
    }

    /** The lanes drawn below the track: none while they are hidden. */
    inline std::vector<Lane> getVisible (const tracktion::Track& t)
    {
        return isShown (t) ? get (t) : std::vector<Lane>();
    }

    inline bool contains (const tracktion::Track& t, const juce::String& param)
    {
        for (auto& lane : get (t))
            if (lane.param == param)
                return true;
        return false;
    }

    inline void setShown (tracktion::Track& t, bool shown)
    {
        getOrCreateTree (t).setProperty (kShown, shown, nullptr);
    }

    inline juce::ValueTree laneAt (const tracktion::Track& t, int index)
    {
        int i = 0;
        for (auto lane : getTree (t))
            if (lane.hasType (kLane) && i++ == index)
                return lane;
        return {};
    }

    /** Adds a lane at `index` (-1: last) and shows the lanes. */
    inline void add (tracktion::Track& t, const juce::String& param, int index = -1)
    {
        auto tree = getOrCreateTree (t);
        juce::ValueTree lane (kLane);
        lane.setProperty (kParam, param, nullptr);

        const int count = (int) get (t).size();
        if (index < 0 || index >= count)
            tree.appendChild (lane, nullptr);
        else
            tree.addChild (lane, tree.indexOf (laneAt (t, index)), nullptr);

        tree.setProperty (kShown, true, nullptr);
    }

    /** Removes a lane; removing the last one hides the lanes. */
    inline void remove (tracktion::Track& t, int index)
    {
        auto tree = getTree (t);
        if (auto lane = laneAt (t, index); lane.isValid())
            tree.removeChild (lane, nullptr);
        if (get (t).empty())
            tree.setProperty (kShown, false, nullptr);
    }

    inline void setParam (tracktion::Track& t, int index, const juce::String& param)
    {
        if (auto lane = laneAt (t, index); lane.isValid())
            lane.setProperty (kParam, param, nullptr);
    }

    /** Sets a lane's saved height; the default is not stored. */
    inline void setHeight (tracktion::Track& t, int index, int height)
    {
        height = juce::jlimit (kMinHeight, kMaxHeight, height);
        if (auto lane = laneAt (t, index); lane.isValid())
        {
            if (height == kDefaultHeight) lane.removeProperty (kHeight, nullptr);
            else                          lane.setProperty (kHeight, height, nullptr);
        }
    }
}
