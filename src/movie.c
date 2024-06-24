/* Copyright (C) 2023 kichikuou <KichikuouChrome@gmail.com>
 * Copyright (C) 2024 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "nulib.h"
#include "nulib/file.h"
#include "ai5/arc.h"

#include "gfx_private.h"
#include "mixer.h"
#include "movie.h"
#include "sts_mixer.h"
#include "vm.h"

#define QUEUE_SIZE 10

struct decoder {
	AVFormatContext *format_ctx;
	AVStream *stream;
	AVCodecContext *ctx;
	AVFrame *frame;
	AVFifoBuffer *queue;
	SDL_mutex *mutex;
	bool finished;
	bool format_eof;
};

struct movie_context {
	struct decoder video;
	struct decoder audio;

	SDL_Texture *dst;

	bool has_pending_video_frame;
	struct SwsContext *sws_ctx;
	AVFrame *sws_frame;
	void *sws_buf;
	// number of ms that the video stream has been rewound (independent of audio)
	double video_rewind_time;
	unsigned video_current_frame;

	sts_mixer_stream_t sts_stream;
	int bytes_per_sample;
	int voice;
	int volume;
	bool interleaved;

	// Time keeping data. Written by audio handler and referenced by video
	// handler (i.e. we sync the video to the audio).
	double stream_time; // in seconds
	uint32_t wall_time_ms;
	SDL_mutex *timer_mutex;
};

static void free_decoder(struct decoder *dec)
{
	if (dec->format_ctx)
		avformat_close_input(&dec->format_ctx);
	if (dec->ctx)
		avcodec_free_context(&dec->ctx);
	if (dec->frame)
		av_frame_free(&dec->frame);
	if (dec->queue) {
		while (av_fifo_size(dec->queue) > 0) {
			AVPacket *packet;
			av_fifo_generic_read(dec->queue, &packet, sizeof(AVPacket*), NULL);
			av_packet_free(&packet);
		}
		av_fifo_freep(&dec->queue);
	}
	if (dec->mutex)
		SDL_DestroyMutex(dec->mutex);
}

static bool init_decoder(struct decoder *dec, AVFormatContext *format_ctx, AVStream *stream)
{
	dec->format_ctx = format_ctx;
	dec->stream = stream;
	if (!format_ctx || !stream)
		return false;

	const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!codec)
		return false;
	dec->ctx = avcodec_alloc_context3(codec);
	if (avcodec_parameters_to_context(dec->ctx, stream->codecpar) < 0)
		return false;
	if (avcodec_open2(dec->ctx, codec, NULL) != 0)
		return false;
	dec->frame = av_frame_alloc();
	dec->queue = av_fifo_alloc(sizeof(AVPacket*) * QUEUE_SIZE);
	dec->mutex = SDL_CreateMutex();
	return true;
}

static void read_packet(struct decoder *dec)
{
	if (dec->format_eof)
		return;

	AVPacket *packet = av_packet_alloc();
	while (av_read_frame(dec->format_ctx, packet) == 0) {
		if (packet->stream_index == dec->stream->index) {
			if (av_fifo_space(dec->queue) < (int)sizeof(AVPacket*))
				av_fifo_grow(dec->queue, sizeof(AVPacket*) * QUEUE_SIZE);
			av_fifo_generic_write(dec->queue, &packet, sizeof(AVPacket*), NULL);
			return;
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	packet = NULL;
	dec->format_eof = true;
	av_fifo_generic_write(dec->queue, &packet, sizeof(AVPacket*), NULL);
}

static void _preload_packets(struct decoder *dec)
{
	SDL_LockMutex(dec->mutex);
	while (!dec->format_eof && av_fifo_size(dec->queue) < (int)(QUEUE_SIZE * sizeof(AVPacket*))) {
		read_packet(dec);
	}
	SDL_UnlockMutex(dec->mutex);
}

static void preload_packets(struct movie_context *mc)
{
	if (mc->audio.stream)
		_preload_packets(&mc->audio);
	_preload_packets(&mc->video);
}

static bool decode_frame(struct decoder *dec)
{
	int ret;
	while ((ret = avcodec_receive_frame(dec->ctx, dec->frame)) == AVERROR(EAGAIN)) {
		SDL_LockMutex(dec->mutex);
		AVPacket *packet;
		while (av_fifo_size(dec->queue) == 0)
			read_packet(dec);
		av_fifo_generic_read(dec->queue, &packet, sizeof(AVPacket*), NULL);
		SDL_UnlockMutex(dec->mutex);

		if ((ret = avcodec_send_packet(dec->ctx, packet)) != 0) {
			WARNING("avcodec_send_packet failed: %d", ret);
			return false;
		}
		av_packet_free(&packet);
	}
	if (ret == AVERROR_EOF)
		dec->finished = true;
	else if (ret)
		WARNING("avcodec_receive_frame failed: %d", ret);
	return ret == 0;
}

#ifndef USE_SDL_MIXER
static int audio_callback(sts_mixer_sample_t *sample, void *data)
{
	struct movie_context *mc = data;
	assert(sample == &mc->sts_stream.sample);

	if (!decode_frame(&mc->audio)) {
		free(sample->data);
		sample->length = 0;
		sample->data = NULL;
		mc->voice = -1;
		return STS_STREAM_COMPLETE;
	}

	const int nr_channels = 2;
	unsigned samples = 0;
	if (mc->interleaved) {
		samples = mc->audio.frame->linesize[0] / (mc->bytes_per_sample * 2);
		if (samples * nr_channels != sample->length) {
			free(sample->data);
			sample->length = samples * nr_channels;
			sample->data = xmalloc(sample->length * mc->bytes_per_sample);
		}
		memcpy(sample->data, mc->audio.frame->data[0], samples * mc->bytes_per_sample * 2);
	} else {
		samples = mc->audio.frame->linesize[0] / mc->bytes_per_sample;
		if (samples * nr_channels != sample->length) {
			free(sample->data);
			sample->length = samples * nr_channels;
			sample->data = xmalloc(sample->length * mc->bytes_per_sample);
		}
		// Interleave.
		uint8_t *l = mc->audio.frame->data[0];
		uint8_t *r = mc->audio.frame->data[mc->audio.ctx->channels > 1 ? 1 : 0];
		uint8_t *out = sample->data;
		for (unsigned i = 0; i < samples; i++) {
			memcpy(out, l, mc->bytes_per_sample);
			out += mc->bytes_per_sample;
			l += mc->bytes_per_sample;
			memcpy(out, r, mc->bytes_per_sample);
			out += mc->bytes_per_sample;
			r += mc->bytes_per_sample;
		}
	}

	// Update the timestamp.
	SDL_LockMutex(mc->timer_mutex);
	mc->stream_time += (double)samples / mc->audio.ctx->sample_rate;
	mc->wall_time_ms = SDL_GetTicks();
	SDL_UnlockMutex(mc->timer_mutex);
	return STS_STREAM_CONTINUE;
}
#endif

static AVStream *open_stream(AVFormatContext *ctx, unsigned type)
{
	if (avformat_find_stream_info(ctx, NULL) < 0) {
		WARNING("avformat_find_stream_info failed");
		return NULL;
	}

	for (int i = 0; i < (int)ctx->nb_streams; ++i) {
		if (ctx->streams[i]->codecpar->codec_type == type)
			return ctx->streams[i];
	}
	WARNING("Couldn't find stream");
	return NULL;
}

struct movie_context *_movie_load(struct movie_context *mc, AVFormatContext *video_ctx,
		AVFormatContext *audio_ctx, int w, int h)
{
	if (w <= 0 || h <= 0) {
		WARNING("Invalid dimensions: %dx%d", w, h);
		goto error;
	}

	// initialize video decoder
	AVStream *video_stream = open_stream(video_ctx, AVMEDIA_TYPE_VIDEO);
	if (!init_decoder(&mc->video, video_ctx, video_stream)) {
		WARNING("Cannot initialize video decoder");
		goto error;
	}
	NOTICE("video: %d x %d, %s", mc->video.ctx->width, mc->video.ctx->height,
			av_get_pix_fmt_name(mc->video.ctx->pix_fmt));

	// initialize audio decoder
	if (audio_ctx) {
		AVStream *audio_stream = open_stream(audio_ctx, AVMEDIA_TYPE_AUDIO);
		if (!init_decoder(&mc->audio, audio_ctx, audio_stream)) {
			WARNING("Cannot initialize audio decoder");
			goto error;
		}
		NOTICE("audio: %d hz, %d channels, %s", mc->audio.ctx->sample_rate,
				mc->audio.ctx->channels,
				av_get_sample_fmt_name(mc->audio.ctx->sample_fmt));
	}
	else {
		NOTICE("audio: none");
	}

	// initialize swscale
	mc->sws_ctx = sws_getContext(mc->video.ctx->width, mc->video.ctx->height,
			mc->video.ctx->pix_fmt, w, h, AV_PIX_FMT_RGBA, SWS_BICUBIC,
			NULL, NULL, NULL);
	if (!mc->sws_ctx) {
		WARNING("sws_getContext failed");
		goto error;
	}
	if (!(mc->sws_frame = av_frame_alloc())) {
		WARNING("av_frame_alloc failed");
		goto error;
	}
	int size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 1);
	if (size < 0) {
		WARNING("av_image_get_buffer_size failed");
		goto error;
	}

	if (!(mc->sws_buf = av_malloc(size))) {
		WARNING("av_malloc failed");
		goto error;
	}
	if (av_image_fill_arrays(mc->sws_frame->data, mc->sws_frame->linesize, mc->sws_buf,
			AV_PIX_FMT_RGBA, w, h, 1) < 0) {
		WARNING("av_image_fill_arrays failed");
		goto error;
	}

	mc->timer_mutex = SDL_CreateMutex();
	mc->volume = 100;

	preload_packets(mc);
	return mc;
error:
	movie_free(mc);
	return NULL;
}

static AVFormatContext *open_input_file(const char *path)
{
	AVFormatContext *format_ctx = NULL;
	if (avformat_open_input(&format_ctx, path, NULL, NULL) < 0) {
		WARNING("avformat_open_input(\"%s\") failed", path);
		return NULL;
	}
	return format_ctx;
}

struct movie_context *movie_load(const char *movie_path, const char *audio_path,
		int w, int h)
{
	struct movie_context *mc = xcalloc(1, sizeof(struct movie_context));
	mc->voice = -1;

#ifdef USE_SDL_MIXER
	audio_path = NULL;
#endif

	AVFormatContext *video_ctx = open_input_file(movie_path);
	AVFormatContext *audio_ctx = audio_path ? open_input_file(audio_path) : NULL;
	if (!video_ctx) {
		movie_free(mc);
		return NULL;
	}
	return _movie_load(mc, video_ctx, audio_ctx, w, h);
}

struct arc_buffer {
	struct archive_data *file;
	size_t pos;
};

static struct arc_buffer video_arc_buffer = {0};
static struct arc_buffer audio_arc_buffer = {0};

static int arc_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
	struct arc_buffer *arcbuf = opaque;
	buf_size = FFMIN(buf_size, arcbuf->file->size - arcbuf->pos);

	if (!buf_size)
		return AVERROR_EOF;

	memcpy(buf, arcbuf->file->data + arcbuf->pos, buf_size);
	arcbuf->pos += buf_size;
	return buf_size;
}

static int64_t arc_seek(void *opaque, int64_t offset, int whence)
{
	struct arc_buffer *arcbuf = opaque;
	if (whence & AVSEEK_SIZE) {
		return arcbuf->file->size;
	}

	int64_t new_off = 0;
	if (whence == SEEK_CUR)
		new_off = arcbuf->pos + offset;
	else if (whence == SEEK_SET)
		new_off = offset;
	else if (whence == SEEK_END)
		new_off = arcbuf->file->size + offset;
	else
		return AVERROR(EINVAL);

	if (new_off < 0 || new_off > arcbuf->file->size)
		return AVERROR(EINVAL);

	arcbuf->pos = new_off;
	return arcbuf->pos;
}

static AVFormatContext *open_input_arc(struct arc_buffer *arcbuf)
{
	AVFormatContext *fmt_ctx = NULL;
	AVIOContext *avio_ctx = NULL;
	uint8_t *avio_ctx_buffer = NULL;

	if (!(fmt_ctx = avformat_alloc_context())) {
		WARNING("avformat_alloc_context failed");
		return NULL;
	}

	if (!(avio_ctx_buffer = av_malloc(4096))) {
		WARNING("av_malloc failed");
		goto error;
	}

	avio_ctx = avio_alloc_context(avio_ctx_buffer, 4096, 0, arcbuf, arc_read_packet,
			NULL, arc_seek);
	if (!avio_ctx) {
		WARNING("avio_alloc_context failed");
		goto error;
	}
	fmt_ctx->pb = avio_ctx;

	if (avformat_open_input(&fmt_ctx, NULL, NULL, NULL) < 0) {
		WARNING("avformat_open_input failed");
		goto error;
	}
	return fmt_ctx;
error:
	if (fmt_ctx)
		avformat_close_input(&fmt_ctx);
	if (avio_ctx) {
		av_freep(&avio_ctx->buffer);
		avio_context_free(&avio_ctx);
	}
	return NULL;
}

struct movie_context *movie_load_arc(struct archive_data *video, struct archive_data *audio,
		int w, int h)
{
	struct movie_context *mc = xcalloc(1, sizeof(struct movie_context));
	mc->voice = -1;

#ifdef USE_SDL_MIXER
	audio = NULL;
#endif

	video_arc_buffer.file = video;
	video_arc_buffer.pos = 0;
	audio_arc_buffer.file = audio;
	audio_arc_buffer.pos = 0;

	AVFormatContext *video_ctx = open_input_arc(&video_arc_buffer);
	AVFormatContext *audio_ctx = audio ? open_input_arc(&audio_arc_buffer) : NULL;
	if (!video_ctx) {
		movie_free(mc);
		return NULL;
	}
	return _movie_load(mc, video_ctx, audio_ctx, w, h);
}

void movie_free(struct movie_context *mc)
{
	if (mc->dst)
		SDL_DestroyTexture(mc->dst);
#ifndef USE_SDL_MIXER
	if (mc->voice >= 0)
		mixer_sts_stream_stop(mc->voice);
	if (mc->sts_stream.sample.data)
		free(mc->sts_stream.sample.data);
#endif

	if (mc->timer_mutex)
		SDL_DestroyMutex(mc->timer_mutex);

	free_decoder(&mc->video);
	free_decoder(&mc->audio);

	if (mc->sws_ctx)
		sws_freeContext(mc->sws_ctx);
	if (mc->sws_frame)
		av_frame_free(&mc->sws_frame);
	if (mc->sws_buf)
		av_free(mc->sws_buf);
	free(mc);
}

int movie_draw(struct movie_context *mc)
{
	// Decode a frame, unless we already have one.
	if (!mc->has_pending_video_frame && !decode_frame(&mc->video))
		return mc->video.finished ? 0 : -1;

	// Get frame timestamp
	double pts = av_q2d(mc->video.stream->time_base) * mc->video.frame->best_effort_timestamp + mc->video_rewind_time;

	// Get current time (and update stream time if no audio stream)
	SDL_LockMutex(mc->timer_mutex);
	unsigned now_ms = SDL_GetTicks();
	double now = mc->stream_time + (now_ms - mc->wall_time_ms) / 1000.0;
	if (!mc->audio.stream) {
		mc->stream_time = now;
		mc->wall_time_ms = now_ms;
	}
	SDL_UnlockMutex(mc->timer_mutex);

	// If timestamp is in the future, save frame and return.
	if (pts > now) {
		mc->has_pending_video_frame = true;
		// We have spare time, let's buffer some packets.
		preload_packets(mc);
		return 0;
	}

	// Convert to RGB and update the texture.
	sws_scale(mc->sws_ctx, (const uint8_t **)mc->video.frame->data, mc->video.frame->linesize,
		 0, mc->video.ctx->height, mc->sws_frame->data, mc->sws_frame->linesize);
	SDL_CALL(SDL_UpdateTexture, mc->dst, NULL, mc->sws_frame->data[0], mc->sws_frame->linesize[0]);
	SDL_CALL(SDL_RenderClear, gfx.renderer);
	SDL_CALL(SDL_RenderCopy, gfx.renderer, mc->dst, NULL, NULL);
	mc->video_current_frame = mc->video.frame->best_effort_timestamp;
	mc->has_pending_video_frame = false;
	return 1;
}

uint8_t *movie_get_pixels(struct movie_context *mc, unsigned *stride)
{
	if (!mc->sws_frame->data[0])
		return NULL;
	*stride = mc->sws_frame->linesize[0];
	return mc->sws_frame->data[0];
}

/*
 * Seek video stream independently of audio stream.
 */
bool movie_seek_video(struct movie_context *mc, unsigned ts)
{
	SDL_LockMutex(mc->video.mutex);
	if (av_seek_frame(mc->video.format_ctx, mc->video.stream->index, ts, AVSEEK_FLAG_ANY) < 0) {
		WARNING("av_seek_frame failed");
		SDL_UnlockMutex(mc->video.mutex);
		return false;
	}
	int diff = (int)mc->video_current_frame - (int)ts;
	mc->video_rewind_time += av_q2d(mc->video.stream->time_base) * diff;
	mc->video_current_frame = ts;
	mc->has_pending_video_frame = false;

	// flush queued frames
	int size = av_fifo_size(mc->video.queue);
	if (size > 0)
		av_fifo_drain(mc->video.queue, size);
	mc->video.format_eof = false;

	SDL_UnlockMutex(mc->video.mutex);

	return true;
}

bool movie_is_end(struct movie_context *mc)
{
	return mc->video.finished && mc->audio.finished;
}

bool movie_play(struct movie_context *mc)
{
	// Start the audio stream.
	mc->stream_time = 0.0;
	mc->wall_time_ms = SDL_GetTicks();

#ifndef USE_SDL_MIXER
	if (mc->audio.stream) {
		mc->sts_stream.userdata = mc;
		mc->sts_stream.callback = audio_callback;
		mc->sts_stream.sample.frequency = mc->audio.ctx->sample_rate;
		switch (mc->audio.ctx->sample_fmt) {
		case AV_SAMPLE_FMT_S16:
			mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_16;
			mc->bytes_per_sample = 2;
			mc->interleaved = true;
			break;
		case AV_SAMPLE_FMT_S16P:
			mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_16;
			mc->bytes_per_sample = 2;
			mc->interleaved = false;
			break;
		case AV_SAMPLE_FMT_S32P:
			mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_32;
			mc->bytes_per_sample = 4;
			mc->interleaved = false;
			break;
		case AV_SAMPLE_FMT_FLTP:
			mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
			mc->bytes_per_sample = 4;
			mc->interleaved = false;
			break;
		default:
			WARNING("Unsupported audio format %d", mc->audio.ctx->sample_fmt);
			return false;
		}
		mc->voice = mixer_sts_stream_play(&mc->sts_stream, mc->volume);
	}
#endif

	if (!mc->audio.stream)
		mc->audio.finished = true;

	SDL_CTOR(SDL_CreateTexture, mc->dst, gfx.renderer, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STATIC,
			mc->video.ctx->width, mc->video.ctx->height);

	return true;
}

int movie_get_position(struct movie_context *mc)
{
	SDL_LockMutex(mc->timer_mutex);
	int ms = mc->wall_time_ms ? mc->stream_time * 1000 + SDL_GetTicks() - mc->wall_time_ms : 0;
	SDL_UnlockMutex(mc->timer_mutex);
	return ms;
}

bool movie_set_volume(struct movie_context *mc, int volume)
{
#ifndef USE_SDL_MIXER
	mc->volume = volume;
	if (mc->voice >= 0)
		mixer_sts_stream_set_volume(mc->voice, volume);
#endif
	return true;
}
