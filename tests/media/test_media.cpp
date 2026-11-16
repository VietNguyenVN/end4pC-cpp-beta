// Exercise the real media parsing, command routing and artwork caches without a
// compositor or an actual player. Polls use fake playerctl; commands reach fake
// MPRIS peers on a private D-Bus session, including ambiguous playerctl prefixes.
// Requires dbus-run-session; exit 77 marks the test skipped when it is absent.
#define main end4pc_application_main
#include "../../src/main.cpp"
#undef main
#include "fake_player.h"

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::string readText(const std::filesystem::path& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void waitCommand(pid_t pid, bool success = true) {
    CHECK(pid > 0);
    if (pid <= 0) return;
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && (WEXITSTATUS(status) == 0) == success);
}
}

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--private-bus") {
        execlp("dbus-run-session", "dbus-run-session", "--", argv[0], "--private-bus", nullptr);
        if (errno == ENOENT) {
            std::fprintf(stderr, "Media test skipped: dbus-run-session is required for an isolated bus.\n");
            return 77;
        }
        std::perror("dbus-run-session");
        return 1;
    }
    namespace fs = std::filesystem;
    char temp[] = "/tmp/end4pc-media-test-XXXXXX";
    if (!mkdtemp(temp)) return 1;
    const fs::path root(temp), log = root / "playerctl.log", commands = root / "commands.log", downloads = root / "curl.log";
    const fs::path fake = root / "playerctl";
    {
        std::ofstream f(fake);
        f << "#!/bin/sh\n"
             "printf '%s\\n' \"$@\" > \"$END4PC_TEST_PLAYER_LOG\"\n"
             "if [ \"$END4PC_TEST_HANG\" = 1 ]; then exec /usr/bin/sleep 10; fi\n"
             "if [ \"${END4PC_TEST_STATS+x}\" = x ]; then printf '%s\\n' \"$END4PC_TEST_STATS\"; "
             "else printf '%s\\t12000000\\t90000000\\t0.5\\tPlaying\\t\\t\\t\\tSong\\n' \"$3\"; fi\n";
    }
    chmod(fake.c_str(), 0700);
    // Even a failing image decoder must never make this test contact a server.
    {
        std::ofstream f(root / "curl");
        f << "#!/bin/sh\nprintf 'attempt\\n' >> \"$END4PC_TEST_CURL_LOG\"\nexit 1\n";
    }
    chmod((root / "curl").c_str(), 0700);
    const std::string path = root.string() + ":/usr/bin:/bin";
    setenv("PATH", path.c_str(), 1);
    setenv("END4PC_TEST_PLAYER_LOG", log.c_str(), 1);
    setenv("END4PC_TEST_CURL_LOG", downloads.c_str(), 1);
    setenv("XDG_CACHE_HOME", root.c_str(), 1);
    unsetenv("END4PC_TEST_STATS");
    unsetenv("END4PC_TEST_HANG");

    FakePlayer chromium("chromium.instance123", commands), bridge("plasma-browser-integration", commands);
    FakePlayer vlc("vlc", commands), vlcPeer("vlc.instance456", commands);

    App app;
    // The preferred name is a discovery preference, not the selected instance.
    app.cfg.preferredPlayer = "audacious";
    CHECK(parseMusicLine(app, "chromium.instance123\tPlaying\t\t\tSong"));
    CHECK(app.musicPlayer == "chromium.instance123");
    waitCommand(mediaSend(app, {"play-pause"}));
    CHECK(readText(commands) == "chromium.instance123\nplay-pause\n");
    pollMedia(app);
    CHECK(readText(log).starts_with("-a\n-p\nchromium.instance123\nmetadata\n"));
    CHECK(app.media.position == 12 && app.media.length == 90);

    // An update from another player must move controls and progress together,
    // even if its visible song title is identical.
    CHECK(parseMusicLine(app, "plasma-browser-integration\tPlaying\t\t\tSong"));
    CHECK(app.media.position == 0 && app.media.length == 0);
    setenv("END4PC_TEST_STATS", "plasma-browser-integration\t12000000\t90000000\t0.2\tPlaying\t123\t\t\tSong", 1);
    waitCommand(mediaSend(app, {"next"}));
    CHECK(readText(commands) == "plasma-browser-integration\nnext\n");
    pollMedia(app);
    CHECK(readText(log).starts_with("-a\nmetadata\n"));

    // Same browser PID + song title: preserve rich bridge metadata but use the
    // native per-song clock instead of a growing playlist/media-element clock.
    setenv("END4PC_TEST_STATS",
        "plasma-browser-integration\t276000000\t351000000\t0.2\tPlaying\t123\t\t\tSong\n"
        "chromium.instance123\t35000000\t168000000\t1\tPlaying\t\t\t\tSong | YouTube Music", 1);
    pollMedia(app);
    CHECK(app.musicPlaybackPlayer == "chromium.instance123");
    CHECK(app.media.position == 35 && app.media.length == 168);
    CHECK(app.media.volume == 0.2);
    waitCommand(mediaSend(app, {"next"}));
    CHECK(readText(commands) == "chromium.instance123\nnext\n");
    waitCommand(mediaSend(app, {"volume", "0.3"}));
    CHECK(readText(commands) == "plasma-browser-integration\nvolume\n0.300000\n");
    waitCommand(mediaSend(app, {"position", "42.25"}));
    CHECK(readText(commands) == "chromium.instance123\nposition\n/test/current_track\n42250000\n");

    // Failed polls retain transport access, but must not send a native offset
    // to a bridge whose media-element clock uses a different origin.
    setenv("END4PC_TEST_STATS", "", 1);
    waitCommand(mediaSend(app, {"position", "42.25"}), false);
    waitCommand(mediaSend(app, {"play-pause"}));
    CHECK(readText(commands) == "plasma-browser-integration\nplay-pause\n");
    setenv("END4PC_TEST_STATS", "plasma-browser-integration\t276000000\t351000000\t0.2\tPlaying\t123\t\t\tSong", 1);
    waitCommand(mediaSend(app, {"position", "42.25"}), false);
    CHECK(readText(commands) == "plasma-browser-integration\nplay-pause\n");

    // Never borrow a clock from another browser or from another song.
    setenv("END4PC_TEST_STATS",
        "plasma-browser-integration\t276000000\t351000000\t0.2\tPlaying\t123\t\t\tSong\n"
        "chromium.instance999\t35000000\t168000000\t1\tPlaying\t\t\t\tSong | YouTube Music\n"
        "chromium.instance123\t35000000\t168000000\t1\tPlaying\t\t\t\tOther song | YouTube Music", 1);
    pollMedia(app);
    CHECK(app.musicPlaybackPlayer == "plasma-browser-integration");
    CHECK(app.media.position == 276);

    // Two native candidates with the same PID/title are ambiguous.
    setenv("END4PC_TEST_STATS",
        "plasma-browser-integration\t276000000\t351000000\t0.2\tPlaying\t123\t\t\tSong\n"
        "chromium.instance123\t35000000\t168000000\t1\tPlaying\t\t\t\tSong\n"
        "firefox.instance123\t36000000\t169000000\t1\tPlaying\t\t\t\tSong", 1);
    pollMedia(app);
    CHECK(app.musicPlaybackPlayer == "plasma-browser-integration" && app.media.position == 276);
    waitCommand(mediaSend(app, {"next"}));
    CHECK(readText(commands) == "plasma-browser-integration\nnext\n");

    // A known stopped bridge still accepts Play/Pause when the poll is empty.
    CHECK(parseMusicLine(app, "plasma-browser-integration\tStopped\t\tArtist\tSong"));
    setenv("END4PC_TEST_STATS", "", 1);
    waitCommand(mediaSend(app, {"play-pause"}));
    CHECK(readText(commands) == "plasma-browser-integration\nplay-pause\n");

    // Stopped ordinary players remain addressable. Neither a failed poll nor
    // playerctl's prefix matching may redirect commands to vlc.instance456.
    CHECK(parseMusicLine(app, "vlc\tStopped\t\tArtist\tSong"));
    CHECK(app.musicPlayer == "vlc" && app.musicStatus == "Stopped");
    pollMedia(app);
    CHECK(app.media.status.empty() && app.musicPlaybackPlayer.empty());
    waitCommand(mediaSend(app, {"play-pause"}));
    CHECK(readText(commands) == "vlc\nplay-pause\n");
    waitCommand(mediaSend(app, {"next"}));
    CHECK(readText(commands) == "vlc\nnext\n");
    // An unchanged watcher update after the failed poll does not need to repair
    // the timeline before transport works.
    parseMusicLine(app, "vlc\tStopped\t\tArtist\tSong");
    waitCommand(mediaSend(app, {"previous"}));
    CHECK(readText(commands) == "vlc\nprevious\n");
    setenv("END4PC_TEST_STATS",
        "vlc.instance456\t70000000\t80000000\t0.7\tPlaying\t\t\t\tSong\n"
        "vlc\t12000000\t90000000\t0.5\tPaused\t\t\t\tSong", 1);
    pollMedia(app);
    CHECK(app.media.position == 12 && app.media.length == 90);
    waitCommand(mediaSend(app, {"position", "15.5"}));
    CHECK(readText(commands) == "vlc\nposition\n/test/current_track\n15500000\n");
    waitCommand(mediaSend(app, {"volume", "0.4"}));
    CHECK(readText(commands) == "vlc\nvolume\n0.400000\n");
    // Malformed and timed-out polls clear the clock without disabling transport.
    setenv("END4PC_TEST_STATS", "vlc\tnot-a-position\t90000000\t0.5\tPlaying\t\t\t\tSong", 1);
    pollMedia(app);
    CHECK(app.media.status.empty() && app.musicPlayer == "vlc");
    waitCommand(mediaSend(app, {"play-pause"}));
    CHECK(readText(commands) == "vlc\nplay-pause\n");
    setenv("END4PC_TEST_HANG", "1", 1);
    const auto pollStart = std::chrono::steady_clock::now();
    pollMedia(app);
    CHECK(std::chrono::steady_clock::now() - pollStart < std::chrono::seconds(4));
    CHECK(app.media.status.empty());
    waitCommand(mediaSend(app, {"next"}));
    CHECK(readText(commands) == "vlc\nnext\n");
    // Bridge revalidation belongs in the command child, not the UI thread.
    parseMusicLine(app, "plasma-browser-integration\tPlaying\t\tArtist\tSong");
    const auto commandStart = std::chrono::steady_clock::now();
    const pid_t slowCommand = mediaSend(app, {"play-pause"});
    CHECK(std::chrono::steady_clock::now() - commandStart < std::chrono::milliseconds(500));
    waitCommand(slowCommand);
    CHECK(readText(commands) == "plasma-browser-integration\nplay-pause\n");
    unsetenv("END4PC_TEST_HANG");
    unsetenv("END4PC_TEST_STATS");
    // The production shell auto-reaps query children; ECHILD is not a failure.
    signal(SIGCHLD, SIG_IGN);
    const auto autoReaped = mediaSnapshots("vlc");
    signal(SIGCHLD, SIG_DFL);
    CHECK(autoReaped.size() == 1 && autoReaped[0].instance == "vlc");
    // Exact owner disappears while a prefix peer remains: fail, never fall back.
    parseMusicLine(app, "vlc\tStopped\t\tArtist\tSong");
    vlc.stop();
    fs::remove(commands);
    waitCommand(mediaSend(app, {"next"}), false);
    CHECK(!fs::exists(commands));

    // Missing volume is normal for some MPRIS players and must not freeze time.
    mediaplayer::Snapshot noVolume;
    CHECK(mediaplayer::parse("chromium.instance123\t35000000\t168000000\t\tPlaying\t\t\t\tSong", noVolume));
    CHECK(noVolume.position == 35 && !noVolume.volume);
    CHECK(parseMusicLine(app, "plasma-browser-integration\tPlaying\t\t\tNew song"));
    CHECK(app.media.position == 0 && app.media.length == 0);

    // Empty artist/artwork fields must not shift the player or title fields.
    CHECK(parseMusicLine(app, "chromium.instance456\tPaused\t\t\tSong\tLive"));
    CHECK(app.musicPlayer == "chromium.instance456");
    CHECK(app.musicTitle == "Song\tLive" && app.musicArtist.empty());
    CHECK(parseMusicLine(app, ""));
    CHECK(app.musicPlayer.empty() && app.musicTitle.empty());
    fs::remove(log);
    mediaSend(app, {"next"});
    pollMedia(app);
    CHECK(!fs::exists(log));  // never fall back to an unrelated player
    parseMusicLine(app, "bad'player\tPlaying\t\t\tSong");
    CHECK(app.musicPlayer.empty());

    // Prefer an actual cover. Fall back only to a validated current YouTube
    // Music video URL; never reuse another track's image or guess from its title.
    mediaplayer::Snapshot thumbnail;
    thumbnail.url = "https://music.youtube.com/watch?list=playlist&v=abcdefgh_-1";
    CHECK(mediaplayer::coverUrl(thumbnail) == "https://i.ytimg.com/vi/abcdefgh_-1/hqdefault.jpg");
    thumbnail.artwork = "file:///tmp/actual-cover.png";
    CHECK(mediaplayer::coverUrl(thumbnail) == thumbnail.artwork);
    thumbnail.artwork.clear();
    thumbnail.url = "https://music.youtube.com.evil.test/watch?v=abcdefgh_-1";
    CHECK(mediaplayer::coverUrl(thumbnail).empty());
    thumbnail.url = "https://music.youtube.com/watch?v=../bad/path";
    CHECK(mediaplayer::coverUrl(thumbnail).empty());
    thumbnail.url.clear();
    CHECK(mediaplayer::coverUrl(thumbnail).empty());

    // The bridge announces a local filename before its image can be decoded.
    // Retrying the same URL after the file appears must recover without toggling playback.
    const fs::path late = root / "late.png";
    const std::string localUrl = "file://" + late.string();
    updateCover(app, localUrl);
    CHECK(!app.musicArt);
    refreshMediaCoverIfNeeded(app);
    cairo_surface_t* localImage = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
    CHECK(cairo_surface_write_to_png(localImage, late.c_str()) == CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(localImage);
    updateCover(app, localUrl);
    CHECK(app.musicArt);
    refreshMediaCoverIfNeeded(app);
    CHECK(app.media.cover);
    // Polling must discover a newly published artwork URL even without a watcher event.
    parseMusicLine(app, "plasma-browser-integration\tPlaying\t\t\tSong");
    const std::string lateStats = "plasma-browser-integration\t12000000\t90000000\t0.2\tPlaying\t123\t" + localUrl + "\t\tSong";
    setenv("END4PC_TEST_STATS", lateStats.c_str(), 1);
    pollMedia(app);
    CHECK(app.musicArtUrl == localUrl && app.musicArt);
    app.musicArtFallback = localUrl;
    parseMusicLine(app, "plasma-browser-integration\tPlaying\t\t\tSong");
    CHECK(app.musicArtUrl == localUrl && app.musicArt);
    parseMusicLine(app, "plasma-browser-integration\tPlaying\t\t\tNext song");
    CHECK(app.musicArtFallback.empty() && !app.musicArt);
    updateCover(app, "");
    refreshMediaCoverIfNeeded(app);
    mediaWRefreshCover(app);

    // The popup/widget saw the URL before its asynchronous download completed.
    // Loading the same URL later must replace their cached empty surfaces.
    const std::string url = "https://example.invalid/cover.png";
    app.musicArtUrl = url;
    refreshMediaCoverIfNeeded(app);
    mediaWRefreshCover(app);
    CHECK(app.media.coverKey == url && !app.media.cover && !app.mediaW.blur);
    const fs::path cache = artCachePath(url);
    fs::create_directories(cache.parent_path());
    cairo_surface_t* image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
    CHECK(cairo_surface_write_to_png(image, cache.c_str()) == CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(image);
    // This is the same reload sequence used by the artwork-completion event.
    updateCover(app, url);
    CHECK(app.musicArt);
    mediaWRefreshCover(app);
    CHECK(app.media.cover && app.media.blur && app.mediaW.blur);
    CHECK(app.media.coverKey == url);

    // Fetch completion is also delivered on failure. It must reload the cache
    // without resetting the retry budget or spawning a tight retry loop.
    int completed[2];
    CHECK(pipe2(completed, O_CLOEXEC | O_NONBLOCK) == 0);
    g_artFetchFd = completed[0];
    g_artFetchWrite = completed[1];
    auto completeDownload = [&] {
        pollfd done{g_artFetchFd, POLLIN, 0};
        CHECK(poll(&done, 1, 5000) == 1);
        char junk[64];
        CHECK(read(g_artFetchFd, junk, sizeof junk) > 0);
        const std::string current = app.musicArtUrl;
        updateCover(app, current);  // production completion path
    };
    const std::string failingUrl = "https://example.invalid/missing-cover.png";
    updateCover(app, failingUrl);
    completeDownload();
    CHECK(app.musicArtAttempts == 1 && !app.musicArt);
    for (int i = 0; i < 5; ++i) updateCover(app, failingUrl);
    CHECK(readText(downloads) == "attempt\n");
    for (int attempt = 2; attempt <= 3; ++attempt) {
        app.musicArtRetryAt = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        updateCover(app, failingUrl);
        completeDownload();
        CHECK(app.musicArtAttempts == attempt);
    }
    app.musicArtRetryAt = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    updateCover(app, failingUrl);
    CHECK(readText(downloads) == "attempt\nattempt\nattempt\n");
    CHECK(app.musicArtAttempts == 3);
    // A genuinely different cover gets a new bounded budget, even if its cache
    // entry exists but is not an image.
    const std::string invalidUrl = "https://example.invalid/corrupt-cover.png";
    std::ofstream(artCachePath(invalidUrl)) << "not an image";
    updateCover(app, invalidUrl);
    completeDownload();
    CHECK(app.musicArtAttempts == 1 && !app.musicArt);
    CHECK(readText(downloads) == "attempt\nattempt\nattempt\nattempt\n");
    close(g_artFetchFd); close(g_artFetchWrite);
    g_artFetchFd = g_artFetchWrite = -1;

    // Image decoders may keep their own helper process alive until we exit.
    // Do not wait for those unrelated children here.
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    std::printf("Media regression tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
