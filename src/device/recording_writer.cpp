#include "recording_writer.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}

namespace dvr {

namespace {

constexpr AVRational kMicroseconds = {1, 1000000};
constexpr int64_t kVideoFrameUs = 40000;
constexpr int kNalSps = 7;
constexpr int kNalPps = 8;
constexpr int kNalAud = 9;

struct Nal {
    const uint8_t *data;
    size_t size;
    int type() const { return data[0] & 0x1f; }
};

std::vector<Nal> split_annex_b(const uint8_t *p, size_t n)
{
    std::vector<Nal> nals;
    auto close = [&](size_t start, size_t end) {
        while (end > start && p[end - 1] == 0)
            end--;
        if (end > start)
            nals.push_back({p + start, end - start});
    };
    long start = -1;
    size_t i = 0;
    while (i + 3 <= n) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            if (start >= 0)
                close(start, i);
            start = static_cast<long>(i + 3);
            i += 3;
        } else {
            i++;
        }
    }
    if (start >= 0)
        close(start, n);
    return nals;
}

void put16(std::vector<uint8_t> &out, size_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

// The avcC record Matroska expects as codec private data.
bool build_avcc(const std::vector<Nal> &nals, std::vector<uint8_t> &out)
{
    const Nal *sps = nullptr, *pps = nullptr;
    for (const Nal &n : nals) {
        if (n.type() == kNalSps && !sps)
            sps = &n;
        if (n.type() == kNalPps && !pps)
            pps = &n;
    }
    if (!sps || !pps || sps->size < 4)
        return false;
    out = {1, sps->data[1], sps->data[2], sps->data[3], 0xFF, 0xE1};
    put16(out, sps->size);
    out.insert(out.end(), sps->data, sps->data + sps->size);
    out.push_back(1);
    put16(out, pps->size);
    out.insert(out.end(), pps->data, pps->data + pps->size);
    return true;
}

std::string av_error(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(buf, sizeof(buf), code);
}

}  // namespace

std::unique_ptr<RecordingWriter> RecordingWriter::create(const std::string &path, bool with_audio, std::string &error)
{
    std::unique_ptr<RecordingWriter> w(new RecordingWriter());
    w->path_ = path;
    w->with_audio_ = with_audio;
    int rc = avformat_alloc_output_context2(&w->format_, nullptr, nullptr, path.c_str());
    if (rc < 0 || !w->format_) {
        error = "cannot choose a container for " + path;
        return nullptr;
    }
    rc = avio_open(&w->format_->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (rc < 0) {
        error = "cannot write " + path + ": " + av_error(rc);
        return nullptr;
    }
    return w;
}

RecordingWriter::~RecordingWriter()
{
    std::string ignored;
    finish(ignored);
}

bool RecordingWriter::start(const uint8_t *key_frame, size_t size, int width, int height, uint64_t time_us)
{
    std::vector<uint8_t> avcc;
    if (!build_avcc(split_annex_b(key_frame, size), avcc))
        return false;

    video_ = avformat_new_stream(format_, nullptr);
    AVCodecParameters *vp = video_->codecpar;
    vp->codec_type = AVMEDIA_TYPE_VIDEO;
    vp->codec_id = AV_CODEC_ID_H264;
    vp->width = width;
    vp->height = height;
    vp->extradata = static_cast<uint8_t *>(av_mallocz(avcc.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    memcpy(vp->extradata, avcc.data(), avcc.size());
    vp->extradata_size = static_cast<int>(avcc.size());
    video_->time_base = kMicroseconds;

    if (with_audio_) {
        audio_ = avformat_new_stream(format_, nullptr);
        AVCodecParameters *ap = audio_->codecpar;
        ap->codec_type = AVMEDIA_TYPE_AUDIO;
        ap->codec_id = AV_CODEC_ID_PCM_S16LE;
        ap->format = AV_SAMPLE_FMT_S16;
        ap->sample_rate = kAudioSampleRate;
        av_channel_layout_default(&ap->ch_layout, 1);
        ap->bits_per_coded_sample = 16;
        ap->block_align = 2;
        ap->bit_rate = kAudioSampleRate * 16;
        audio_->time_base = kMicroseconds;
    }
    int rc = avformat_write_header(format_, nullptr);
    if (rc < 0) {
        failed_ = true;
        return false;
    }
    started_ = true;
    first_us_ = time_us;
    return true;
}

void RecordingWriter::write(AVStream *stream, std::vector<uint8_t> &data, int64_t pts_us, int64_t duration_us, bool key)
{
    AVPacket *pkt = av_packet_alloc();
    if (av_new_packet(pkt, static_cast<int>(data.size())) < 0) {
        av_packet_free(&pkt);
        failed_ = true;
        return;
    }
    memcpy(pkt->data, data.data(), data.size());
    pkt->stream_index = stream->index;
    pkt->pts = pkt->dts = pts_us;
    pkt->duration = duration_us;
    if (key)
        pkt->flags |= AV_PKT_FLAG_KEY;
    av_packet_rescale_ts(pkt, kMicroseconds, stream->time_base);
    if (av_interleaved_write_frame(format_, pkt) < 0)
        failed_ = true;
    av_packet_free(&pkt);
}

void RecordingWriter::add_video(const uint8_t *data, size_t size, bool key, int width, int height, uint64_t time_us)
{
    if (failed_ || finished_)
        return;
    if (!started_ && !(key && start(data, size, width, height, time_us)))
        return;
    int64_t pts = std::max<int64_t>(static_cast<int64_t>(time_us - first_us_), last_video_pts_ + 1);
    std::vector<uint8_t> packet;
    for (const Nal &nal : split_annex_b(data, size)) {
        int type = nal.type();
        if (type == kNalSps || type == kNalPps || type == kNalAud)
            continue;
        for (int shift = 24; shift >= 0; shift -= 8)
            packet.push_back(static_cast<uint8_t>(nal.size >> shift));
        packet.insert(packet.end(), nal.data, nal.data + nal.size);
    }
    if (packet.empty())
        return;
    write(video_, packet, pts, kVideoFrameUs, key);
    last_video_pts_ = pts;
    video_frames_++;
}

void RecordingWriter::add_audio(const uint8_t *units, size_t size, uint64_t time_us)
{
    if (failed_ || finished_ || !started_ || !audio_ || time_us < first_us_)
        return;
    pcm_.clear();
    decoder_.decode(units, size, pcm_);
    if (pcm_.empty())
        return;
    int64_t pts = std::max<int64_t>(static_cast<int64_t>(time_us - first_us_), last_audio_pts_ + 1);
    std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t *>(pcm_.data()),
                               reinterpret_cast<const uint8_t *>(pcm_.data()) + pcm_.size() * sizeof(int16_t));
    write(audio_, bytes, pts, static_cast<int64_t>(pcm_.size()) * 1000000 / kAudioSampleRate, true);
    last_audio_pts_ = pts;
}

bool RecordingWriter::finish(std::string &error)
{
    if (finished_)
        return !failed_;
    finished_ = true;
    bool wrote = started_ && video_frames_ > 0;
    if (started_)
        av_write_trailer(format_);
    if (format_) {
        avio_closep(&format_->pb);
        avformat_free_context(format_);
        format_ = nullptr;
    }
    if (!wrote)
        error = "no video was recorded";
    else if (failed_)
        error = "writing " + path_ + " failed";
    return wrote && !failed_;
}

}  // namespace dvr
