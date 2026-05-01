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

    DECLARE_ID (start);
    DECLARE_ID (width);
}

class ProjectData
{
public:
    ProjectData();
    ~ProjectData() = default;

    juce::ValueTree getProjectTree() const { return projectTree; }
    
    // Creates the initial mock data representing src/data.ts
    void createMockData();

private:
    juce::ValueTree projectTree { IDs::Project };
};
