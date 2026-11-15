// Exercise the real media parsing, command routing and artwork caches without a
// compositor or an actual player. playerctl is replaced only in this process's PATH.
#define main end4pc_application_main
#include "../../src/main.cpp"
#undef main

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::string readText(const std::filesystem::path& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
}

int main() {
    namespace fs = std::filesystem;
    char temp[] = "/tmp/end4pc-media-test-XXXXXX";
    if (!mkdtemp(temp)) return 1;
    const fs::path root(temp), log = root / "playerctl.log";
    const fs::path fake = root / "playerctl";
    {
        std::ofstream f(fake);
        f << "#!/bin/sh\n"
             "printf '%s\\n' \"$@\" > \"$END4PC_TEST_PLAYER_LOG\"\n"
             "if [ \"$1\" = -a ]; then printf '%s\\n' \"$END4PC_TEST_STATS\"; "
             "elif [ \"$3\" = metadata ]; then printf '%s\\t12000000\\t90000000\\t0.5\\tPlaying\\t\\t\\tSong\\n' \"$2\"; fi\n";
    }
    chmod(fake.c_str(), 0700);
    // Even a failing image decoder must never make this test contact a server.
    {
        std::ofstream f(root / "curl");
        f << "#!/bin/sh\nexit 1\n";
    }
    chmod((root / "curl").c_str(), 0700);
    const std::string path = root.string() + ":/usr/bin:/bin";
    setenv("PATH", path.c_str(), 1);
    setenv("END4PC_TEST_PLAYER_LOG", log.c_str(), 1);
    setenv("XDG_CACHE_HOME", root.c_str(), 1);

    App app;
    // The preferred name is a discovery preference, not the selected instance.
    app.cfg.preferredPlayer = "audacious";
    CHECK(parseMusicLine(app, "chromium.instance123\tPlaying\t\t\tSong"));
    CHECK(app.musicPlayer == "chromium.instance123");
    mediaSend(app, {"play-pause"});
    CHECK(waitpid(-1, nullptr, 0) > 0);
    CHECK(readText(log) == "-p\nchromium.instance123\nplay-pause\n");
    pollMedia(app);
    CHECK(readText(log).starts_with("-p\nchromium.instance123\nmetadata\n"));
    CHECK(app.media.position == 12 && app.media.length == 90);

    // An update from another player must move controls and progress together,
    // even if its visible song title is identical.
    CHECK(parseMusicLine(app, "plasma-browser-integration\tPlaying\t\t\tSong"));
    CHECK(app.media.position == 0 && app.media.length == 0);
    setenv("END4PC_TEST_STATS", "plasma-browser-integration\t12000000\t90000000\t0.2\tPlaying\t123\t\tSong", 1);
    mediaSend(app, {"next"});
    CHECK(waitpid(-1, nullptr, 0) > 0);
    CHECK(readText(log) == "-p\nplasma-browser-integration\nnext\n");
    pollMedia(app);
    CHECK(readText(log).starts_with("-a\nmetadata\n"));

    // Same browser PID + song title: preserve rich bridge metadata but use the
    // native per-song clock instead of a growing playlist/media-element clock.
    setenv("END4PC_TEST_STATS",
        "plasma-browser-integration\t276000000\t351000000\t0.2\tPlaying\t123\t\tSong\n"
        "chromium.instance123\t35000000\t168000000\t1\tPlaying\t\t\tSong | YouTube Music", 1);
    pollMedia(app);
    CHECK(app.musicPlaybackPlayer == "chromium.instance123");
    CHECK(app.media.position == 35 && app.media.length == 168);
    CHECK(app.media.volume == 0.2);
    mediaSend(app, {"next"});
    CHECK(waitpid(-1, nullptr, 0) > 0);
    CHECK(readText(log) == "-p\nchromium.instance123\nnext\n");
    mediaSend(app, {"volume", "0.3"});
    CHECK(waitpid(-1, nullptr, 0) > 0);
    CHECK(readText(log) == "-p\nplasma-browser-integration\nvolume\n0.3\n");

    // Never borrow a clock from another browser or from another song.
    setenv("END4PC_TEST_STATS",
        "plasma-browser-integration\t276000000\t351000000\t0.2\tPlaying\t123\t\tSong\n"
        "chromium.instance999\t35000000\t168000000\t1\tPlaying\t\t\tSong | YouTube Music\n"
        "chromium.instance123\t35000000\t168000000\t1\tPlaying\t\t\tOther song | YouTube Music", 1);
    pollMedia(app);
    CHECK(app.musicPlaybackPlayer == "plasma-browser-integration");
    CHECK(app.media.position == 276);

    // Missing volume is normal for some MPRIS players and must not freeze time.
    mediaplayer::Snapshot noVolume;
    CHECK(mediaplayer::parse("chromium.instance123\t35000000\t168000000\t\tPlaying\t\t\tSong", noVolume));
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
    const std::string lateStats = "plasma-browser-integration\t12000000\t90000000\t0.2\tPlaying\t123\t" + localUrl + "\tSong";
    setenv("END4PC_TEST_STATS", lateStats.c_str(), 1);
    pollMedia(app);
    CHECK(app.musicArtUrl == localUrl && app.musicArt);
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
    app.musicArtUrl.clear();
    updateCover(app, url);
    CHECK(app.musicArt);
    mediaWRefreshCover(app);
    CHECK(app.media.cover && app.media.blur && app.mediaW.blur);
    CHECK(app.media.coverKey == url);

    // Image decoders may keep their own helper process alive until we exit.
    // Do not wait for those unrelated children here.
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    std::printf("Media regression tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
