#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "backend.h"
#include "backend_utils.h"
#include "control.h"
#include "socket.h"
#include "utils.h"

#include "../libs/miniaudio.h"

#if LIBSWRESAMPLE_VERSION_MAJOR <= 3
  #define LEGACY_LIBSWRSAMPLE
#endif

// WRITE AUDIO DATA TO BUFFER
void audio_buffer_write(Audio_Buffer *buf, uint8_t *audio_data, int data_must_write)
{
  pthread_mutex_lock(&buf->lock);
  
  while (buf->filled + data_must_write > buf->capacity) {
    pthread_cond_wait(&buf->space_free, &buf->lock);
  }
  
  int space_until_end = buf->capacity - buf->write_pos;
  
  if (data_must_write <= space_until_end) {
    memcpy(buf->pcm_data + buf->write_pos, audio_data, data_must_write);
  } else {
    memcpy(buf->pcm_data + buf->write_pos, audio_data, space_until_end);
    
    int remaining = data_must_write - space_until_end;
    memcpy(buf->pcm_data, audio_data + space_until_end, remaining);
  }
  
  buf->write_pos = (buf->write_pos + data_must_write) % buf->capacity;
  
  buf->filled += data_must_write;
  
  pthread_cond_signal(&buf->data_ready);
  pthread_mutex_unlock(&buf->lock);
}

// READ AUDIO DATA FROM BUFFER TO SPEAKER
void audio_buffer_read(Audio_Buffer *buf, uint8_t *output, int bytes_needed)
{
  pthread_mutex_lock(&buf->lock);
  
  while (buf->filled == 0) {
    pthread_cond_wait(&buf->data_ready, &buf->lock);
  }
  
  int bytes_to_read = bytes_needed;
  if (bytes_to_read > buf->filled) {
    bytes_to_read = buf->filled;
  }
  
  int data_until_end = buf->capacity - buf->read_pos;
  
  if (bytes_to_read <= data_until_end) {
    memcpy(output, buf->pcm_data + buf->read_pos, bytes_to_read);
  } else {
    memcpy(output, buf->pcm_data + buf->read_pos, data_until_end);
    
    int remaining = bytes_to_read - data_until_end;
    memcpy(output + data_until_end, buf->pcm_data, remaining);
  }
  
  buf->read_pos = (buf->read_pos + bytes_to_read) % buf->capacity;
  
  buf->filled -= bytes_to_read;
  
  pthread_cond_signal(&buf->space_free);
  pthread_mutex_unlock(&buf->lock);
}

void *run_decoder(void *arg)
{
  StreamContext *streamCTX = (StreamContext*)arg;
  AVFormatContext *fmtCTX = streamCTX->fmtCTX;
  AVCodecContext * codecCTX = streamCTX->codecCTX;
  SwrContext *swrCTX = NULL;
  Audio_Info *inf = streamCTX->inf;
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
    printf("something happend during packet or frame init!\n");
    if (swrCTX ) swr_free(&swrCTX);
    return NULL;
  }

  int64_t total_samples_played = 0;
  int duration_time = fmtCTX->duration / 1000000.0;

decode:
  // first we read the data from container format (.mp3, .opus, .flac, ...etc)
  while (av_read_frame(fmtCTX, packet) >= 0){

    // we need only audio stream
    if (packet->stream_index == inf->audioStream ){

      // send packet to frame decoder
      if (avcodec_send_packet(codecCTX, packet) < 0 )
        continue;

      // frame recieves it as PCM samples (used by miniaudio for playback)
      while (avcodec_receive_frame(codecCTX, frame) >= 0){
        // init duration progress
        double current_time = (double)total_samples_played / inf->sample_rate;
        progress(state, current_time, duration_time);
        total_samples_played += frame->nb_samples;

        // run this if plnar: convert from planar to interleaved
        if (swrCTX ){
          uint8_t *data_conv = malloc(frame->nb_samples * inf->ch * inf->sample_fmt_bytes);

          if (!data_conv){
            fprintf(stderr, "Error: Out of memory for audio convertion\n");
            av_frame_unref(frame);
            continue;
          }

          uint8_t *data[1] = {data_conv};

          // start converting the samples
          int samples = swr_convert(swrCTX,
            data, frame->nb_samples, // output
            (const uint8_t**)frame->data, frame->nb_samples // input
          );

          if (samples > 0 ){
            // get how much bytes to write from this (PCM samples)
            int bytes = samples * inf->ch * inf->sample_fmt_bytes;
            // write in buffer
            audio_buffer_write(streamCTX->buf, data[0], bytes);
          }
          free(data_conv);

          // run this if: already interleaved
        } else {
          // get how much bytes to write from this (PCM samples)
          int bytes = frame->nb_samples * inf->ch * inf->sample_fmt_bytes;
          // write in buffer
          audio_buffer_write(streamCTX->buf, frame->data[0], bytes);
        }
        av_frame_unref(frame);
      }
    }
    av_packet_unref(packet);

    // check if paused
    pthread_mutex_lock(&state->lock);

      while (state->paused)
        pthread_cond_wait(&state->wait_cond, &state->lock);

    pthread_mutex_unlock(&state->lock);

    if (!state->running) break;
  }

    if (state->looping && state->running) { // if we're looping, restart again..
        av_seek_frame(fmtCTX, -1, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codecCTX);
        total_samples_played = 0;
        goto decode; // find another way, labels aren't good for readability
    }

  printf("\n");

  // IMPORTANT: Signal all waiting threads before exiting
  // Note that other threads are implemented to exit once state.running is false so...
  pthread_mutex_lock(&state->lock);
  state->running = 0;  // Ensure running is 0
  pthread_cond_broadcast(&state->wait_cond);
  pthread_mutex_unlock(&state->lock);
  
  // clean
  if (swrCTX) swr_free(&swrCTX);
  av_frame_free(&frame);
  av_packet_free(&packet);

  // exit
  return NULL;
}
  
// miniaudio will use this callback to read PCM samples
void ma_dataCallback(ma_device *ma_config, void *output, const void *input, ma_uint32 frameCount)
{
  StreamContext *streamCTX = (StreamContext*)ma_config->pUserData;
  Audio_Info *inf = streamCTX->inf;
  PlayBackState *state = streamCTX->state;
  
  // Read audio data
  int bytes = frameCount * inf->ch * inf->sample_fmt_bytes;
  audio_buffer_read(streamCTX->buf, output, bytes);

  // check if paused
  pthread_mutex_lock(&state->lock);

  while (state->paused)
    pthread_cond_wait(&state->wait_cond, &state->lock);

  pthread_mutex_unlock(&state->lock);
  
  // Apply volume (already have safe copy)
  if (state->volume != 1.00f) {
    ma_apply_volume_factor_pcm_frames(output, frameCount, inf->ma_fmt, inf->ch, state->volume);
  }
}

// init miniaudio config before using
ma_device_config init_miniaudioConfig(Audio_Info *inf, StreamContext *streamCTX)
{
  ma_device_config ma_config = ma_device_config_init(ma_device_type_playback);

  ma_config.playback.channels = inf->ch;
  ma_config.playback.format = inf->ma_fmt;
  ma_config.sampleRate = inf->sample_rate;
  ma_config.dataCallback = ma_dataCallback;
  ma_config.pUserData = streamCTX;

  return ma_config;
}

// reads the file and creates a Stream Context
void get_audio_info(const char *filename, StreamContext *streamCTX)
{
  Audio_Info *inf = streamCTX->inf;

  // Read File
  if (avformat_open_input(&streamCTX->fmtCTX, filename, NULL, NULL) < 0 )
    die("ffmpeg: file type is not supported");

  if (avformat_find_stream_info(streamCTX->fmtCTX, NULL) < 0 )
    die("ffmpeg: cannot find any streams");

  // here we try get audio stream index from container
  int audioStream = -1;
  audioStream = get_stream(streamCTX->fmtCTX, AVMEDIA_TYPE_AUDIO, audioStream);

  if (audioStream == -1 )
    die("file: can't find AudioStream");

  // here we get the information about audio stream is codecParameters
  const AVCodecParameters *codecPAR = streamCTX->fmtCTX->streams[audioStream]->codecpar;
  const AVCodec *codecID = avcodec_find_decoder(codecPAR->codec_id);

  // allocate empty decoder
  streamCTX->codecCTX = avcodec_alloc_context3(codecID);

  if (!streamCTX->codecCTX )
    die("ffmpeg: failed allocate codec!");

  // Copy audio specification to decoder
  avcodec_parameters_to_context(streamCTX->codecCTX, codecPAR);

  // initialize decoder with actual codec
  if (avcodec_open2(streamCTX->codecCTX, codecID, NULL) < 0)
    die("ffmpeg: failed init decoder!");

  // Audio samples can be stored in two formats: PLANAR or INTERLEAVED
  // 
  // PLANAR (separate channels):           INTERLEAVED (mixed channels):
  //   Channel 0: [L L L L L L]              [L R L R L R L R L R]
  //   Channel 1: [R R R R R R]
  // 
  // Speakers need INTERLEAVED format! We must convert PLANAR to INTERLEAVED.
  // The decoder will take care of converting sample formats
  enum AVSampleFormat input_sample_fmt = streamCTX->codecCTX->sample_fmt;
  enum AVSampleFormat output_sample_fmt = input_sample_fmt;
  
  if (av_sample_fmt_is_planar(input_sample_fmt)){
    output_sample_fmt = get_interleaved(input_sample_fmt);
  }

  // save to inf base information
  #ifdef LEGACY_LIBSWRSAMPLE
    inf->ch = streamCTX->codecCTX->channels,
    inf->ch_layout = streamCTX->codecCTX->channel_layout,
  #else
    inf->ch = streamCTX->codecCTX->ch_layout.nb_channels,
    inf->ch_layout = streamCTX->codecCTX->ch_layout,
  #endif

  inf->audioStream = audioStream,
  inf->sample_rate = streamCTX->codecCTX->sample_rate,
  inf->sample_fmt = output_sample_fmt,
  inf->sample_fmt_bytes = av_get_bytes_per_sample(inf->sample_fmt),
  inf->ma_fmt = get_ma_format(output_sample_fmt);
}

// this handles playing audio files.
int playback_run(const char *filename, uint loop)
{
  Audio_Info inf = {0};
  Audio_Buffer *buf = NULL;
  PlayBackState state = {0};
  StreamContext streamCTX = {0};

  streamCTX.inf = &inf;
  streamCTX.buf = NULL;
  streamCTX.state = &state;
  streamCTX.fmtCTX = NULL;
  streamCTX.codecCTX = NULL;

  av_log_set_level(AV_LOG_QUIET); // ignore warning

  // create StreamContext from file.
  get_audio_info(filename, &streamCTX);
  init_playbackstatus(&state, loop);

  // init threads
  pthread_t control_thread;
  pthread_t sock_thread;
  pthread_t decoder_thread;

  // init a buffer size = 500ms
  int capacity = (inf.sample_rate) * (inf.ch) * (inf.sample_fmt_bytes) * 0.5;
  streamCTX.buf = audio_buffer_init(capacity);

  // init miniaudio device (for sending PCM samples to speaker)
  ma_device device;
  ma_device_config ma_config = init_miniaudioConfig(&inf, &streamCTX);

  // initialize the device output
  if (ma_device_init(NULL, &ma_config, &device) != MA_SUCCESS ){
    audio_buffer_destroy(buf);
    pthread_mutex_destroy(&state.lock);
    pthread_cond_destroy(&state.wait_cond);
    return 1;
  }

  // Outputs
  if (streamCTX.fmtCTX->metadata)
    print_metadata(streamCTX.fmtCTX->metadata);

  printf("Playing: %s\n",  filename);
  printf("%.2dHz, %dch, %s\n", inf.sample_rate, inf.ch, av_get_sample_fmt_name(inf.sample_fmt));

  // start threads
  pthread_create(&control_thread, NULL, handle_input, &state); // terminal controls
  pthread_create(&sock_thread, NULL, run_socket, &state); // socket controls
  pthread_create(&decoder_thread, NULL, run_decoder, &streamCTX); // decoder ._.
  
  // start mini audio and wait decoder write to end
  ma_device_start(&device);

  // wait for all threads to finish.. (if only we could allow the main thread to have coffee during this..)
  pthread_join(decoder_thread, NULL);
  pthread_join(control_thread, NULL);
  pthread_join(sock_thread, NULL);

  // clean up
  ma_device_stop(&device);
  ma_device_uninit(&device);
  audio_buffer_destroy(streamCTX.buf);
  pthread_mutex_destroy(&state.lock);
  pthread_cond_destroy(&state.wait_cond);

  cleanUP(streamCTX.fmtCTX, streamCTX.codecCTX);
  return 0;
}
