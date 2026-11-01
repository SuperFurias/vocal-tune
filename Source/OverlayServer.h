#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

/**
    Tiny local HTTP server for the OBS browser source.

    Serves the embedded overlay page and streams pitch points over
    Server-Sent Events. Only listens on the loopback interface.
*/
class OverlayServer : private juce::Thread
{
public:
    OverlayServer();
    ~OverlayServer() override;

    bool start (int preferredPort = 39871);
    void stop();

    bool isRunning() const;
    int getPort() const noexcept { return port.load (std::memory_order_relaxed); }
    juce::String getOverlayUrl() const;
    juce::String getStatusText() const;

    /** Publishes one JSON frame of the form {"now":123,"pts":[[...],...]}. */
    void publishPoints (const juce::String& json);

    /** Publishes a JSON state object, sent to new and existing clients. */
    void publishState (const juce::String& json);

    void clearHistory();

private:
    struct Client;
    struct Frame
    {
        uint64_t sequence = 0;
        juce::String json;
    };

    void run() override;
    void service();
    void acceptConnections();
    void handleRequest (Client& client, const juce::String& path);
    void sendStatic (Client& client, const juce::String& body, const juce::String& contentType);
    void sendHeaders (Client& client, const juce::String& extraHeaders);
    void sendSse (Client& client, const juce::String& event, const juce::String& data);
    void beginSse (Client& client);
    void streamPending (Client& client);
    void closeClient (Client& client);

    juce::String overlayHtml;

    std::unique_ptr<juce::StreamingSocket> listener;
    std::vector<std::unique_ptr<Client>> clients;

    juce::SpinLock dataLock;
    std::deque<Frame> frames;
    juce::String stateJson { "{}" };
    uint64_t nextSequence = 1;
    uint64_t stateVersion = 0;

    std::atomic<int> port { 0 };
    std::atomic<bool> listening { false };
    std::atomic<bool> stopping { false };
    juce::String startupError;
};
