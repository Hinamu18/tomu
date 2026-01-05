#ifndef CONTROL_H
#define CONTROL_H

#include "backend.h"

void help();
void *control_place(void *arg);
void playback_pause(PlayBackState *state);
void playback_resume(PlayBackState *state);
void playback_stop(PlayBackState *state);
void volume_increase(PlayBackState *state);
void volume_decrease(PlayBackState *state);
void path_handle(const char *path);
void shuffle(const char *path);

#endif
