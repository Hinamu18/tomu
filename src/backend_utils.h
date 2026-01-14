#ifndef BACKEND_UTILS
#define BACKEND_UTILS

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include "../libs/miniaudio.h"

#include "backend.h"

enum AVSampleFormat get_interleaved(enum AVSampleFormat value);
ma_format get_ma_format(enum AVSampleFormat value);

int get_stream(AVFormatContext *fmtCTX, int type, int value);
int setup_decoder(StreamContext *streamCTX, Audio_Info *inf, SwrContext **swrCTX);

Audio_Buffer *audio_buffer_init(int capacity);
void audio_buffer_destroy(Audio_Buffer *buf);

void init_playbackstatus(PlayBackState *state, uint loop);


void handle_audio_seek(StreamContext *streamCTX, int *duration_time, int64_t *total_samples_played);
void print_metadata(AVDictionary *metadata);
void progress(PlayBackState *state, double current_time, int duration_time);

static inline int get_sec(double value){
  return (int)value % 60;
}

static inline int get_min(double value){
  return ((int)value % 3600) / 60;
}

static inline int get_hour(double value){
  return (int)value / 3600;
}

#endif
