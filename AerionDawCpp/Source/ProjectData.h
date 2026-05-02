#pragma once
#include <JuceHeader.h>

namespace IDs
{
    #define DECLARE_ID(name) const juce::Identifier name (#name);
    DECLARE_ID (Project);
    DECLARE_ID (Tracks);
    DECLARE_ID (Track);
    DECLARE_ID (AuxTracks);
    DECLARE_ID (AuxTrack);
    DECLARE_ID (Regions);
    DECLARE_ID (Region);
    DECLARE_ID (Inserts);
    DECLARE_ID (Insert);
    DECLARE_ID (Sends);
    DECLARE_ID (Send);

    DECLARE_ID (id);
    DECLARE_ID (name);
    DECLARE_ID (color);
    DECLARE_ID (type);
    DECLARE_ID (level);
    DECLARE_ID (pan);
    DECLARE_ID (mute);
    DECLARE_ID (solo);
    DECLARE_ID (armed);

    DECLARE_ID (start);
    DECLARE_ID (width);
}

class ProjectData
{
public:
    ProjectData();
    ~ProjectData() = default;

    juce::ValueTree getProjectTree() const { return projectTree; }
    
    juce::ValueTree getTrackTree (int id) const;
    juce::ValueTree getTrackTree (const juce::String& id) const;

    // Syncs the ProjectData tree with the live Tracktion Engine state
    void syncWithEngine (tracktion::Edit& edit);

    // Creates the initial mock data representing src/data.ts
    void createMockData();

private:
    juce::ValueTree projectTree { IDs::Project };
};
