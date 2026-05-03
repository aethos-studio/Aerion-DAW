#pragma once
#include <JuceHeader.h>

class GoogleDriveClient : public juce::Thread
{
public:
    struct DriveFile
    {
        juce::String id;
        juce::String name;
        juce::String mimeType;
    };

    GoogleDriveClient();
    ~GoogleDriveClient() override;

    void login();
    void logout();
    bool isLoggedIn() const noexcept { return ! accessToken.isEmpty(); }

    // Refreshes the access token using the stored refresh token.
    // Returns true on success. Network call — invoke off the message thread.
    bool refreshAccessToken();

    // Uploads a file to the user's Drive (folder of their choice). Runs async.
    void saveProject (const juce::File& projectFile);

    // Lists audio files in the user's Drive. Result is delivered to onFilesListed
    // on the message thread.
    void listAudioFiles();

    // Downloads a Drive file to a temp location; result delivered to onFileDownloaded
    // on the message thread.
    void downloadFile (const DriveFile& file);

    std::function<void (bool loggedIn)> onLoginStateChanged;
    std::function<void (juce::Array<DriveFile>)> onFilesListed;
    std::function<void (juce::File)> onFileDownloaded;

    // Configure these via your Google Cloud OAuth client (Desktop type).
    // The "secret" for desktop apps is not actually secret, but Google's token
    // endpoint still requires it.
    juce::String clientId     = "YOUR_CLIENT_ID";
    juce::String clientSecret = "YOUR_CLIENT_SECRET";

    // From juce::Thread — runs the local OAuth redirect listener.
    void run() override;

private:
    juce::String accessToken;
    juce::String refreshToken;

    juce::String codeVerifier;
    juce::String authCode;

    void exchangeCodeForToken();
    juce::var sendRequest (const juce::String& url, const juce::String& method,
                           const juce::String& body = {},
                           const juce::String& contentType = "application/json");

    static juce::String encodeForm (const juce::StringPairArray& params);

    void saveTokens();
    void loadTokens();
    void notifyLoginState();

    std::unique_ptr<juce::StreamingSocket> serverSocket;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoogleDriveClient)
};
