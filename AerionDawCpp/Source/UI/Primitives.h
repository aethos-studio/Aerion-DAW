#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "../AudioEngine.h"
#include "ThemeTokens.h"

inline void setFaderFromY (AudioEngineManager& audioEngine, tracktion::Track* t, juce::Rectangle<int> area, int y)
{
    if (! t) return;
    // Must match paintFader: same travel maps the same dB <-> pixel Y.
    int faderTop = area.getY() + 4;
    int faderH   = area.getHeight() - 24;
    float sPos = 1.0f - juce::jlimit (0.0f, 1.0f, (float) (y - faderTop) / (float) juce::jmax (1, faderH));
    audioEngine.setTrackVolumeDb (t, AudioEngineManager::getDbFromFaderPos (sPos));
}

inline void paintFader (juce::Graphics& g, juce::Rectangle<int> area,
                        AudioEngineManager& audioEngine,
                        tracktion::Track* track, juce::Colour tColor, bool isMaster,
                        juce::Drawable* faderKnobDrawable,
                        juce::Rectangle<int>* readoutAreaOut = nullptr,
                        bool masterK14ReferenceScale = false)
{
    juce::ignoreUnused (tColor);
    if (area.getHeight() < 30) return;

    int faderTop = area.getY() + 4;
    int faderH   = area.getHeight() - 24;

    // Meter scale: compress the -inf..0 dB range so 0 dB sits closer to the top,
    // while keeping the fader knob mapping unchanged.
    auto meterPosFromDb = [] (float db) -> float
    {
        constexpr float kMin = AudioEngineManager::kMinVolumeDb; // -60
        constexpr float kMax = 6.0f;
        db = juce::jlimit (kMin, kMax, db);

        // -60..0 occupies 88% of the height; 0..+6 occupies the top 12%.
        if (db <= 0.0f)
        {
            float t = (db - kMin) / (0.0f - kMin);   // 0..1
            return juce::jlimit (0.0f, 1.0f, t * 0.88f);
        }

        float t = db / kMax;                         // 0..1
        return juce::jlimit (0.0f, 1.0f, 0.88f + t * 0.12f);
    };

    int cx      = area.getCentreX();
    int track_w = 6;
    int track_x = cx - track_w / 2;

    int meter_w   = 8;
    int meter_gap = 4;
    int meter_lx  = track_x - meter_gap - meter_w;
    int meter_rx  = track_x + track_w + meter_gap;

    // Scale tick marks to the left of the left meter
    struct Tick { float db; const char* label; };
    static const Tick kTicks[] = {
        { -60.0f, "-inf" }, { -30.0f, "-30" }, { -18.0f, "-18" },
        { -12.0f, "-12"  }, {  -6.0f, "-6"  }, {   0.0f, "0"   }, { 6.0f, "+6" }
    };
    int tick_x2 = meter_lx - 1;
    int tick_x1 = tick_x2 - 5;
    auto faderYForLinearPos = [faderTop, faderH] (float pos01) -> int
    {
        return faderTop + (int) std::lround ((float) faderH * (1.0f - pos01));
    };
    g.setFont (juce::Font (7.0f));
    for (auto& tick : kTicks)
    {
        // Tick labels should reflect the fader scale, not the compressed meter scale.
        float fPos = AudioEngineManager::getFaderPosFromDb (tick.db);
        int   tY   = faderYForLinearPos (fPos);
        bool  is0  = juce::approximatelyEqual (tick.db, 0.0f);
        g.setColour (is0 ? Theme::border.brighter (0.3f) : Theme::border.withAlpha (0.5f));
        g.drawHorizontalLine (tY, (float) tick_x1, (float) tick_x2);
        g.setColour (Theme::textMuted.withAlpha (0.6f));
        g.drawText (tick.label,
                    juce::Rectangle<int> (area.getX(), tY - 4, tick_x1 - area.getX(), 9),
                    juce::Justification::centredRight, false);
    }

    // Fader rail
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle ((float) track_x, (float) faderTop, (float) track_w, (float) faderH, 3.0f);

    // 0dB tick on fader rail
    {
        float zeroPos = AudioEngineManager::getFaderPosFromDb (0.0f);
        int   zeroY   = faderYForLinearPos (zeroPos);
        g.setColour (Theme::border.withAlpha (0.5f));
        g.drawHorizontalLine (zeroY, (float) (track_x - 2), (float) (track_x + track_w + 2));
    }

    // Dual meters (same peak for L and R  -  no separate L/R API)
    float peak = audioEngine.getTrackPeak (track);
    float pPos = meterPosFromDb (peak);
    bool  clip = peak > 0.0f;
    float maxPeak = audioEngine.getTrackMaxPeak (track);
    bool  clipping = maxPeak > 0.0f;

    const float zeroMeterPos = meterPosFromDb (0.0f);
    const int   zeroMeterY   = faderTop + (int) std::round (faderH * (1.0f - zeroMeterPos));
    const float maxPeakPos   = meterPosFromDb (maxPeak);
    const int   maxPeakY     = faderTop + (int) std::round (faderH * (1.0f - maxPeakPos));

    auto drawMeter = [&] (int mx)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle ((float) mx, (float) faderTop, (float) meter_w, (float) faderH, 2.0f);
        g.setColour (Theme::border.withAlpha (0.4f));
        g.drawRoundedRectangle ((float) mx, (float) faderTop, (float) meter_w, (float) faderH, 2.0f, 1.0f);

        if (clip)
        {
            g.setColour (Theme::recordRed);
            g.fillRoundedRectangle ((float) mx, (float) faderTop, (float) meter_w, 5.0f, 1.5f);
        }

        if (pPos > 0.0f)
        {
            int mY = faderTop + (int) (faderH * (1.0f - pPos));
            juce::ColourGradient mg (Theme::meterRed,   0, (float) faderTop,
                                     Theme::meterGreen, 0, (float) (faderTop + faderH), false);
            mg.addColour (0.17, Theme::meterYellow);
            mg.addColour (0.33, Theme::meterGreen);
            g.setGradientFill (mg);
            g.fillRoundedRectangle ((float) mx, (float) mY, (float) meter_w,
                                    (float) (faderTop + faderH - mY), 2.0f);
        }

        // 0 dB reference line (subtle) to make the top-end scale clearer.
        g.setColour (Theme::border.brighter (0.35f).withAlpha (0.55f));
        g.drawHorizontalLine (zeroMeterY, (float) mx + 1.0f, (float) (mx + meter_w - 1));

        // Optional K-14 staging overlay on master: reference at -14 dBFS + softer ticks.
        if (isMaster && masterK14ReferenceScale)
        {
            static constexpr float kK14[] = { -20.0f, -17.0f, -14.0f, -11.0f, -8.0f };
            for (float db : kK14)
            {
                const int y = faderTop + (int) std::round (faderH * (1.0f - meterPosFromDb (db)));
                const bool ref = juce::approximatelyEqual (db, -14.0f);
                g.setColour ((ref ? Theme::textMain : Theme::textMuted).withAlpha (ref ? 0.85f : 0.35f));
                g.drawHorizontalLine (y, (float) mx + 0.5f, (float) (mx + meter_w - 0.5f));
            }
        }

        // Peak-hold marker line (cheap, but very readable).
        if (maxPeak > -90.0f)
        {
            g.setColour ((clipping ? Theme::recordRed : Theme::textMain).withAlpha (0.80f));
            g.drawHorizontalLine (maxPeakY, (float) mx + 1.0f, (float) (mx + meter_w - 1));
        }
    };

    drawMeter (meter_lx);
    drawMeter (meter_rx);

    // Fader cap: pin aerion_fader.svg (viewBox 18x48) so the white indicator (centre y=24)
    // lands exactly on the same Y as scale ticks (drawWithin/centred can drift vs tight SVG bounds).
    float db   = audioEngine.getTrackVolumeDb (track);
    float sPos = AudioEngineManager::getFaderPosFromDb (db);
    const int   capY  = faderYForLinearPos (sPos);
    const float capYf = (float) capY;
    if (faderKnobDrawable != nullptr)
    {
        constexpr float kSvgW = 18.0f;
        constexpr float kSvgH = 48.0f;
        constexpr float kIndicatorX = kSvgW * 0.5f;
        constexpr float kIndicatorY = 24.0f; // centre of #fff line (y 23.25 + h 1.5 in aerion_fader.svg)
        constexpr float kDrawH = 36.0f;
        const float     scale  = kDrawH / kSvgH;
        // Uniform scale about origin then translate so SVG indicator (kIndicatorX, kIndicatorY) maps to (cx, capYf).
        // Do not use translation().scaled() — JUCE scales mat02/mat12 as well.
        const auto xf = juce::AffineTransform (scale, 0.f, (float) cx - kIndicatorX * scale,
                                               0.f, scale, capYf - kIndicatorY * scale);
        faderKnobDrawable->draw (g, 1.0f, xf);
    }

    // Peak-hold dB readout
    g.setColour (clipping ? Theme::recordRed : Theme::textMuted);
    g.setFont (juce::Font (9.0f).withStyle (juce::Font::bold));
    juce::String dbText = (maxPeak > -90.0f)
        ? juce::String::formatted ("%+.1f dB", maxPeak)
        : juce::String::formatted ("%+.1f dB", db);

    auto readoutArea = area.withY (area.getBottom() - 16).withHeight (14);
    g.drawText (dbText, readoutArea, juce::Justification::centred);
    if (readoutAreaOut) *readoutAreaOut = readoutArea;
}

