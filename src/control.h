#ifndef CONTROL_H
#define CONTROL_H

#include "backend.h"

void *handle_input(void *arg);

void playback_toggle(PlayBackState *state);
void playback_pause(PlayBackState *state);
void playback_resume(PlayBackState *state);
void playback_stop(PlayBackState *state);


void seek_forward_sec(PlayBackState *state);
void seek_forward_min(PlayBackState *state);
void seek_backward_sec(PlayBackState *state);
void seek_backward_min(PlayBackState *state);

void playback_speed_increase(PlayBackState *state);
void playback_speed_decrease(PlayBackState *state);

void volume_increase(PlayBackState *state);
void volume_decrease(PlayBackState *state);

void shuffle(const char *path, uint loop);

#endif
