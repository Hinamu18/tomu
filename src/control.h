#ifndef CONTROL_H
#define CONTROL_H

#include "backend.h"

void help();
void *handle_input(void *arg);
void playback_toggle(PlayBackState *state);
void playback_stop(PlayBackState *state);
void volume_increase(PlayBackState *state);
void volume_decrease(PlayBackState *state);
void path_handle(const char *path);
void shuffle(const char *path);

// those don't need to be used directly, and inlining makes life nicer

// use playback_toggle unless you have a good reason to use this
inline void playback_pause(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->paused = 1;
  pthread_mutex_unlock(&state->lock);
}

// use playback_toggle unless you have a good reason to use this
inline void playback_resume(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->paused = 0;
  pthread_cond_broadcast(&state->waitKudasai);
  pthread_mutex_unlock(&state->lock);
}

#endif
