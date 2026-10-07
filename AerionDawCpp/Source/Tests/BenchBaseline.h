#pragma once

// Regression baselines for AerionBench.
//
// Every timing the bench reports is also recorded here under a stable name
// ("software: timeline full repaint", "audio 128 samples, 1 thread: mean").
// At the end of a run:
//
//   --save-baseline=<file>   writes them to a JSON file
//   --compare=<file>         compares them with that file, lists what changed
//                            and returns exit code 3 if anything is more than
//                            20 % slower (and slower by more than the noise
//                            floor of its unit)
//
// Timings depend on the machine and build, so a baseline is only meaningful on
// the machine, build type and arguments it was saved with; the file records
// all three and --compare warns when they differ. Not for CI.

#include <JuceHeader.h>
#include <iostream>
#include <map>

namespace BenchBaseline
{
    // How much slower counts as a regression, and below what absolute change
    // a difference is noise whatever its percentage.
    inline constexpr double kSlowerBy = 0.20;

    inline double noiseFloor (const juce::String& unit)
    {
        if (unit == "ms") return 0.05;
        if (unit == "%")  return 0.5;    // share of an audio block
        return 0.0;
    }

    struct Metric
    {
        double value = 0.0;
        juce::String unit;
    };

    /** Recorded timings, in the order they were first reported. Lower is better. */
    inline std::vector<std::pair<juce::String, Metric>>& metrics()
    {
        static std::vector<std::pair<juce::String, Metric>> list;
        return list;
    }

    inline void record (const juce::String& name, double value, const juce::String& unit)
    {
        for (auto& [n, m] : metrics())
            if (n == name)
            {
                m = { value, unit };
                return;
            }

        metrics().push_back ({ name, { value, unit } });
    }

    /** The arguments that change what is measured: everything but the
        baseline options themselves. */
    inline juce::String configOf (const juce::StringArray& args)
    {
        juce::StringArray kept;
        for (auto& a : args)
            if (! a.startsWith ("--save-baseline") && ! a.startsWith ("--compare"))
                kept.add (a);
        return kept.joinIntoString (" ");
    }

    inline juce::String buildType()
    {
       #if JUCE_DEBUG
        return "Debug";
       #else
        return "Release";
       #endif
    }

    inline juce::String argValue (const juce::StringArray& args, juce::StringRef name)
    {
        for (auto& a : args)
            if (a.startsWith (name))
                return a.fromFirstOccurrenceOf ("=", false, false).unquoted();
        return {};
    }

    inline bool save (const juce::File& file, const juce::StringArray& args)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("machine", juce::SystemStats::getComputerName());
        root->setProperty ("cpu",     juce::SystemStats::getCpuModel());
        root->setProperty ("build",   buildType());
        root->setProperty ("args",    configOf (args));
        root->setProperty ("saved",   juce::Time::getCurrentTime().toISO8601 (true));

        auto* values = new juce::DynamicObject();
        for (auto& [name, m] : metrics())
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("value", m.value);
            entry->setProperty ("unit",  m.unit);
            values->setProperty (name, juce::var (entry));
        }
        root->setProperty ("metrics", juce::var (values));

        file.getParentDirectory().createDirectory();
        return file.replaceWithText (juce::JSON::toString (juce::var (root)));
    }

    /** Prints the comparison; returns the number of regressions, or -1 if
        the baseline could not be read. */
    inline int compare (const juce::File& file, const juce::StringArray& args)
    {
        const auto root = juce::JSON::parse (file);
        auto* values = root["metrics"].getDynamicObject();
        if (values == nullptr)
        {
            std::cout << "Could not read the baseline " << file.getFullPathName() << std::endl;
            return -1;
        }

        std::cout << std::endl << "[compare with baseline " << file.getFileName()
                  << ", saved " << root["saved"].toString() << "]" << std::endl;

        auto warnIfDifferent = [] (const char* what, const juce::String& then, const juce::String& now)
        {
            if (then != now)
                std::cout << "  warning: " << what << " differs (baseline: " << then << ", now: " << now << ")" << std::endl;
        };
        warnIfDifferent ("machine", root["machine"].toString(), juce::SystemStats::getComputerName());
        warnIfDifferent ("build",   root["build"].toString(),   buildType());
        warnIfDifferent ("args",    root["args"].toString(),    configOf (args));

        int regressions = 0, improvements = 0;

        for (auto& [name, m] : metrics())
        {
            const auto& base = values->getProperty (name);
            if (! base.isObject())
            {
                std::cout << "  " << name.paddedRight (' ', 46) << "new: " << juce::String (m.value, 3) << " " << m.unit << std::endl;
                continue;
            }

            const double before = (double) base["value"];
            const double change = before > 0.0 ? (m.value - before) / before : 0.0;
            const bool beyondNoise = std::abs (m.value - before) > noiseFloor (m.unit);
            const bool slower = beyondNoise && change > kSlowerBy;
            const bool faster = beyondNoise && change < -kSlowerBy;
            regressions  += slower ? 1 : 0;
            improvements += faster ? 1 : 0;

            std::cout << "  " << name.paddedRight (' ', 46)
                      << juce::String (before, 3).paddedLeft (' ', 9) << " -> " << juce::String (m.value, 3).paddedLeft (' ', 9)
                      << " " << m.unit.paddedRight (' ', 3)
                      << (change >= 0 ? " +" : " ") << juce::String (100.0 * change, 1) << " %"
                      << (slower ? "   SLOWER" : faster ? "   faster" : "") << std::endl;
        }

        for (auto& name : values->getProperties())
        {
            const auto key = name.name.toString();
            if (std::none_of (metrics().begin(), metrics().end(), [&] (auto& p) { return p.first == key; }))
                std::cout << "  " << key.paddedRight (' ', 46) << "not measured in this run" << std::endl;
        }

        std::cout << "  " << regressions << " slower, " << improvements << " faster (threshold "
                  << juce::String (100.0 * kSlowerBy, 0) << " %)" << std::endl;
        return regressions;
    }

    /** Handles --save-baseline and --compare after a run. Returns the exit
        code to use: `exitCode` unchanged, or 3 if --compare found a regression
        (and the run had not already failed). */
    inline int finish (const juce::StringArray& args, int exitCode)
    {
        if (auto path = argValue (args, "--save-baseline"); path.isNotEmpty())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
            std::cout << std::endl << (save (file, args) ? "Saved baseline " : "FAILED to save baseline ")
                      << file.getFullPathName() << " (" << metrics().size() << " timings)" << std::endl;
        }

        if (auto path = argValue (args, "--compare"); path.isNotEmpty())
        {
            const auto regressions = compare (juce::File::getCurrentWorkingDirectory().getChildFile (path), args);
            if (exitCode == 0 && regressions != 0)
                return 3;
        }

        return exitCode;
    }
}
