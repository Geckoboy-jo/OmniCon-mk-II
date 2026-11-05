#ifndef AUDIO_CLOCK_HPP
#define AUDIO_CLOCK_HPP

#include <algorithm>
#include <chrono>
#include <thread>

class AudioMasterClock {
public:
  void configure(int sampleRateHz, double fps, int leadMs) {
    sampleRate = std::max(1000, sampleRateHz);
    targetFps = (fps > 1.0) ? fps : 60.0;
    frameDuration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / targetFps));
    maxLead = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(static_cast<double>(std::max(0, leadMs)) /
                                      1000.0));
    reset();
  }

  void reset() {
    const auto now = std::chrono::steady_clock::now();
    nextFrameDeadline = now;
    initialized = false;
  }

  void onFrameProduced() {
    if (!initialized) {
      nextFrameDeadline = std::chrono::steady_clock::now() + frameDuration;
      initialized = true;
      return;
    }

    nextFrameDeadline += frameDuration;

    const auto now = std::chrono::steady_clock::now();
    if (nextFrameDeadline < now - maxLead) {
      nextFrameDeadline = now;
    }
  }

  void throttle() const {
    if (!initialized) {
      return;
    }

    auto now = std::chrono::steady_clock::now();
    if (nextFrameDeadline <= now) {
      return;
    }

    // Sleep most of the wait interval, then finish with short yields for
    // tighter pacing.
    auto remaining = nextFrameDeadline - now;
    if (remaining > std::chrono::milliseconds(1)) {
      std::this_thread::sleep_until(nextFrameDeadline -
                                    std::chrono::milliseconds(1));
    }

    while ((now = std::chrono::steady_clock::now()) < nextFrameDeadline) {
      std::this_thread::yield();
    }
  }

private:
  int sampleRate = 48000;
  double targetFps = 60.0;
  std::chrono::steady_clock::duration frameDuration =
      std::chrono::milliseconds(16);
  std::chrono::steady_clock::duration maxLead = std::chrono::milliseconds(80);
  mutable bool initialized = false;
  mutable std::chrono::steady_clock::time_point nextFrameDeadline =
      std::chrono::steady_clock::now();
};

#endif
