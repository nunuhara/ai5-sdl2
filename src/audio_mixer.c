/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdlib.h>
#include <stdatomic.h>
#include <sndfile.h>
#include <SDL.h>

#include "nulib.h"
#include "ai5/arc.h"

#include "asset.h"
#include "mixer.h"

#define muldiv(x, y, denom) ((int64_t)(x) * (int64_t)(y) / (int64_t)(denom))

/*
 * The actual mixer implementation is contained in this header.
 * The rest of this file implements a high level interface for managing mixer
 * channels (loading audio, starting/stopping, etc.)
 */
#define STS_MIXER_IMPLEMENTATION
#include "sts_mixer.h"

#define CHUNK_SIZE 1024

struct fade {
	atomic_bool fading;
	bool stop;
	uint_least32_t frames;
	uint_least32_t elapsed;
	float start_volume;
	float end_volume;
};

struct mixer_stream {
	// archive data
	struct archive_data *dfile;
	int mixer_no;

	// audio file data
	SNDFILE *file;
	SF_INFO info;
	sf_count_t offset;

	// stream data
	atomic_int voice;
	sts_mixer_stream_t stream;
	float data[CHUNK_SIZE * 2];

	// main thread read-only
	atomic_uint_least32_t frame;

	atomic_uint volume;
	atomic_bool swapped;
	uint_least32_t loop_start;
	uint_least32_t loop_end;
	atomic_uint loop_count;
	struct fade fade;
};

struct mixer {
	sts_mixer_t mixer;
	sts_mixer_stream_t stream;
	int voice;
	atomic_bool muted;
	float data[CHUNK_SIZE * 2];
	char *name;

	struct mixer *parent;
	struct mixer **children;
	int nr_children;

	struct fade fade;
};

static struct mixer *master = NULL;
static struct mixer *mixers = NULL;
static int nr_mixers = 0;

static SDL_AudioDeviceID audio_device = 0;

/*
 * The SDL2 audio callback.
 */
static void audio_callback(void *data, Uint8 *stream, int len)
{
	sts_mixer_mix_audio(&master->mixer, stream, len / (sizeof(float) * 2));
	if (master->muted) {
		memset(stream, 0, len);
	}
}

/*
 * Seek to the specified position in the stream.
 * Returns true if the seek succeeded, otherwise returns false.
 */
static bool cb_seek(struct mixer_stream *ch, uint_least32_t pos)
{
	if (pos > ch->info.frames) {
		pos = ch->info.frames;
	}
	sf_count_t r = sf_seek(ch->file, pos, SEEK_SET);
	if (r < 0) {
		WARNING("sf_seek failed");
		return false;
	}
	ch->frame = r;
	return true;
}

/*
 * Seek to loop start, if the stream should loop.
 * Returns true if the stream should loop, false if it should stop.
 */
static bool cb_loop(struct mixer_stream *ch)
{
	if (!cb_seek(ch, ch->loop_start) || ch->loop_count == 1) {
		return false;
	}
	if (ch->loop_count > 1) {
		ch->loop_count--;
	}
	return true;
}

/*
 * Callback to refill the stream's audio data.
 * Called from sys_mixer_mix_audio.
 */
static int cb_read_frames(struct mixer_stream *ch, float *out, sf_count_t frame_count, uint_least32_t *num_read)
{
	*num_read = 0;

	// handle case where chunk crosses loop point (seamless)
	// NOTE: it's assumed that the length of the loop is greater than the chunk length
	if (ch->frame + frame_count >= ch->loop_end) {
		// read frames up to loop_end
		*num_read = sf_readf_float(ch->file, out, ch->loop_end - ch->frame);
		// adjust parameters for later
		ch->frame += *num_read;
		out += *num_read;
		frame_count -= *num_read;
		// seek to loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	} else if (ch->frame >= ch->loop_end) {
		// seek to loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	}

	// read remaining data
	sf_count_t n = sf_readf_float(ch->file, out, frame_count);
	*num_read += n;
	ch->frame += n;
	out += *num_read;
	frame_count -= *num_read;

	// XXX: This *shouldn't* be necessary, but sometimes libsndfile seems to stop reading
	//      just before the end of file (i.e. ch->frame + frame_count is < ch->info.frames,
	//      yet sf_readf_float returns less that the requested amount of frames).
	//      Not sure if this is a bug in ai5-sdl2 or libsndfile
	if (frame_count > 0) {
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
		*num_read += sf_readf_float(ch->file, out, frame_count);
	}

	return STS_STREAM_CONTINUE;
}

/*
 * Calculate the gain for a fade at a given sample.
 */
static float cb_calc_fade(struct fade *fade)
{
	if (fade->elapsed >= fade->frames)
		return fade->stop ? 0.0 : fade->end_volume;

	float progress = (float)fade->elapsed / (float)fade->frames;
	float delta_v = fade->end_volume - fade->start_volume;
	float gain = fade->start_volume + delta_v * progress;
	if (gain < 0.0) return 0.0;
	if (gain > 1.0) return 1.0;
	return gain;
}

static int refill_stream(sts_mixer_sample_t *sample, void *data)
{
	struct mixer_stream *ch = data;
	uint_least32_t frames_read;
	memset(ch->data, 0, sizeof(float) * sample->length);

	// read audio data from file
	int r = cb_read_frames(ch, ch->data, CHUNK_SIZE, &frames_read);

	// convert mono to stereo
	if (ch->info.channels == 1) {
		for (int i = CHUNK_SIZE-1; i >= 0; i--) {
			ch->data[i*2+1] = ch->data[i];
			ch->data[i*2] = ch->data[i];
		}
	}

	// reverse LR channels
	else if (ch->swapped) {
		for (int i = 0; i < CHUNK_SIZE; i++) {
			float tmp = ch->data[i*2];
			ch->data[i*2] = ch->data[i*2+1];
			ch->data[i*2+1] = tmp;
		}
	}

	// set gain for fade
	if (ch->fade.fading) {
		float gain = cb_calc_fade(&ch->fade);
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
		ch->volume = gain * 100.0;

		ch->fade.elapsed += frames_read;
		if (ch->fade.elapsed >= ch->fade.frames) {
			ch->fade.fading = false;
			ch->volume = ch->fade.end_volume * 100.0;
			if (ch->fade.stop) {
				cb_seek(ch, 0);
				r = STS_STREAM_COMPLETE;
			}
		}
	} else {
		float gain = ch->volume / 100.0;
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
	}

	if (r == STS_STREAM_COMPLETE) {
		ch->voice = -1;
	}

	return r;
}

static int refill_mixer(sts_mixer_sample_t *sample, void *data)
{
	struct mixer *mixer = data;

	// mix child mixers/streams
	sts_mixer_mix_audio(&mixer->mixer, &mixer->data, CHUNK_SIZE);
	if (mixer->muted) {
		memset(mixer->data, 0, sizeof(float) * sample->length);
	}

	// set gain for fade
	if (mixer->fade.fading) {
		float gain = cb_calc_fade(&mixer->fade);
		mixer->mixer.gain = gain;

		mixer->fade.elapsed += CHUNK_SIZE;
		if (mixer->fade.elapsed >= mixer->fade.frames) {
			mixer->fade.fading = false;
			if (mixer->fade.stop) {
				sts_mixer_stop_all_voices(&mixer->mixer);
			}
		}
	}

	return STS_STREAM_CONTINUE;
}

int mixer_stream_play(struct mixer_stream *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice >= 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 1;
	}
	memset(ch->data, 0, sizeof(ch->data));
	ch->voice = sts_mixer_play_stream(&mixers[ch->mixer_no].mixer, &ch->stream, 1.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_stream_stop(struct mixer_stream *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice < 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 1;
	}
	cb_seek(ch, 0);
	sts_mixer_stop_voice(&mixers[ch->mixer_no].mixer, ch->voice);
	ch->voice = -1;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_stream_is_playing(struct mixer_stream *ch)
{
	return ch->voice >= 0;
}

int mixer_stream_set_loop_count(struct mixer_stream *ch, int count)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_count = count;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_stream_get_loop_count(struct mixer_stream *ch)
{
	return ch->loop_count;
}

int mixer_stream_set_loop_start_pos(struct mixer_stream *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_start = pos;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_stream_set_loop_end_pos(struct mixer_stream *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_end = pos;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_stream_set_volume(struct mixer_stream *ch, int volume)
{
	SDL_LockAudioDevice(audio_device);
	ch->fade.fading = false;
	ch->volume = max(0, min(100, volume));
	return 1;
}

int mixer_stream_fade(struct mixer_stream *ch, int time, int volume, bool stop)
{
	if (!time && stop)
		return mixer_stream_stop(ch);
	if (!time)
		return mixer_stream_set_volume(ch, volume);

	SDL_LockAudioDevice(audio_device);
	ch->fade.fading = true;
	ch->fade.stop = stop;
	ch->fade.frames = muldiv(time, ch->info.samplerate, 1000);
	ch->fade.elapsed = 0;
	ch->fade.start_volume = (float)ch->volume / 100.0;
	ch->fade.end_volume = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_stream_stop_fade(struct mixer_stream *ch)
{
	SDL_LockAudioDevice(audio_device);
	// XXX: we need to set the volume to end_volume and potentially stop the
	//      stream here; better to let the callback do it
	ch->fade.elapsed = ch->fade.frames;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_stream_is_fading(struct mixer_stream *ch)
{
	return mixer_stream_is_playing(ch) && ch->fade.fading;
}

int mixer_stream_pause(struct mixer_stream *ch)
{
	WARNING("mixer_stream_pause not implemented");
	return 0;
}

int mixer_stream_restart(struct mixer_stream *ch)
{
	WARNING("mixer_stream_restart not implemented");
	return 0;
}

int mixer_stream_is_paused(struct mixer_stream *ch)
{
	return 0;
}

int mixer_stream_get_pos(struct mixer_stream *ch)
{
	return muldiv(ch->frame, 1000, ch->info.samplerate);
}

int mixer_stream_get_length(struct mixer_stream *ch)
{
	// FIXME: how is this different than mixer_stream_get_time_length?
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
}

int mixer_stream_get_sample_pos(struct mixer_stream *ch)
{
	return ch->frame;
}

int mixer_stream_get_sample_length(struct mixer_stream *ch)
{
	return ch->info.frames;
}

int mixer_stream_seek(struct mixer_stream *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	int r = cb_seek(ch, muldiv(pos, ch->info.samplerate, 1000));
	SDL_UnlockAudioDevice(audio_device);
	return r;
}

int mixer_stream_reverse_LR(struct mixer_stream *ch)
{
	ch->swapped = !ch->swapped;
	return 1;
}

int mixer_stream_get_volume(struct mixer_stream *ch)
{
	return ch->volume;
}

int mixer_stream_get_time_length(struct mixer_stream *ch)
{
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
}

static sf_count_t mixer_stream_vio_get_filelen(void *data)
{
	return ((struct mixer_stream*)data)->dfile->size;
}

static sf_count_t mixer_stream_vio_seek(sf_count_t offset, int whence, void *data)
{
	struct mixer_stream *ch = data;
	switch (whence) {
	case SEEK_CUR:
		ch->offset += offset;
		break;
	case SEEK_SET:
		ch->offset = offset;
		break;
	case SEEK_END:
		ch->offset = ch->dfile->size + offset;
		break;
	}
	ch->offset = clamp(0, (sf_count_t)ch->dfile->size, ch->offset);
	return ch->offset;
}

static sf_count_t mixer_stream_vio_read(void *ptr, sf_count_t count, void *data)
{
	struct mixer_stream *ch = data;
	sf_count_t c = min(count, (sf_count_t)ch->dfile->size - ch->offset);
	memcpy(ptr, ch->dfile->data + ch->offset, c);
	ch->offset += c;
	return c;
}

static sf_count_t mixer_stream_vio_write(const void *ptr, sf_count_t count, void *user_data)
{
	ERROR("sndfile vio write not supported");
}

static sf_count_t mixer_stream_vio_tell(void *data)
{
	return ((struct mixer_stream*)data)->offset;
}

static SF_VIRTUAL_IO mixer_stream_vio = {
	.get_filelen = mixer_stream_vio_get_filelen,
	.seek = mixer_stream_vio_seek,
	.read = mixer_stream_vio_read,
	.write = mixer_stream_vio_write,
	.tell = mixer_stream_vio_tell
};

struct mixer_stream *mixer_stream_open(struct archive_data *dfile, enum mix_channel mixer)
{
	struct mixer_stream *ch = xcalloc(1, sizeof(struct mixer_stream));

	// take ownership of archive file
	if (!archive_data_load(dfile)) {
		WARNING("Failed to load archive file: %s", dfile->name);
		free(ch);
		return NULL;
	}
	ch->dfile = dfile;

	// open file
	ch->file = sf_open_virtual(&mixer_stream_vio, SFM_READ, &ch->info, ch);
	if (sf_error(ch->file) != SF_ERR_NO_ERROR) {
		WARNING("sf_open_virtual failed: %s", sf_strerror(ch->file));
		goto error;
	}
	if (ch->info.channels > 2) {
		WARNING("Audio file has more than 2 channels");
		goto error;
	}

	// create stream
	ch->stream.userdata = ch;
	ch->stream.callback = refill_stream;
	ch->stream.sample.frequency = ch->info.samplerate;
	ch->stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
	ch->stream.sample.length = CHUNK_SIZE * 2;
	ch->stream.sample.data = ch->data;
	ch->voice = -1;

	ch->volume = 100;
	ch->mixer_no = mixer;

	// get loop info
	unsigned loop_start = 0;
	unsigned loop_end = 0;
	unsigned loop_count = 0;
	if (dfile->archive && dfile->archive->meta.type == ARCHIVE_TYPE_AWD) {
		// loop info stored in archive
		if (dfile->meta.loop_start != 0xffffffff) {
			// XXX: convert to sample offsets (assuming 16-bit mono PCM)
			loop_start = dfile->meta.loop_start * 2;
			loop_end = dfile->meta.loop_end * 2;
			loop_count = -1;
		}
	} else {
		// loop info stored in file
		SF_INSTRUMENT instr;
		if (sf_command(ch->file, SFC_GET_INSTRUMENT, &instr, sizeof(instr)) == SF_TRUE) {
			if (instr.loop_count > 0) {
				loop_start = instr.loops[0].start;
				loop_end = instr.loops[0].end;
				loop_count = instr.loops[0].count;
			}
		}
	}

	if (loop_start != loop_end) {
		ch->loop_start = loop_start;
		ch->loop_end = loop_end;
		ch->loop_count = loop_count;
	} else {
		ch->loop_start = 0;
		ch->loop_end = ch->info.frames;
		ch->loop_count = 1;
	}

	return ch;

error:
	archive_data_release(dfile);
	free(ch);
	return NULL;
}

void mixer_stream_close(struct mixer_stream *ch)
{
	mixer_stream_stop(ch);
	sf_close(ch->file);
	archive_data_release(ch->dfile);
	free(ch);
}

void mixer_init(void)
{
	nr_mixers = 5;
	mixers = xcalloc(nr_mixers, sizeof(struct mixer));
	mixers[MIXER_MUSIC].name = strdup("Music");
	mixers[MIXER_EFFECT].name = strdup("Sound");
	mixers[MIXER_VOICE].name = strdup("Voice");
	mixers[MIXER_VOICESUB].name = strdup("VoiceSub");
	mixers[MIXER_MASTER].name = strdup("Master");
	master = &mixers[MIXER_MASTER];
	mixers[MIXER_MUSIC].parent = master;
	mixers[MIXER_EFFECT].parent = master;
	mixers[MIXER_VOICE].parent = master;
	mixers[MIXER_VOICESUB].parent = master;

	// initialize mixers
	for (int i = 0; i < nr_mixers; i++) {
		sts_mixer_init(&mixers[i].mixer, 44100, STS_MIXER_SAMPLE_FORMAT_FLOAT);
		mixers[i].mixer.gain = 1.0f;
	}

	// initialize mixer streams
	for (int i = 0; i < nr_mixers; i++) {
		if (&mixers[i] == master)
			continue;
		mixers[i].stream.userdata = &mixers[i];
		mixers[i].stream.callback = refill_mixer;
		mixers[i].stream.sample.frequency = 44100;
		mixers[i].stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mixers[i].stream.sample.length = CHUNK_SIZE * 2;
		mixers[i].stream.sample.data = mixers[i].data;
		mixers[i].voice = sts_mixer_play_stream(&mixers[i].parent->mixer, &mixers[i].stream, 1.0f);
	}

	// initialize SDL audio
	SDL_AudioSpec have;
	SDL_AudioSpec want = {
		.format = AUDIO_F32,
		.freq = 44100,
		.channels = 2,
		.samples = CHUNK_SIZE,
		.callback = audio_callback,
	};
	audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	SDL_PauseAudioDevice(audio_device, 0);
}

int mixer_get_numof(void)
{
	return nr_mixers;
}

const char *mixer_get_name(int n)
{
	if (n < 0 || n >= nr_mixers)
		return NULL;
	return mixers[n].name;
}

int mixer_set_name(int n, const char *name)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	free(mixers[n].name);
	mixers[n].name = strdup(name);
	return 1;
}

int mixer_stop(int n)
{
	if (n < 0 || n >= nr_mixers)
		return 0;

	SDL_LockAudioDevice(audio_device);
	sts_mixer_stop_all_voices(&mixers[n].mixer);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_get_volume(int n, int *volume)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	SDL_LockAudioDevice(audio_device);
	*volume = clamp(0, 100, (int)(mixers[n].mixer.gain * 100));
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_set_volume(int n, int volume)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	SDL_LockAudioDevice(audio_device);
	mixers[n].fade.fading = false;
	mixers[n].mixer.gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_fade(int n, int time, int volume, bool stop)
{
	if (!time && stop)
		return mixer_stop(n);
	if (!time)
		return mixer_set_volume(n, volume);
	if (n < 0 || n >= nr_mixers)
		return 0;

	SDL_LockAudioDevice(audio_device);
	mixers[n].fade.fading = true;
	mixers[n].fade.stop = stop;
	mixers[n].fade.frames = muldiv(time, mixers[n].stream.sample.frequency, 1000);
	mixers[n].fade.elapsed = 0;
	mixers[n].fade.start_volume = mixers[n].mixer.gain;
	mixers[n].fade.end_volume = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int mixer_is_fading(int n)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	return mixers[n].fade.fading;
}

int mixer_get_mute(int n, int *mute)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	*mute = mixers[n].muted;
	return 1;
}

int mixer_set_mute(int n, int mute)
{
	if (n < 0 || n >= nr_mixers)
		return 0;
	mixers[n].muted = !!mute;
	return 1;
}

int mixer_sts_stream_play(sts_mixer_stream_t* stream, int volume)
{
	SDL_LockAudioDevice(audio_device);
	float gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	int voice = sts_mixer_play_stream(&master->mixer, stream, gain);
	SDL_UnlockAudioDevice(audio_device);
	return voice;
}

bool mixer_sts_stream_set_volume(int voice, int volume)
{
	if (voice < 0 || voice >= STS_MIXER_VOICES)
		return false;
	SDL_LockAudioDevice(audio_device);
	master->mixer.voices[voice].gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	SDL_UnlockAudioDevice(audio_device);
	return true;
}

void mixer_sts_stream_stop(int voice)
{
	SDL_LockAudioDevice(audio_device);
	sts_mixer_stop_voice(&master->mixer, voice);
	SDL_UnlockAudioDevice(audio_device);
}
