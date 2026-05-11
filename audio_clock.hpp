#ifndef AUDIO_CLOCK_HPP
#define AUDIO_CLOCK_HPP

#include <chrono>
#include <thread>
#include <algorithm>

class AudioMasterClock {
public:
    void configure(int sampleRateHz, double fps, int leadMs)
    {
        sampleRate = std::max(1000, sampleRateHz);
        targetFps = (fps > 1.0) ? fps : 60.0;
        samplesPerFrame = static_cast<double>(sampleRate) / targetFps;
        leadSamples = static_cast<double>(std::max(0, leadMs)) * static_cast<double>(sampleRate) / 1000.0;
        reset();
    }

    void reset()
    {
        start = std::chrono::steady_clock::now();
        producedSamples = 0.0;
    }

    void onFrameProduced()
    {
        producedSamples += samplesPerFrame;
    }

    void throttle() const
    {
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = now - start;
        const double consumedSamples = elapsed.count() * static_cast<double>(sampleRate);
        const double maxProduced = consumedSamples + leadSamples;

        if (producedSamples <= maxProduced) {
            return;
        }

        const double sleepSamples = producedSamples - maxProduced;
        const double sleepSeconds = sleepSamples / static_cast<double>(sampleRate);
        if (sleepSeconds > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepSeconds));
        }
    }

private:
    int sampleRate = 48000;
    double targetFps = 60.0;
    double samplesPerFrame = 800.0;
    double leadSamples = 3840.0;
    mutable double producedSamples = 0.0;
    mutable std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

#endif
