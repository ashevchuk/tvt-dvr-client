#include "ima_decoder.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

namespace dvr {

ImaDecoder::ImaDecoder()
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_ADPCM_IMA_WAV);
    if (!codec)
        return;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    ctx->sample_rate = kAudioSampleRate;
    ctx->block_align = kBlockBytes;
    ctx->bits_per_coded_sample = 4;
    av_channel_layout_default(&ctx->ch_layout, 1);
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return;
    }
    ctx_ = ctx;
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    block_.resize(kBlockBytes + AV_INPUT_BUFFER_PADDING_SIZE);
}

ImaDecoder::~ImaDecoder()
{
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    avcodec_free_context(&ctx_);
}

void ImaDecoder::decode(const uint8_t *units, size_t size, std::vector<int16_t> &out)
{
    if (!ctx_)
        return;
    for (size_t off = 0; off + kUnitBytes <= size; off += kUnitBytes) {
        memcpy(block_.data(), units + off + kHeaderBytes, kBlockBytes);
        packet_->data = block_.data();
        packet_->size = kBlockBytes;
        if (avcodec_send_packet(ctx_, packet_) < 0)
            continue;
        while (avcodec_receive_frame(ctx_, frame_) == 0) {
            const int16_t *pcm = reinterpret_cast<const int16_t *>(frame_->data[0]);
            out.insert(out.end(), pcm, pcm + frame_->nb_samples);
        }
    }
}

}  // namespace dvr
