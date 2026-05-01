#include "ProjectData.h"

ProjectData::ProjectData()
{
    createMockData();
}

void ProjectData::createMockData()
{
    juce::ValueTree tracksTree (IDs::Tracks);
    juce::ValueTree auxTracksTree (IDs::AuxTracks);
    
    projectTree.addChild (tracksTree, -1, nullptr);
    projectTree.addChild (auxTracksTree, -1, nullptr);

    // Helper lambda to create a track
    auto createTrack = [&](int id, const juce::String& name, const juce::String& color, const juce::String& type, float level, const juce::String& pan)
    {
        juce::ValueTree track (IDs::Track);
        track.setProperty (IDs::id, id, nullptr);
        track.setProperty (IDs::name, name, nullptr);
        track.setProperty (IDs::color, color, nullptr);
        track.setProperty (IDs::type, type, nullptr);
        track.setProperty (IDs::level, level, nullptr);
        track.setProperty (IDs::pan, pan, nullptr);
        return track;
    };

    // Helper lambda to create a region
    auto createRegion = [&](float start, float width)
    {
        juce::ValueTree region (IDs::Region);
        region.setProperty (IDs::start, start, nullptr);
        region.setProperty (IDs::width, width, nullptr);
        return region;
    };

    auto createInsert = [&](const juce::String& name)
    {
        juce::ValueTree insert (IDs::Insert);
        insert.setProperty (IDs::name, name, nullptr);
        return insert;
    };

    auto createSend = [&](const juce::String& name, const juce::String& level)
    {
        juce::ValueTree send (IDs::Send);
        send.setProperty (IDs::name, name, nullptr);
        send.setProperty (IDs::level, level, nullptr);
        return send;
    };

    // --- TRACK 1: Lead Vocal ---
    auto t1 = createTrack (1, "Lead Vocal", "#e53935", "audio", 75.0f, "C");
    juce::ValueTree t1Regions (IDs::Regions);
    t1Regions.addChild (createRegion (10.0f, 250.0f), -1, nullptr);
    t1Regions.addChild (createRegion (280.0f, 120.0f), -1, nullptr);
    t1.addChild (t1Regions, -1, nullptr);
    
    juce::ValueTree t1Inserts (IDs::Inserts);
    t1Inserts.addChild (createInsert ("Pro-Q 3"), -1, nullptr);
    t1Inserts.addChild (createInsert ("CLA-76"), -1, nullptr);
    t1.addChild (t1Inserts, -1, nullptr);

    juce::ValueTree t1Sends (IDs::Sends);
    t1Sends.addChild (createSend ("Reverb Bus", "-12.4"), -1, nullptr);
    t1.addChild (t1Sends, -1, nullptr);
    tracksTree.addChild (t1, -1, nullptr);

    // --- TRACK 2: Backing ---
    auto t2 = createTrack (2, "Backing", "#FFaa44", "audio", 60.0f, "R 24");
    juce::ValueTree t2Regions (IDs::Regions);
    t2Regions.addChild (createRegion (280.0f, 120.0f), -1, nullptr);
    t2.addChild (t2Regions, -1, nullptr);
    
    juce::ValueTree t2Inserts (IDs::Inserts);
    t2Inserts.addChild (createInsert ("Pro-Q 3"), -1, nullptr);
    t2Inserts.addChild (createInsert ("DeEsser"), -1, nullptr);
    t2.addChild (t2Inserts, -1, nullptr);

    juce::ValueTree t2Sends (IDs::Sends);
    t2Sends.addChild (createSend ("Reverb Bus", "-14.2"), -1, nullptr);
    t2.addChild (t2Sends, -1, nullptr);
    tracksTree.addChild (t2, -1, nullptr);

    // --- TRACK 3: Sub Bass ---
    auto t3 = createTrack (3, "Sub Bass", "#00bcd4", "midi", 85.0f, "C");
    juce::ValueTree t3Regions (IDs::Regions);
    t3Regions.addChild (createRegion (0.0f, 400.0f), -1, nullptr);
    t3.addChild (t3Regions, -1, nullptr);

    juce::ValueTree t3Inserts (IDs::Inserts);
    t3Inserts.addChild (createInsert ("Pro-C 2"), -1, nullptr);
    t3.addChild (t3Inserts, -1, nullptr);

    juce::ValueTree t3Sends (IDs::Sends);
    t3Sends.addChild (createSend ("Drum Room", "-inf"), -1, nullptr);
    t3.addChild (t3Sends, -1, nullptr);
    tracksTree.addChild (t3, -1, nullptr);

    // --- TRACK 4: Drum Bus ---
    auto t4 = createTrack (4, "Drum Bus", "#44AAFF", "folder", 90.0f, "C");
    juce::ValueTree t4Regions (IDs::Regions);
    t4Regions.addChild (createRegion (0.0f, 600.0f), -1, nullptr);
    t4.addChild (t4Regions, -1, nullptr);

    juce::ValueTree t4Inserts (IDs::Inserts);
    t4Inserts.addChild (createInsert ("SSL Comp"), -1, nullptr);
    t4Inserts.addChild (createInsert ("Saturn 2"), -1, nullptr);
    t4.addChild (t4Inserts, -1, nullptr);
    tracksTree.addChild (t4, -1, nullptr);


    // --- AUX TRACKS ---
    auto createAuxTrack = [&](int id, const juce::String& name, const juce::String& color, float level, const juce::String& pan)
    {
        juce::ValueTree track (IDs::AuxTrack);
        track.setProperty (IDs::id, id, nullptr);
        track.setProperty (IDs::name, name, nullptr);
        track.setProperty (IDs::color, color, nullptr);
        track.setProperty (IDs::level, level, nullptr);
        track.setProperty (IDs::pan, pan, nullptr);
        return track;
    };

    // Aux 5: Reverb Bus
    auto a5 = createAuxTrack (5, "Reverb Bus", "#8844FF", 40.0f, "C");
    juce::ValueTree a5Inserts (IDs::Inserts);
    a5Inserts.addChild (createInsert ("ValhallaRoom"), -1, nullptr);
    a5.addChild (a5Inserts, -1, nullptr);
    auxTracksTree.addChild (a5, -1, nullptr);

    // Aux 6: Delay
    auto a6 = createAuxTrack (6, "Delay", "#8844FF", 30.0f, "C");
    juce::ValueTree a6Inserts (IDs::Inserts);
    a6Inserts.addChild (createInsert ("EchoBoy"), -1, nullptr);
    a6.addChild (a6Inserts, -1, nullptr);
    auxTracksTree.addChild (a6, -1, nullptr);
}
