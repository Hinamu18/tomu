#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libswresample/version.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "backend.h"
#include "control.h"
#include "socket.h"
#include "utils.h"

#define MINIAUDIO_IMPLEMENTATION
#include "libs/miniaudio.h"

#if LIBSWRESAMPLE_VERSION_MAJOR <= 3
  #define LEGACY_LIBSWRSAMPLE
#endif

// function take from planar_value to get interleaved_value
enum AVSampleFormat get_interleaved(enum AVSampleFormat value){
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
ma_format get_ma_format(enum AVSampleFormat value){
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

AudioBuffer *audio_buffer_init(int capacity){
  AudioBuffer *buf = malloc(sizeof(AudioBuffer));
  buf->PCM_data = malloc(capacity);
  buf->capacity = capacity;
  buf->write_postion = 0;
  buf->read_postion = 0;
  buf->size = 0;
  pthread_mutex_init(&buf->lock, NULL);
  pthread_cond_init(&buf->data_avaliable, NULL);
  pthread_cond_init(&buf->space_avaliable, NULL);
  return buf;
}

void audio_buffer_destroy(AudioBuffer *buf){
  if (buf ){
    free(buf->PCM_data);
    pthread_mutex_destroy(&buf->lock);
    pthread_cond_destroy(&buf->data_avaliable);
    pthread_cond_destroy(&buf->space_avaliable);
    free (buf);
  }
}

void audio_buffer_write(AudioBuffer *buf, uint8_t *data, int bytes_write){
  pthread_mutex_lock(&buf->lock);
  while(buf->size + bytes_write > buf->capacity){
    pthread_cond_wait(&buf->space_avaliable, &buf->lock);
  }

  int chunk = buf->capacity - buf->write_postion;
  if (chunk > bytes_write ){
    chunk = bytes_write;
  }

  memcpy(buf->PCM_data + buf->write_postion, data, chunk);
  memcpy(buf->PCM_data, data + chunk, bytes_write - chunk);

  buf->write_postion = (buf->write_postion + bytes_write) % buf->capacity;
  buf->size += bytes_write;

  pthread_cond_signal(&buf->data_avaliable);

  pthread_mutex_unlock(&buf->lock);
}

void audio_buffer_read(AudioBuffer *buf, uint8_t *output, int bytes_read){
  pthread_mutex_lock(&buf->lock);
  while(buf->size == 0){
    pthread_cond_wait(&buf->data_avaliable, &buf->lock);
  }

  if (bytes_read > buf->size){
    bytes_read = buf->size;
  }

  int chunk = buf->capacity - buf->read_postion;
  if (chunk > bytes_read ){
    chunk = bytes_read;
  }

  memcpy(output, buf->PCM_data + buf->read_postion, chunk);
  memcpy(output + chunk, buf->PCM_data, bytes_read - chunk);

  buf->read_postion = (buf->read_postion + bytes_read) % buf->capacity;
  buf->size -= bytes_read;

  pthread_cond_signal(&buf->space_avaliable);
  pthread_mutex_unlock(&buf->lock);
}

void *run_decoder(void *arg){
  StreamContext *streamCTX = (StreamContext*)arg;
  AVFormatContext *fmtCTX = streamCTX->fmtCTX;
  AVCodecContext * codecCTX = streamCTX->codecCTX;
  SwrContext *swrCTX = NULL;
  AudioInfo *inf = streamCTX->inf;
  PlayBackState *state = streamCTX->state;

  #ifdef LEGACY_LIBSWRSAMPLE
    swrCTX = swr_alloc_set_opts(swrCTX,
      inf->ch_layout, inf->sample_fmt, inf->sample_rate, // output
      inf->ch_layout, codecCTX->sample_fmt, inf->sample_rate, // input
      0, NULL
    );
  #else
    swr_alloc_set_opts2(&swrCTX,
      &inf->ch_layout, inf->sample_fmt, inf->sample_rate, // output
      &inf->ch_layout, codecCTX->sample_fmt, inf->sample_rate, // input
      0, NULL
    );
  #endif

  if (!swrCTX || swr_init(swrCTX) < 0 )
    swr_free(&swrCTX);

  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  if (!packet || !frame ){
    printf("something happend when init packet or frame!\n");
    if (swrCTX ) swr_free(&swrCTX);
    return NULL;
  }

  int64_t total_samples_played = 0;
  int duration_time = fmtCTX->duration / 1000000.0;


  // first we read the data from container format (.mp3, .opus, .flac, ...etc)
  while (av_read_frame(fmtCTX, packet) >= 0){

    // we need only audio stream
    if (packet->stream_index == inf->audioStream){

      // sent packet to frame decoder
      if (avcodec_send_packet(codecCTX, packet) < 0 ){
        continue;
      }

      // frame take it as PCM samples (for used to miniaudio send to speaker for played)
      while (avcodec_receive_frame(codecCTX, frame) >= 0){
        // init duration
        double current_time = (double)total_samples_played / inf->sample_rate;
        printf("\r  %d:%02d:%02d / %d:%02d:%02d | vol: %.0f%%.",
          get_hour(current_time), get_min(current_time), get_sec(current_time), 
          get_hour(duration_time), get_min(duration_time), get_sec(duration_time),
          state->volume * 100
        );
        fflush(stdout);
        total_samples_played += frame->nb_samples;

        // run this if plnar: convert from planar to interleaved
        if (swrCTX ){
          uint8_t *data_conv = malloc(frame->nb_samples * inf->ch * inf->sample_fmt_bytes);
          uint8_t *data[1] = {data_conv};

          // start convert the samples
          int samples = swr_convert(swrCTX,
            data, frame->nb_samples, // output
            (const uint8_t**)frame->data, frame->nb_samples // input
          );

          if (samples > 0 ){
            // get how much bytes write from this (PCM samples)
            int bytes = samples * inf->ch * inf->sample_fmt_bytes;
            audio_buffer_write(streamCTX->buf, data[0], bytes);
          }
          free(data_conv);

          // run this if: already interleaved
        } else{
          // get how much bytes write from this (PCM samples)
          int bytes = frame->nb_samples * inf->ch * inf->sample_fmt_bytes;
          audio_buffer_write(streamCTX->buf, *frame->data, bytes);
        }
        av_frame_unref(frame);
      }
    }
    av_packet_unref(packet);

    pthread_mutex_lock(&state->lock);
    while (state->paused){
      pthread_cond_wait(&state->waitKudasai, &state->lock);
    }
    pthread_mutex_unlock(&state->lock);

    if (!state->running) break;
  }

  if (swrCTX ) swr_free(&swrCTX);
  av_frame_free(&frame);
  av_packet_free(&packet);
  return NULL;
}
  
// miniaudio will use this function everytime for reading PCM samples
void ma_dataCallback(ma_device *ma_config, void *output, const void *input, ma_uint32 frameCount){
  StreamContext *streamCTX = (StreamContext*)ma_config->pUserData;
  AudioInfo *inf = streamCTX->inf;
  PlayBackState *state = streamCTX->state;

  // check if audio is paused
  pthread_mutex_lock(&streamCTX->state->lock);

  while (streamCTX->state->paused)
    pthread_cond_wait(&streamCTX->state->waitKudasai, &streamCTX->state->lock);

  pthread_mutex_unlock(&streamCTX->state->lock);

  // see how much bytes needed by speaker to work
  int bytes = frameCount * inf->ch * inf->sample_fmt_bytes;
  audio_buffer_read(streamCTX->buf, output, bytes);

  if (state->volume != 1.00f)
    ma_apply_volume_factor_pcm_frames(output, frameCount, inf->ma_fmt, inf->ch, state->volume);

}

// init mini audio config before used
ma_device_config init_miniaudioConfig(AudioInfo *inf, StreamContext *streamCTX){
  ma_device_config ma_config = ma_device_config_init(ma_device_type_playback);

  ma_config.playback.channels = inf->ch;
  ma_config.playback.format = inf->ma_fmt;
  ma_config.sampleRate = inf->sample_rate;
  ma_config.dataCallback = ma_dataCallback;
  ma_config.pUserData = streamCTX;

  return ma_config;
}

// this function is responsible of running workers and starting playback
void stream_audio(StreamContext *streamCTX){
  AudioInfo *inf = streamCTX->inf;
  PlayBackState *state = streamCTX->state;

  pthread_t control_thread;
  pthread_t sock_thread;
  pthread_t decoder_thread;

  // init a buffer size = 500ms
  int capacity = (inf->sample_rate) * (inf->ch) * (inf->sample_fmt_bytes) * 0.5;
  streamCTX->buf = audio_buffer_init(capacity);

  // init miniaudio engine (for sending PCM samples to speaker)
  ma_device device;
  ma_device_config ma_config = init_miniaudioConfig(inf, streamCTX);

  // initialize the device output
  if (ma_device_init(NULL, &ma_config, &device) != MA_SUCCESS ){
    audio_buffer_destroy(streamCTX->buf);
    pthread_mutex_destroy(&state->lock);
    pthread_cond_destroy(&state->waitKudasai);
    return;
  }

  // exec 3 worker threads
  pthread_create(&control_thread, NULL, handle_input, streamCTX->state);
  pthread_create(&sock_thread, NULL, run_socket, streamCTX->state);
  pthread_create(&decoder_thread, NULL, run_decoder, streamCTX);
  
  // start device and wait till there's not more samples to play
  ma_device_start(&device);
  pthread_join(decoder_thread, NULL);

  // playback_stop(state);

  // when end
  // pthread_join(control_thread, NULL);
  // pthread_join(sock_thread, NULL);

  // audio is end now must off all thing (I'll keep this it's funny)
  // cleanup after playback finishes
  ma_device_stop(&device);
  ma_device_uninit(&device);
  audio_buffer_destroy(streamCTX->buf);
  pthread_mutex_destroy(&state->lock);
  pthread_cond_destroy(&state->waitKudasai);
  return;
}

// This function for get information from a file
// we only trying extract information & save values with used in stream_audio()
int playback_run(const char *filename){
  AVFormatContext *fmtCTX = NULL;
  AVCodecContext *codecCTX = NULL;

  av_log_set_level(AV_LOG_QUIET);

  if (avformat_open_input(&fmtCTX, filename, NULL, NULL) < 0 )
	die("file: file type is not supported");

  if (avformat_find_stream_info(fmtCTX, NULL) < 0 )
	die("ffmpeg: cannot find any streams");

  // he we get stream audio index from container
  int audioStream = -1;
  for (int i = 0; i < fmtCTX->nb_streams; i++){
    AVStream *stream = fmtCTX->streams[i];
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ){
      audioStream = i;
      break;
    }
  }

  if (audioStream == -1 )
	die("file: file type is not supported");

  // here we get the information about audio stream is codecParameters
  const AVCodecParameters *codecPAR = fmtCTX->streams[audioStream]->codecpar;

  // codecID mean the key of decoder needed for decode as PCM samples (later)
  const AVCodec *codecID = avcodec_find_decoder(codecPAR->codec_id);

  // init empty decoder
  codecCTX = avcodec_alloc_context3(codecID);

  if (!codecCTX )
    die("ffmpeg: cannot allocate codec!");

  // Copy audio specification to decoder
  avcodec_parameters_to_context(codecCTX, codecPAR);

  // initialize decoder with actual codec
  if (avcodec_open2(codecCTX, codecID, NULL) < 0)
    die("ffmpeg: cannot init decoder!");

  // Audio samples can be stored in two formats: PLANAR or INTERLEAVED
  // 
  // PLANAR (separate channels):           INTERLEAVED (mixed channels):
  //   Channel 0: [L L L L L L]              [L R L R L R L R L R]
  //   Channel 1: [R R R R R R]
  // 
  // Speakers need INTERLEAVED format! We must convert PLANAR to INTERLEAVED.
  // here not mean is converted now, not yet (later)
  enum AVSampleFormat input_sample_fmt = codecCTX->sample_fmt;
  enum AVSampleFormat output_sample_fmt = input_sample_fmt;
  
  if (av_sample_fmt_is_planar(input_sample_fmt)){
    output_sample_fmt = get_interleaved(input_sample_fmt);
  }

  // setup PlaybackState
  PlayBackState state = {
    .running = 1,
    .paused = 0,
    .volume = 1.00f
  };
  pthread_mutex_init(&state.lock, NULL);
  pthread_cond_init(&state.waitKudasai, NULL);

  // Save Base information to AudioInfo struct
  AudioInfo inf = {
    #ifdef LEGACY_LIBSWRSAMPLE
      .ch = codecCTX->channels,
      .ch_layout = codecCTX->channel_layout,
    #else
      .ch = codecCTX->ch_layout.nb_channels,
      .ch_layout = codecCTX->ch_layout,
    #endif

    .audioStream = audioStream,
    .sample_rate = codecCTX->sample_rate,
    .sample_fmt = output_sample_fmt,
    .sample_fmt_bytes = av_get_bytes_per_sample(inf.sample_fmt),
    .ma_fmt = get_ma_format(output_sample_fmt),
  };

  // Pointer Contexts for used another functions
  StreamContext streamCTX = {
    .inf = &inf,
    .fmtCTX = fmtCTX,
    .codecCTX = codecCTX,
    .state = &state,
  };

  printf("Playing: %s\n",  filename);
  printf("%dHz, %dch, %s\n", inf.sample_rate, inf.ch, av_get_sample_fmt_name(inf.sample_fmt));

  // play audio
  stream_audio(&streamCTX);

  cleanUP(fmtCTX, codecCTX);
  return 0;
}
