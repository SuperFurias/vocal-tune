#include "Recorder.h"

#include <algorithm>
#include <chrono>

Recorder::Recorder() = default;

Recorder::~Recorder()
{
    stop();
}

juce::String Recorder::start (double newSampleRate, const juce::File& directory)
{
    stop();

    if (! directory.exists())
        directory.createDirectory();

    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    const auto timestamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    auto file = directory.getChildFile ("vocal-tune-" + timestamp + ".wav");
    int suffix = 1;
    while (file.existsAsFile())
        file = directory.getChildFile ("vocal-tune-" + timestamp + "-" + juce::String (suffix++) + ".wav");

    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (! fileStream->openedOk())
        return {};

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

    juce::WavAudioFormat format;
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate (sampleRate)
                       .withNumChannels (1)
                       .withBitsPerSample (24);

    writer = format.createWriterFor (stream, options);
    if (writer == nullptr)
        return {};

    {
        const juce::ScopedLock sl (fileLock);
        lastFilePath = file.getFullPathName();
    }

    writeIndex = readIndex = samplesUsed = 0;
    samplesWritten.store (0, std::memory_order_relaxed);
    stopRequested.store (false, std::memory_order_relaxed);
    recording.store (true, std::memory_order_release);

    worker = std::thread ([this] { threadLoop(); });

    return file.getFullPathName();
}

void Recorder::stop()
{
    recording.store (false, std::memory_order_release);

    if (worker.joinable())
    {
        stopRequested.store (false, std::memory_order_release);
        worker.join();
    }

    writer.reset();
}

void Recorder::writeSamples (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0 || ! recording.load (std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock (ringLock);
    if (! lock.isLocked())
        return;

    auto* destination = ring.getWritePointer (0, writeIndex);
    const int first = std::min (numSamples, ringSize - writeIndex);
    std::copy (samples, samples + first, destination);
    if (first < numSamples)
        std::copy (samples + first, samples + numSamples, ring.getWritePointer (0, 0));

    writeIndex = (writeIndex + numSamples) % ringSize;
    samplesUsed = std::min (ringSize, samplesUsed + numSamples);
}

double Recorder::getDurationSeconds() const
{
    return sampleRate > 0.0 ? (double) samplesWritten.load (std::memory_order_relaxed) / sampleRate : 0.0;
}

juce::String Recorder::getLastFilePath() const
{
    const juce::ScopedLock sl (fileLock);
    return lastFilePath;
}

void Recorder::threadLoop()
{
    constexpr int chunkSize = 4096;
    juce::AudioBuffer<float> chunk (1, chunkSize);

    while (! stopRequested.load (std::memory_order_acquire))
    {
        int available = 0;
        {
            const juce::SpinLock::ScopedLockType lock (ringLock);
            available = samplesUsed;
        }

        if (available <= 0)
        {
            if (! recording.load (std::memory_order_acquire))
                break;

            std::this_thread::sleep_for (std::chrono::milliseconds (4));
            continue;
        }

        int toRead = std::min (available, chunkSize);
        {
            const juce::SpinLock::ScopedLockType lock (ringLock);
            toRead = std::min (toRead, samplesUsed);
            const int first = std::min (toRead, ringSize - readIndex);
            std::copy (ring.getReadPointer (0, readIndex),
                       ring.getReadPointer (0, readIndex + first),
                       chunk.getWritePointer (0));
            if (first < toRead)
                std::copy (ring.getReadPointer (0, 0),
                           ring.getReadPointer (0, toRead - first),
                           chunk.getWritePointer (0, first));

            readIndex = (readIndex + toRead) % ringSize;
            samplesUsed -= toRead;
        }

        if (toRead <= 0)
            continue;

        if (writer != nullptr)
        {
            const float* data = chunk.getReadPointer (0);
            writer->writeFromFloatArrays (&data, 1, toRead);
            samplesWritten.fetch_add ((uint64_t) toRead, std::memory_order_relaxed);
        }
    }

    // Drain anything still buffered so stopping never loses the tail.
    int guard = 0;
    while (guard++ < 200)
    {
        int available = 0;
        {
            const juce::SpinLock::ScopedLockType lock (ringLock);
            available = samplesUsed;
        }
        if (available <= 0)
            break;

        int toRead = std::min (available, chunkSize);
        {
            const juce::SpinLock::ScopedLockType lock (ringLock);
            toRead = std::min (toRead, samplesUsed);
            const int first = std::min (toRead, ringSize - readIndex);
            std::copy (ring.getReadPointer (0, readIndex),
                       ring.getReadPointer (0, readIndex + first),
                       chunk.getWritePointer (0));
            if (first < toRead)
                std::copy (ring.getReadPointer (0, 0),
                           ring.getReadPointer (0, toRead - first),
                           chunk.getWritePointer (0, first));
            readIndex = (readIndex + toRead) % ringSize;
            samplesUsed -= toRead;
        }

        if (writer != nullptr)
        {
            const float* data = chunk.getReadPointer (0);
            writer->writeFromFloatArrays (&data, 1, toRead);
            samplesWritten.fetch_add ((uint64_t) toRead, std::memory_order_relaxed);
        }
    }
}
