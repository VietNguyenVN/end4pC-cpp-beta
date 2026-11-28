// The audio engine of Play3r. See engine.h.
#include "engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
}
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tvariant.h>

namespace player {

namespace {
constexpr int kRate = 48000, kChannels = 2;
constexpr size_t kRingFrames = 1 << 17;  // about 2.7 seconds: enough to ride out a busy moment, little memory (1 MB)
constexpr size_t kChunk = 4096;          // frames the decoder writes at once

std::string stem(const std::string& path) { return std::filesystem::path(path).stem().string(); }

// A sample ring shared by the decoder (writes) and the output (reads). A short mutex is enough here: both sides hold it for a copy.
struct Ring {
    std::vector<float> data = std::vector<float>(kRingFrames * kChannels);
    size_t head = 0, tail = 0, count = 0;  // in frames
    size_t free() const { return kRingFrames - count; }
    void push(const float* in, size_t frames) {
        for (size_t i = 0; i < frames; i++) {
            std::memcpy(&data[head * kChannels], in + i * kChannels, sizeof(float) * kChannels);
            head = (head + 1) % kRingFrames;
        }
        count += frames;
    }
    size_t pop(float* out, size_t frames) {
        const size_t n = std::min(frames, count);
        for (size_t i = 0; i < n; i++) {
            std::memcpy(out + i * kChannels, &data[tail * kChannels], sizeof(float) * kChannels);
            tail = (tail + 1) % kRingFrames;
        }
        count -= n;
        return n;
    }
    void clear() { head = tail = count = 0; }
};

struct Decoder {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    int stream = -1;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    ~Decoder() { close(); }
    void close() {
        if (swr) swr_free(&swr);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codec) avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
        stream = -1;
    }
    bool makeResampler() {
        if (swr) swr_free(&swr);
        AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLT, kRate, &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0) return false;
        return swr_init(swr) >= 0;
    }
    bool open(const std::string& path, std::string& error) {
        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) { error = "cannot open the file"; return false; }
        if (avformat_find_stream_info(fmt, nullptr) < 0) { error = "cannot read the file"; return false; }
        const AVCodec* dec = nullptr;
        stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
        if (stream < 0 || !dec) { error = "no audio in the file"; return false; }
        codec = avcodec_alloc_context3(dec);
        if (!codec || avcodec_parameters_to_context(codec, fmt->streams[stream]->codecpar) < 0 || avcodec_open2(codec, dec, nullptr) < 0) { error = "cannot decode the audio"; return false; }
        if (!makeResampler()) { error = "cannot convert the audio"; return false; }
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        return packet && frame;
    }
    double length() const { return fmt && fmt->duration > 0 ? fmt->duration / double(AV_TIME_BASE) : 0; }
    bool seek(double seconds) {
        if (!fmt) return false;
        const int64_t target = static_cast<int64_t>(std::max(0.0, seconds) * AV_TIME_BASE);
        if (avformat_seek_file(fmt, -1, INT64_MIN, target, INT64_MAX, 0) < 0) return false;
        avcodec_flush_buffers(codec);
        makeResampler();
        return true;
    }
};
}  // namespace

struct Engine::Impl {
    Output output;
    double nullSpeed;
    // The ring and the clock.
    std::mutex mutex;
    std::condition_variable room;
    Ring ring;
    uint64_t generation = 0;  // grows with every seek or open: samples decoded for an older one are dropped
    double base = 0;          // the second of the track at frame 0 of the count below
    uint64_t consumed = 0;    // frames played since base
    bool decoderDone = false, ended = false;
    // The decoder thread.
    std::thread decoderThread;
    std::atomic<bool> quit{false};
    std::unique_ptr<Decoder> decoder;
    double seekTo = -1;
    // The outputs.
    std::atomic<bool> playing{false};
    pw_thread_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    spa_hook streamHook{};
    std::thread nullThread;
    Engine* owner = nullptr;

    // ---- the output side: pulls samples, applies the volume, counts what was heard -------------------------------------------------
    size_t pull(float* out, size_t frames) {
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        size_t got = 0;
        if (lock.owns_lock() && playing.load()) {
            got = ring.pop(out, frames);
            consumed += got;
            if (got < frames && decoderDone && ring.count == 0) ended = true;
            lock.unlock();
            room.notify_one();
        }
        const float v = owner->volume_.load(), gain = v * v;  // a squared curve feels more even than a linear one
        for (size_t i = 0; i < got * kChannels; i++) out[i] *= gain;
        if (got < frames) std::memset(out + got * kChannels, 0, sizeof(float) * (frames - got) * kChannels);
        return frames;
    }
    static void onProcess(void* data) {
        auto* self = static_cast<Impl*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
        if (!b) return;
        spa_data& d = b->buffer->datas[0];
        auto* dst = static_cast<float*>(d.data);
        if (!dst) { pw_stream_queue_buffer(self->stream, b); return; }
        const size_t stride = sizeof(float) * kChannels;
        size_t frames = d.maxsize / stride;
        if (b->requested) frames = std::min<size_t>(frames, b->requested);
        self->pull(dst, frames);
        d.chunk->offset = 0;
        d.chunk->stride = stride;
        d.chunk->size = frames * stride;
        pw_stream_queue_buffer(self->stream, b);
    }

    bool startOutput() {
        if (output == Output::Null) {
            nullThread = std::thread([this] {
                std::vector<float> scratch(kChunk * kChannels);
                while (!quit.load()) {
                    pull(scratch.data(), kChunk);
                    std::this_thread::sleep_for(std::chrono::duration<double>(kChunk / double(kRate) / std::max(0.01, nullSpeed)));
                }
            });
            return true;
        }
        static std::once_flag once;
        std::call_once(once, [] { pw_init(nullptr, nullptr); });
        loop = pw_thread_loop_new("player", nullptr);
        if (!loop) return false;
        static const pw_stream_events events = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.process = onProcess;
            return e;
        }();
        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
                                                 PW_KEY_APP_NAME, "Play3r", PW_KEY_NODE_NAME, "Play3r", PW_KEY_NODE_LATENCY, "2048/48000", nullptr);
        stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), "Play3r", props, &events, this);
        if (!stream) return false;
        uint8_t buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof buffer);
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = kRate;
        info.channels = kChannels;
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod* params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
        if (pw_stream_connect(stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                              static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1) < 0)
            return false;
        pw_stream_set_active(stream, false);
        return pw_thread_loop_start(loop) == 0;
    }
    void stopOutput() {
        if (nullThread.joinable()) nullThread.join();
        if (loop) {
            pw_thread_loop_stop(loop);
            if (stream) pw_stream_destroy(stream);
            pw_thread_loop_destroy(loop);
            stream = nullptr;
            loop = nullptr;
        }
    }
    void setActive(bool on) {
        if (!loop || !stream) return;
        pw_thread_loop_lock(loop);
        pw_stream_set_active(stream, on);
        pw_thread_loop_unlock(loop);
    }

    // ---- the decoder side -------------------------------------------------------------------------------------------------------------
    void decodeLoop() {
        std::vector<float> out;
        while (!quit.load()) {
            uint64_t gen;
            double target;
            {
                std::unique_lock<std::mutex> lock(mutex);
                room.wait_for(lock, std::chrono::milliseconds(100), [&] { return quit.load() || seekTo >= 0 || (!decoderDone && ring.free() >= kChunk * 2); });
                if (quit.load()) return;
                gen = generation;
                target = seekTo;
                seekTo = -1;
            }
            if (!decoder) continue;
            if (target >= 0) {
                decoder->seek(target);
                std::lock_guard<std::mutex> lock(mutex);
                decoderDone = false;
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (decoderDone || ring.free() < kChunk * 2) continue;
            }
            if (!decodeSome(out, gen)) {
                std::lock_guard<std::mutex> lock(mutex);
                if (gen == generation) decoderDone = true;
            }
        }
    }
    // Decodes one packet and queues its samples. False at the end of the file.
    bool decodeSome(std::vector<float>& out, uint64_t gen) {
        Decoder& d = *decoder;
        const int r = av_read_frame(d.fmt, d.packet);
        const bool eof = r < 0;
        if (!eof && d.packet->stream_index != d.stream) { av_packet_unref(d.packet); return true; }
        if (avcodec_send_packet(d.codec, eof ? nullptr : d.packet) < 0 && !eof) { av_packet_unref(d.packet); return true; }
        if (!eof) av_packet_unref(d.packet);
        while (avcodec_receive_frame(d.codec, d.frame) == 0) {
            const int maxOut = swr_get_out_samples(d.swr, d.frame->nb_samples) + 64;
            out.resize(static_cast<size_t>(maxOut) * kChannels);
            uint8_t* planes[1] = {reinterpret_cast<uint8_t*>(out.data())};
            const int n = swr_convert(d.swr, planes, maxOut, const_cast<const uint8_t**>(d.frame->extended_data), d.frame->nb_samples);
            av_frame_unref(d.frame);
            if (n <= 0) continue;
            size_t done = 0;
            while (done < static_cast<size_t>(n) && !quit.load()) {
                std::unique_lock<std::mutex> lock(mutex);
                if (gen != generation) return true;  // a seek happened: this audio is from before it
                const size_t step = std::min<size_t>(n - done, ring.free());
                if (step == 0) { room.wait_for(lock, std::chrono::milliseconds(50)); continue; }
                ring.push(out.data() + done * kChannels, step);
                done += step;
            }
        }
        return !eof;
    }
};

Engine::Engine(Output output, double nullSpeed) : impl_(new Impl) {
    impl_->output = output;
    impl_->nullSpeed = nullSpeed;
    impl_->owner = this;
}

Engine::~Engine() {
    impl_->quit = true;
    impl_->room.notify_all();
    if (impl_->decoderThread.joinable()) impl_->decoderThread.join();
    impl_->stopOutput();
}

bool Engine::open(const std::string& path, bool autoplay) {
    Impl& s = *impl_;
    error_.clear();
    av_log_set_level(AV_LOG_ERROR);  // (ffmpeg chats about every odd MP3 frame: the tags and the sound are fine)
    // Stop the old track: the decoder thread is parked by the generation change and replaced.
    s.quit = true;
    s.room.notify_all();
    if (s.decoderThread.joinable()) s.decoderThread.join();
    s.quit = false;
    auto fresh = std::make_unique<Decoder>();
    if (!fresh->open(path, error_)) { state_ = State::Stopped; duration_ = 0; return false; }
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.decoder = std::move(fresh);
        s.ring.clear();
        s.generation++;
        s.base = 0;
        s.consumed = 0;
        s.decoderDone = s.ended = false;
        s.seekTo = -1;
    }
    duration_ = s.decoder->length();
    {
        const Decoder& d = *s.decoder;
        stream_.codec = avcodec_get_name(d.codec->codec_id);
        int64_t bits = d.codec->bit_rate > 0 ? d.codec->bit_rate : d.fmt->streams[d.stream]->codecpar->bit_rate;
        if (bits <= 0) bits = d.fmt->bit_rate;
        stream_.bitrateKbps = static_cast<int>(bits / 1000);
        stream_.sampleRate = d.codec->sample_rate;
        stream_.channels = d.codec->ch_layout.nb_channels;
    }
    if (!s.loop && !s.nullThread.joinable() && !s.startOutput()) { error_ = "cannot open the audio output"; return false; }
    s.decoderThread = std::thread([&s] { s.decodeLoop(); });
    s.playing = autoplay;
    s.setActive(autoplay);
    state_ = autoplay ? State::Playing : State::Paused;
    return true;
}

void Engine::play() {
    if (state_ == State::Stopped) return;
    impl_->playing = true;
    impl_->setActive(true);
    state_ = State::Playing;
}

void Engine::pause() {
    if (state_ != State::Playing) return;
    impl_->playing = false;
    impl_->setActive(false);
    state_ = State::Paused;
}

void Engine::toggle() {
    if (state_ == State::Playing) pause();
    else play();
}

void Engine::stop() {
    Impl& s = *impl_;
    s.playing = false;
    s.setActive(false);
    std::lock_guard<std::mutex> lock(s.mutex);
    s.ring.clear();
    s.generation++;
    s.base = 0;
    s.consumed = 0;
    s.seekTo = 0;
    s.decoderDone = false;
    state_ = State::Stopped;
}

void Engine::seek(double seconds) {
    Impl& s = *impl_;
    if (state_ == State::Stopped) return;
    const double len = duration_.load();
    seconds = std::max(0.0, len > 0 ? std::min(seconds, len - 0.05) : seconds);
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.ring.clear();
        s.generation++;
        s.base = seconds;
        s.consumed = 0;
        s.seekTo = seconds;
        s.decoderDone = s.ended = false;
    }
    s.room.notify_all();
}

void Engine::setVolume(float volume) { volume_ = std::clamp(volume, 0.0f, 1.0f); }

double Engine::position() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->base + impl_->consumed / double(kRate);
}

bool Engine::takeEnded() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const bool e = impl_->ended;
    if (e) {
        impl_->ended = false;
        state_ = State::Stopped;
        impl_->playing = false;
    }
    return e;
}

// ---- tags -------------------------------------------------------------------------------------------------------------------------------
TrackInfo readTags(const std::string& path, bool withCover) {
    TrackInfo info;
    TagLib::FileRef file(path.c_str(), true, TagLib::AudioProperties::Fast);
    if (!file.isNull()) {
        if (file.tag()) {
            info.title = file.tag()->title().to8Bit(true);
            info.artist = file.tag()->artist().to8Bit(true);
            info.album = file.tag()->album().to8Bit(true);
        }
        if (file.audioProperties()) info.duration = file.audioProperties()->lengthInMilliseconds() / 1000.0;
        const auto pictures = withCover ? file.complexProperties("PICTURE") : TagLib::List<TagLib::VariantMap>();
        for (const auto& picture : pictures) {
            const auto it = picture.find("data");
            if (it == picture.end()) continue;
            const TagLib::ByteVector bytes = it->second.value<TagLib::ByteVector>();
            if (bytes.isEmpty()) continue;
            info.cover.assign(bytes.data(), bytes.data() + bytes.size());
            break;
        }
    }
    if (info.title.empty()) info.title = stem(path);
    return info;
}

}  // namespace player
