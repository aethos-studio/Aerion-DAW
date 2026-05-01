#include "GoogleDriveClient.h"

namespace
{
    constexpr int redirectPort = 8080;
    const juce::String redirectUri = "http://localhost:8080";

    juce::String base64url (const void* data, size_t size)
    {
        return juce::Base64::toBase64 (data, size)
                  .replaceCharacter ('+', '-')
                  .replaceCharacter ('/', '_')
                  .removeCharacters ("=");
    }

    juce::String generateCodeVerifier()
    {
        juce::Random rng;
        rng.setSeedRandomly();

        juce::MemoryBlock bytes (64);
        for (size_t i = 0; i < bytes.getSize(); ++i)
            bytes[i] = (char) rng.nextInt (256);

        return base64url (bytes.getData(), bytes.getSize());
    }

    juce::String codeChallengeFor (const juce::String& verifier)
    {
        const juce::SHA256 hash (verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
        const auto digest = hash.getRawData();
        return base64url (digest.getData(), digest.getSize());
    }

    juce::File getTokenFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("AerionDaw")
                   .getChildFile ("drive_tokens.json");
    }
}

GoogleDriveClient::GoogleDriveClient() : juce::Thread ("GoogleDriveClient OAuth")
{
    loadTokens();
}

GoogleDriveClient::~GoogleDriveClient()
{
    if (serverSocket != nullptr)
        serverSocket->close();

    stopThread (2000);
}

void GoogleDriveClient::login()
{
    if (isLoggedIn() || isThreadRunning())
        return;

    codeVerifier = generateCodeVerifier();
    const auto challenge = codeChallengeFor (codeVerifier);

    auto authUrl = juce::URL ("https://accounts.google.com/o/oauth2/v2/auth")
                       .withParameter ("client_id", clientId)
                       .withParameter ("redirect_uri", redirectUri)
                       .withParameter ("response_type", "code")
                       .withParameter ("scope", "https://www.googleapis.com/auth/drive.file")
                       .withParameter ("code_challenge_method", "S256")
                       .withParameter ("code_challenge", challenge)
                       .withParameter ("access_type", "offline")
                       .withParameter ("prompt", "consent");

    authUrl.launchInDefaultBrowser();
    startThread();
}

void GoogleDriveClient::logout()
{
    accessToken  = {};
    refreshToken = {};
    saveTokens();
    notifyLoginState();
}

void GoogleDriveClient::run()
{
    serverSocket = std::make_unique<juce::StreamingSocket>();

    if (! serverSocket->createListener (redirectPort))
    {
        DBG ("GoogleDriveClient: failed to bind redirect listener on port " << redirectPort);
        serverSocket.reset();
        return;
    }

    constexpr int totalTimeoutMs = 120000; // user has 2 minutes to consent
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) totalTimeoutMs;

    while (! threadShouldExit())
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now >= deadline)
            break;

        const int waitMs = juce::jmin (500, (int) (deadline - now));
        if (serverSocket->waitUntilReady (true, waitMs) != 1)
            continue;

        std::unique_ptr<juce::StreamingSocket> connection (serverSocket->waitForNextConnection());
        if (connection == nullptr)
            continue;

        char buffer[2048] = {};
        connection->waitUntilReady (true, 2000);
        const int bytesRead = connection->read (buffer, (int) sizeof (buffer) - 1, false);
        if (bytesRead <= 0)
            continue;

        const juce::String request (buffer, (size_t) bytesRead);

        juce::String code, error;
        if (request.startsWith ("GET /?"))
        {
            const auto query = request.fromFirstOccurrenceOf ("GET /?", false, false)
                                      .upToFirstOccurrenceOf (" ", false, false);

            for (const auto& pair : juce::StringArray::fromTokens (query, "&", {}))
            {
                const auto k = pair.upToFirstOccurrenceOf ("=", false, false);
                const auto v = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));
                if (k == "code")  code  = v;
                if (k == "error") error = v;
            }
        }

        const auto html = code.isNotEmpty()
            ? juce::String ("<h1>Aerion DAW is connected.</h1><p>You can close this window.</p>")            : juce::String ("<h1>Authentication failed.</h1><p>") + error + "</p>";

        const auto response = juce::String ("HTTP/1.1 200 OK\r\n")
                            + "Content-Type: text/html; charset=utf-8\r\n"
                            + "Content-Length: " + juce::String (html.getNumBytesAsUTF8()) + "\r\n"
                            + "Connection: close\r\n\r\n"
                            + html;

        connection->write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());

        if (code.isNotEmpty())
        {
            authCode = code;
            exchangeCodeForToken();
        }
        break;
    }

    serverSocket.reset();
}

void GoogleDriveClient::exchangeCodeForToken()
{
    juce::StringPairArray params;
    params.set ("code", authCode);
    params.set ("client_id", clientId);
    params.set ("client_secret", clientSecret);
    params.set ("redirect_uri", redirectUri);
    params.set ("grant_type", "authorization_code");
    params.set ("code_verifier", codeVerifier);

    const auto result = sendRequest ("https://oauth2.googleapis.com/token", "POST",
                                     encodeForm (params),
                                     "application/x-www-form-urlencoded");

    if (auto* obj = result.getDynamicObject())
    {
        accessToken  = obj->getProperty ("access_token").toString();
        refreshToken = obj->getProperty ("refresh_token").toString();
        saveTokens();
        notifyLoginState();
    }
}

bool GoogleDriveClient::refreshAccessToken()
{
    if (refreshToken.isEmpty())
        return false;

    juce::StringPairArray params;
    params.set ("client_id", clientId);
    params.set ("client_secret", clientSecret);
    params.set ("refresh_token", refreshToken);
    params.set ("grant_type", "refresh_token");

    const auto result = sendRequest ("https://oauth2.googleapis.com/token", "POST",
                                     encodeForm (params),
                                     "application/x-www-form-urlencoded");

    if (auto* obj = result.getDynamicObject())
    {
        const auto newToken = obj->getProperty ("access_token").toString();
        if (newToken.isNotEmpty())
        {
            accessToken = newToken;
            saveTokens();
            return true;
        }
    }

    return false;
}

juce::String GoogleDriveClient::encodeForm (const juce::StringPairArray& params)
{
    juce::String s;
    for (const auto& key : params.getAllKeys())
    {
        if (s.isNotEmpty()) s << "&";
        s << juce::URL::addEscapeChars (key, true)
          << "="
          << juce::URL::addEscapeChars (params[key], true);
    }
    return s;
}

juce::var GoogleDriveClient::sendRequest (const juce::String& url, const juce::String& method,
                                          const juce::String& body, const juce::String& contentType)
{
    juce::URL juceUrl (url);
    if (! body.isEmpty())
        juceUrl = juceUrl.withPOSTData (body);

    juce::StringPairArray headers;
    headers.set ("Content-Type", contentType);
    if (accessToken.isNotEmpty())
        headers.set ("Authorization", "Bearer " + accessToken);

    const auto paramHandling = body.isEmpty() ? juce::URL::ParameterHandling::inAddress
                                              : juce::URL::ParameterHandling::inPostData;

    const auto options = juce::URL::InputStreamOptions (paramHandling)
                             .withExtraHeaders (headers.getDescription())
                             .withHttpRequestCmd (method);

    if (auto stream = juceUrl.createInputStream (options))
        return juce::JSON::parse (stream->readEntireStreamAsString());

    return {};
}

void GoogleDriveClient::saveProject (const juce::File& projectFile)
{
    if (! isLoggedIn() || ! projectFile.existsAsFile())
        return;

    juce::Thread::launch ([this, projectFile]
    {
        const juce::String boundary = "AerionDawBoundary_" + juce::Uuid().toString();
        juce::DynamicObject::Ptr metadata = new juce::DynamicObject();
        metadata->setProperty ("name", projectFile.getFileName());

        juce::MemoryBlock fileBytes;
        if (! projectFile.loadFileAsData (fileBytes))
            return;

        juce::MemoryOutputStream out;
        out << "--" << boundary << "\r\n"
            << "Content-Type: application/json; charset=UTF-8\r\n\r\n"
            << juce::JSON::toString (juce::var (metadata.get())) << "\r\n"
            << "--" << boundary << "\r\n"
            << "Content-Type: application/octet-stream\r\n\r\n";
        out.write (fileBytes.getData(), fileBytes.getSize());
        out << "\r\n--" << boundary << "--\r\n";

        const juce::MemoryBlock postData (out.getData(), out.getDataSize());

        auto url = juce::URL ("https://www.googleapis.com/upload/drive/v3/files")
                       .withParameter ("uploadType", "multipart")
                       .withPOSTData (postData);

        juce::StringPairArray headers;
        headers.set ("Content-Type", "multipart/related; boundary=" + boundary);
        headers.set ("Authorization", "Bearer " + accessToken);

        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                 .withExtraHeaders (headers.getDescription())
                                 .withHttpRequestCmd ("POST");

        if (auto stream = url.createInputStream (options))
        {
            const auto responseText = stream->readEntireStreamAsString();
            DBG ("Drive upload response: " << responseText);
        }
    });
}

void GoogleDriveClient::listAudioFiles()
{
    if (! isLoggedIn())
        return;

    juce::Thread::launch ([this]
    {
        auto url = juce::URL ("https://www.googleapis.com/drive/v3/files")
                       .withParameter ("q", "mimeType contains 'audio/'")
                       .withParameter ("fields", "files(id,name,mimeType)")
                       .withParameter ("pageSize", "100");

        juce::StringPairArray headers;
        headers.set ("Authorization", "Bearer " + accessToken);

        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withExtraHeaders (headers.getDescription())
                                 .withHttpRequestCmd ("GET");

        auto stream = url.createInputStream (options);
        if (stream == nullptr)
            return;

        const auto json = juce::JSON::parse (stream->readEntireStreamAsString());

        juce::Array<DriveFile> files;
        if (auto* arr = json["files"].getArray())
        {
            for (const auto& v : *arr)
            {
                DriveFile f;
                f.id       = v["id"].toString();
                f.name     = v["name"].toString();
                f.mimeType = v["mimeType"].toString();
                files.add (f);
            }
        }

        if (onFilesListed)
            juce::MessageManager::callAsync ([cb = onFilesListed, files]() { cb (files); });
    });
}

void GoogleDriveClient::saveTokens()
{
    const auto file = getTokenFile();
    file.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("access_token",  accessToken);
    obj->setProperty ("refresh_token", refreshToken);

    file.replaceWithText (juce::JSON::toString (juce::var (obj.get())));
}

void GoogleDriveClient::loadTokens()
{
    const auto file = getTokenFile();
    if (! file.existsAsFile())
        return;

    const auto v = juce::JSON::parse (file.loadFileAsString());
    if (auto* obj = v.getDynamicObject())
    {
        accessToken  = obj->getProperty ("access_token").toString();
        refreshToken = obj->getProperty ("refresh_token").toString();
    }
}

void GoogleDriveClient::notifyLoginState()
{
    if (! onLoginStateChanged)
        return;

    const bool loggedIn = isLoggedIn();
    juce::MessageManager::callAsync ([cb = onLoginStateChanged, loggedIn]() { cb (loggedIn); });
}
