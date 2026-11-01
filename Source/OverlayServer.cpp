#include "OverlayServer.h"

#include <juce_core/juce_core.h>
#include "BinaryData.h"

namespace
{
    constexpr int maxClients = 8;
    constexpr int maxFrames = 240;

    juce::String statusLine (int code, const juce::String& reason)
    {
        return "HTTP/1.1 " + juce::String (code) + " " + reason + "\r\n";
    }
}

struct OverlayServer::Client
{
    std::unique_ptr<juce::StreamingSocket> socket;
    juce::String request;
    bool requestComplete = false;
    bool isSse = false;
    bool dead = false;
    bool sentHeaders = false;
    uint64_t lastSentFrame = 0;
    uint64_t lastStateVersion = 0;
    double lastHeartbeatMs = 0.0;
};

OverlayServer::OverlayServer()
    : juce::Thread ("Overlay HTTP")
{
    overlayHtml = juce::String::fromUTF8 (BinaryData::overlay_html, BinaryData::overlay_htmlSize);
}

OverlayServer::~OverlayServer()
{
    stop();
}

bool OverlayServer::start (int preferredPort)
{
    stop();

    startupError.clear();

    for (int candidate = preferredPort; candidate < preferredPort + 12; ++candidate)
    {
        auto candidateListener = std::make_unique<juce::StreamingSocket>();
        if (candidateListener->createListener (candidate, "127.0.0.1"))
        {
            listener = std::move (candidateListener);
            port.store (candidate, std::memory_order_relaxed);
            stopping.store (false, std::memory_order_release);
            listening.store (true, std::memory_order_release);
            startThread (juce::Thread::Priority::normal);
            return true;
        }
    }

    startupError = "Could not bind a local port (39871-39882).";
    return false;
}

void OverlayServer::stop()
{
    stopping.store (true, std::memory_order_release);

    if (isThreadRunning())
        stopThread (2000);

    if (listener != nullptr)
    {
        listener->close();
        listener.reset();
    }

    clients.clear();
    listening.store (false, std::memory_order_release);
    port.store (0, std::memory_order_relaxed);
}

bool OverlayServer::isRunning() const
{
    return listening.load (std::memory_order_acquire) && isThreadRunning();
}

juce::String OverlayServer::getOverlayUrl() const
{
    const auto p = port.load (std::memory_order_relaxed);
    if (p <= 0)
        return {};

    return "http://127.0.0.1:" + juce::String (p) + "/overlay";
}

juce::String OverlayServer::getStatusText() const
{
    if (isRunning())
        return "OBS overlay live at " + getOverlayUrl();

    if (startupError.isNotEmpty())
        return startupError;

    return "Overlay server offline";
}

void OverlayServer::publishPoints (const juce::String& json)
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    nextSequence++;
    frames.push_back ({ nextSequence, json });

    while ((int) frames.size() > maxFrames)
        frames.pop_front();
}

void OverlayServer::publishState (const juce::String& json)
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    stateJson = json;
    ++stateVersion;
}

void OverlayServer::clearHistory()
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    frames.clear();
}

void OverlayServer::run()
{
    while (! threadShouldExit())
    {
        acceptConnections();

        if (stopping.load (std::memory_order_acquire))
            break;

        service();
        wait (3);
    }

    for (auto& client : clients)
        if (client->socket != nullptr)
            client->socket->close();

    clients.clear();

    if (listener != nullptr)
        listener->close();
}

void OverlayServer::acceptConnections()
{
    if (listener == nullptr)
        return;

    for (int i = 0; i < 4; ++i)
    {
        if (listener->waitUntilReady (true, 0) <= 0)
            break;

        auto* socket = listener->waitForNextConnection();
        if (socket == nullptr)
            break;

        if ((int) clients.size() >= maxClients)
        {
            delete socket;
            continue;
        }

        auto client = std::make_unique<Client>();
        client->socket.reset (socket);
        client->lastHeartbeatMs = juce::Time::getMillisecondCounterHiRes();
        clients.push_back (std::move (client));
    }
}

void OverlayServer::service()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    for (auto& client : clients)
    {
        if (client->dead || client->socket == nullptr)
            continue;

        if (! client->requestComplete)
        {
            if (client->socket->waitUntilReady (true, 0) > 0)
            {
                char buffer[4096];
                const int bytes = client->socket->read (buffer, (int) sizeof (buffer), false);

                if (bytes <= 0)
                {
                    client->dead = true;
                    continue;
                }

                client->request.append (buffer, (int) bytes);

                if (client->request.length() > 16384)
                {
                    client->dead = true;
                    continue;
                }

                if (client->request.contains ("\r\n\r\n"))
                {
                    client->requestComplete = true;

                    const auto firstLine = client->request.upToFirstOccurrenceOf ("\r\n", false, false);
                    const auto tokens = juce::StringArray::fromTokens (firstLine, " ", "");
                    auto path = tokens.size() > 1 ? tokens[1] : juce::String ("/");

                    if (! path.startsWith ("/"))
                        path = "/" + path;

                    path = path.upToFirstOccurrenceOf ("?", false, false);
                    handleRequest (*client, path);
                }
            }
        }
        else if (client->isSse)
        {
            streamPending (*client);

            if (client->dead)
                continue;

            if (nowMs - client->lastHeartbeatMs > 10000.0)
            {
                const juce::String heartbeat (": heartbeat\n\n");
                if (client->socket->write (heartbeat.toRawUTF8(), (int) heartbeat.getNumBytesAsUTF8()) <= 0)
                    client->dead = true;
                else
                    client->lastHeartbeatMs = nowMs;
            }
        }
    }

    for (auto& client : clients)
        if (client->dead && client->socket != nullptr)
            client->socket->close();

    clients.erase (std::remove_if (clients.begin(), clients.end(),
                                   [] (const std::unique_ptr<Client>& client) { return client->dead; }),
                   clients.end());
}

void OverlayServer::handleRequest (Client& client, const juce::String& path)
{
    if (path == "/overlay" || path == "/overlay.html" || path == "/")
    {
        sendStatic (client, overlayHtml, "text/html; charset=utf-8");
        return;
    }

    if (path == "/events")
    {
        beginSse (client);
        return;
    }

    if (path == "/health")
    {
        sendStatic (client, "ok", "text/plain; charset=utf-8");
        return;
    }

    if (path == "/state")
    {
        juce::String state;
        {
            const juce::SpinLock::ScopedLockType lock (dataLock);
            state = stateJson;
        }
        sendStatic (client, state, "application/json; charset=utf-8");
        return;
    }

    if (path == "/favicon.ico")
    {
        const juce::String headers = statusLine (204, "No Content")
                                     + "Access-Control-Allow-Origin: *\r\n"
                                     + "Connection: close\r\n"
                                     + "Content-Length: 0\r\n\r\n";
        client.socket->write (headers.toRawUTF8(), (int) headers.getNumBytesAsUTF8());
        client.dead = true;
        return;
    }

    sendStatic (client, "<!doctype html><title>404</title><h1>Not found</h1>", "text/html; charset=utf-8");
}

void OverlayServer::sendStatic (Client& client, const juce::String& body, const juce::String& contentType)
{
    const int bodyBytes = (int) body.getNumBytesAsUTF8();
    const juce::String headers = statusLine (200, "OK")
                                 + "Content-Type: " + contentType + "\r\n"
                                 + "Content-Length: " + juce::String (bodyBytes) + "\r\n"
                                 + "Cache-Control: no-store\r\n"
                                 + "Access-Control-Allow-Origin: *\r\n"
                                 + "Connection: close\r\n\r\n";

    if (client.socket->write (headers.toRawUTF8(), (int) headers.getNumBytesAsUTF8()) <= 0
        || (bodyBytes > 0 && client.socket->write (body.toRawUTF8(), bodyBytes) <= 0))
    {
        client.dead = true;
        return;
    }

    client.dead = true;
}

void OverlayServer::beginSse (Client& client)
{
    const juce::String headers = statusLine (200, "OK")
                                 + "Content-Type: text/event-stream; charset=utf-8\r\n"
                                 + "Cache-Control: no-store, no-cache, must-revalidate\r\n"
                                 + "Connection: keep-alive\r\n"
                                 + "Access-Control-Allow-Origin: *\r\n"
                                 + "X-Accel-Buffering: no\r\n\r\n"
                                 + "retry: 800\n\n";

    if (client.socket->write (headers.toRawUTF8(), (int) headers.getNumBytesAsUTF8()) <= 0)
    {
        client.dead = true;
        return;
    }

    client.isSse = true;
    client.sentHeaders = true;
    client.lastHeartbeatMs = juce::Time::getMillisecondCounterHiRes();

    juce::String state;
    std::vector<Frame> history;
    uint64_t version = 0;

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        state = stateJson;
        version = stateVersion;
        const int start = std::max (0, (int) frames.size() - 180);
        history.reserve ((size_t) ((int) frames.size() - start));
        for (int i = start; i < (int) frames.size(); ++i)
            history.push_back (frames[(size_t) i]);
    }

    sendSse (client, "state", state);
    sendSse (client, "reset", "{}");

    for (const auto& frame : history)
        sendSse (client, "pts", frame.json);

    client.lastStateVersion = version;
    if (! history.empty())
        client.lastSentFrame = history.back().sequence;

    if (client.dead)
        return;

    // If new frames arrived while we were sending, they will be streamed on the
    // next service pass.
}

void OverlayServer::streamPending (Client& client)
{
    juce::String state;
    uint64_t version = 0;
    std::vector<Frame> pending;

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);

        if (client.lastStateVersion != stateVersion)
        {
            state = stateJson;
            version = stateVersion;
            client.lastStateVersion = version;
        }

        for (const auto& frame : frames)
            if (frame.sequence > client.lastSentFrame)
                pending.push_back (frame);

        if (! pending.empty())
            client.lastSentFrame = pending.back().sequence;
    }

    if (version != 0)
        sendSse (client, "state", state);

    for (const auto& frame : pending)
    {
        sendSse (client, "pts", frame.json);
        if (client.dead)
            return;
    }
}

void OverlayServer::sendSse (Client& client, const juce::String& event, const juce::String& data)
{
    if (client.dead || client.socket == nullptr)
        return;

    juce::String text;
    text << "event: " << event << "\n";
    text << "data: " << data << "\n\n";

    if (client.socket->write (text.toRawUTF8(), (int) text.getNumBytesAsUTF8()) <= 0)
        client.dead = true;
}
