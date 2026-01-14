#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include "../libs/miniaudio.h"

#include "backend.h"
#include "backend_utils.h"

// function take from planar_value to get interleaved_value
enum AVSampleFormat get_interleaved(enum AVSampleFormat value)
{
  switch (value){
    case AV_SAMPLE_FMT_DBLP: return AV_SAMPLE_FMT_DBL;
    case AV_SAMPLE_FMT_FLTP: return AV_SAMPLE_FMT_FLT;
    case AV_SAMPLE_FMT_S64P: return AV_SAMPLE_FMT_S64;
    case AV_SAMPLE_FMT_S32P: return AV_SAMPLE_FMT_S32;
    case AV_SAMPLE_FMT_S16P: return AV_SAMPLE_FMT_S16;
    case AV_SAMPLE_FMT_U8P: return AV_SAMPLE_FMT_U8;
    default: return AV_SAMPLE_FMT_S16; // fallback
  }
}

// function take from interleaved_value get mini audio format
ma_format get_ma_format(enum AVSampleFormat value)
{
  switch (value){
    case AV_SAMPLE_FMT_DBL: return ma_format_f32;
    case AV_SAMPLE_FMT_FLT: return ma_format_f32;
    case AV_SAMPLE_FMT_S64: return ma_format_s32;
    case AV_SAMPLE_FMT_S32: return ma_format_s32;
    case AV_SAMPLE_FMT_S16: return ma_format_s16;
    case AV_SAMPLE_FMT_U8: return ma_format_u8;
    default: return ma_format_s16; // fallback
  }
}

// fn to search correct stream you want
int get_stream(AVFormatContext *fmtCTX, int type, int value)
{
  for (int i = 0; i < fmtCTX->nb_streams; i++){
    AVStream *stream = fmtCTX->streams[i];
    if (stream->codecpar->codec_type == type ){
      value = i;
      break;
    }
  }
  return value;
}

// Setup SWR context
int setup_decoder(StreamContext *streamCTX, Audio_Info *inf, SwrContext **swrCTX)
{

  #ifdef LEGACY_LIBSWRSAMPLE
    *swrCTX = swr_alloc_set_opts(*swrCTX,
      inf->ch_layout, inf->sample_fmt, inf->sample_rate, // output
      inf->ch_layout, streamCTX->codecCTX->sample_fmt, inf->sample_rate, // input
      0, NULL
    );
  #else
    swr_alloc_set_opts2(&swrCTX,
      &inf->ch_layout, inf->sample_fmt, inf->sample_rate, // output
      &inf->ch_layout, codecCTX->sample_fmt, inf->sample_rate, // input
      0, NULL
    );
  #endif

  return 1;
}

void init_playbackstatus(PlayBackState *state, uint loop)
{
  state->running = 1;
  state->paused = 0;
  state->volume = 1.00f;
  state->looping = loop;

  state->seek_request = 0;
  state->seek_target = 0;

  pthread_mutex_init(&state->lock, NULL);
  pthread_cond_init(&state->wait_cond, NULL);
}

void print_metadata(AVDictionary *metadata) {
  AVDictionaryEntry *tag = NULL;

  printf("File tags:\n");
  while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
    printf("  %s : %s\n", tag->key, tag->value);
  }
}

// Reset audio buffer to empty state (used after seeking to discard old audio)
void audio_buffer_reset(Audio_Buffer *buf)
{
  pthread_mutex_lock(&buf->lock);

    buf->filled = 0;
    buf->read_pos = 0;
    buf->write_pos = 0;
    pthread_cond_broadcast(&buf->space_free);

  pthread_mutex_unlock(&buf->lock);
}

Audio_Buffer *audio_buffer_init(int capacity)
{
  Audio_Buffer *buf = malloc(sizeof(Audio_Buffer));

  buf->pcm_data = malloc(capacity);
  buf->capacity = capacity;
  buf->write_pos = 0;     // Start writing at beginning
  buf->read_pos = 0;      // Start reading from beginning
  buf->filled = 0;        // Buffer starts empty

  pthread_mutex_init(&buf->lock, NULL);
  pthread_cond_init(&buf->data_ready, NULL);
  pthread_cond_init(&buf->space_free, NULL);
  return buf;
}

void audio_buffer_destroy(Audio_Buffer *buf)
{
  if (buf ){
    free(buf->pcm_data);
    pthread_mutex_destroy(&buf->lock);
    pthread_cond_destroy(&buf->data_ready);
    pthread_cond_destroy(&buf->space_free);
    free (buf);
  }
}


void handle_audio_seek(StreamContext *streamCTX, int *duration_time, int64_t *total_samples_played)
{
  Audio_Info *inf = streamCTX->inf;
  PlayBackState *state = streamCTX->state;
  AVFormatContext *fmtCTX = streamCTX->fmtCTX;
  AVCodecContext *codecCTX = streamCTX->codecCTX;


  // Get current position in seconds
  double current_sec = (double)*total_samples_played / inf->sample_rate;
  
  // Calculate new position (seek_target is in microseconds, convert to seconds)
  double new_position_seconds = current_sec + ((double)state->seek_target / 1000000);
  
  // Clamp to valid range (0 to duration)
  if (new_position_seconds < 0) new_position_seconds = 0;
  if (new_position_seconds > *duration_time) new_position_seconds = *duration_time;
  
  // Convert to stream timebase for av_seek_frame
  // av_q2d converts AVRational to double: numerator / denominator
  int64_t target_pts = (int64_t)(new_position_seconds / av_q2d(inf->audioStream->time_base));
  
  // Perform the seek (ffmpeg wants stream timebase units, not microseconds!)
  av_seek_frame(fmtCTX, inf->audioStream_index, target_pts, AVSEEK_FLAG_BACKWARD);
  avcodec_flush_buffers(codecCTX);

  // Update sample counter
  *total_samples_played = (int64_t)(new_position_seconds * inf->sample_rate);

  // clear buffer (discard old audio)
  audio_buffer_reset(streamCTX->buf);

  // reset seek flag
  state->seek_request = 0;
  state->seek_target = 0;
  return;
}

inline void progress(PlayBackState *state, double current_time, int duration_time)
{
  int bar_width = 30;

  int pos = (current_time / duration_time) * bar_width;

  printf("\033[2K"); // clear the line before writing, if this causes flickering, do it manually (using spaces)
  printf("\r[");
  for (int i = 0; i < bar_width; i++){

    if (i < pos)
      printf("=");

    else if (i == pos)
      printf(">");

    else
      printf(".");
  }

    printf("] %d:%02d:%02d / %d:%02d:%02d (%.00f%%) | v: %.0f%%",
    get_hour(current_time), get_min(current_time), get_sec(current_time), 
    get_hour(duration_time), get_min(duration_time), get_sec(duration_time),
    (current_time / duration_time) * 100.0, state->volume * 100.0f
  );

  fflush(stdout);
}
