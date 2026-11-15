#pragma once
// Minimal MPRIS peers on the test's private bus. Each owns a separate connection
// so assertions cover the real destination, property writes and track IDs.
struct FakePlayer {
    std::string instance, log;
    pid_t pid = -1;

    void record(const std::string& action) {
        std::ofstream(log) << instance << '\n' << action << '\n';
    }
    static int method(sd_bus_message* message, void* data, sd_bus_error*) {
        auto& player = *static_cast<FakePlayer*>(data);
        const std::string member = sd_bus_message_get_member(message);
        if (member == "SetPosition") {
            const char* track = nullptr;
            int64_t position = 0;
            if (sd_bus_message_read(message, "ox", &track, &position) < 0) return -EINVAL;
            player.record("position\n" + std::string(track) + "\n" + std::to_string(position));
        } else player.record(member == "PlayPause" ? "play-pause" : member == "Next" ? "next" : "previous");
        return sd_bus_reply_method_return(message, "");
    }
    static int get(sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply, void*, sd_bus_error*) {
        if (std::string(property) == "Volume") return sd_bus_message_append(reply, "d", 0.2);
        // Include an unrelated variant before trackid to exercise dictionary traversal.
        return sd_bus_message_append(reply, "a{sv}", 2, "xesam:title", "s", "Song", "mpris:trackid", "o", "/test/current_track");
    }
    static int volume(sd_bus*, const char*, const char*, const char*, sd_bus_message* value, void* data, sd_bus_error*) {
        double level = 0;
        if (sd_bus_message_read(value, "d", &level) < 0) return -EINVAL;
        static_cast<FakePlayer*>(data)->record("volume\n" + std::to_string(level));
        return 1;
    }
    FakePlayer(std::string name, const std::filesystem::path& logPath) : instance(std::move(name)), log(logPath) {
        int ready[2];
        if (pipe(ready) < 0) throw std::runtime_error("fake player pipe");
        pid = fork();
        if (pid == 0) {
            close(ready[0]);
            sd_bus* bus = nullptr;
            static const sd_bus_vtable vtable[] = {
                SD_BUS_VTABLE_START(0),
                SD_BUS_METHOD("PlayPause", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
                SD_BUS_METHOD("Next", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
                SD_BUS_METHOD("Previous", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
                SD_BUS_METHOD("SetPosition", "ox", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
                SD_BUS_PROPERTY("Metadata", "a{sv}", get, 0, 0),
                SD_BUS_WRITABLE_PROPERTY("Volume", "d", get, volume, 0, 0),
                SD_BUS_VTABLE_END
            };
            if (sd_bus_open_user(&bus) < 0 ||
                sd_bus_add_object_vtable(bus, nullptr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", vtable, this) < 0 ||
                sd_bus_request_name(bus, ("org.mpris.MediaPlayer2." + instance).c_str(), 0) < 0) _exit(1);
            const char ok = 1;
            if (write(ready[1], &ok, 1) != 1) _exit(1);
            close(ready[1]);
            for (;;) {
                const int result = sd_bus_process(bus, nullptr);
                if (result < 0) _exit(1);
                if (result == 0 && sd_bus_wait(bus, UINT64_MAX) < 0) _exit(1);
            }
        }
        close(ready[1]);
        char ok = 0;
        const ssize_t got = read(ready[0], &ok, 1);
        close(ready[0]);
        if (got != 1 || !ok) throw std::runtime_error("fake player startup");
    }
    ~FakePlayer() { stop(); }
    void stop() {
        if (pid > 0) { kill(pid, SIGTERM); waitpid(pid, nullptr, 0); pid = -1; }
    }
};
