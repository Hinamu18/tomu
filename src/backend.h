#ifndef BACKEND_H
#define BACKEND_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include "../libs/miniaudio.h"

#if LIBSWRESAMPLE_VERSION_MAJOR <= 3
  #define LEGACY_LIBSWRSAMPLE
#endif

// struct handle Playback
typedef struct {
  int running;
  int paused;
  float volume;
  uint looping;
  pthread_mutex_t lock;
  pthread_cond_t wait_cond;

} PlayBackState;

typedef struct {
  uint8_t *pcm_data;           // Audio data storage
  int capacity;                // Total size in bytes
  int write_pos;               // Where to write next
  int read_pos;                // Where to read next  
  int filled;                  // How many bytes are stored now
  pthread_mutex_t lock;        // Protect from multiple threads
  pthread_cond_t data_ready;   // Signal when data available
  pthread_cond_t space_free;   // Signal when space available

} Audio_Buffer;

// struct for base information of audio file (codec)
typedef struct {
  int audioStream;
  int ch;
  #ifdef LEGACY_LIBSWRSAMPLE
    int ch_layout;
  #else
    AVChannelLayout ch_layout;
  #endif
  int sample_rate;
  enum AVSampleFormat sample_fmt;
  int sample_fmt_bytes;
  ma_format ma_fmt;

} Audio_Info;


// struct for point context used in another functions (needed)
typedef struct {
  Audio_Buffer *buf;
  Audio_Info *inf;
  AVFormatContext *fmtCTX;
  AVCodecContext *codecCTX;
  PlayBackState *state;

} StreamContext;

int playback_run(const char *filename, uint loop);

#endif
