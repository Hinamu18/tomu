#include <errno.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <poll.h>

#include "backend.h"
#include "control.h"
#include "utils.h"

void help(){
  printf(
    "Usage: tomu [COMMAND] [PATH]\n"
    " Commands:\n\n"

    "   --loop            : loop same sound\n"
    "   --shuffle-loop    : select random file audio and loop\n"
    "   --version         : show version of program\n"
    "   --help            : show help message\n"

    "\nkeys:\n"
    " Space = pause/resume\n"
    " q = quit\n"

    "\nExample: tomu loop [FILE.mp3]\n"
  );
}

// TODO: not complete yet & have some bugs
// function for handle keys
void *control_place(void *arg){
  PlayBackState *state = (PlayBackState*)arg;

  struct termios old, raw;
  char c;

  tcgetattr(STDIN_FILENO, &old);
  raw = old;

  raw.c_lflag &= ~(ICANON | ECHO);

  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  printf("\033[?25l"); // hide cursor
  fflush(stdout);

  while (state->running){
    struct pollfd pfd = {
      .fd = STDIN_FILENO,
      .events = POLLIN
    };

    // wait 100ms for input
    int ret = poll(&pfd, 1, 100);

    if (ret > 0 && (pfd.revents & POLLIN)) {
      int n = read(STDIN_FILENO, &c, 1);
      if (n > 0 ){

        if (c == 'q'){
          playback_stop(state);
          break;
        }

        else if (c == ' '){
          if (state->paused)
            playback_resume(state);
          else 
            playback_pause(state);
        }

        else if (c == 27) { // ESC
          char seq[2];

          // arrows key
          if (read(STDIN_FILENO, &seq, 2) == 2){
            if (seq[0] == '['){
              if (seq[1] == 'A') volume_increase(state); // UP 
              if (seq[1] == 'B') volume_decrease(state); // Down
              // if (seq[1] == 'D'); // Left
              // if (seq[1] == 'C'); // Right
            }
          }
        }
      }
    }

    else if (ret == 0) 
      continue;

    else 
      perror("[W] poll error");
  }

  printf("\033[?25h\r"); // show cursor
  fflush(stdout);

  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  return NULL;
}

void playback_pause(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->paused = 1;
  pthread_mutex_unlock(&state->lock);
}

void playback_resume(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->paused = 0;
  pthread_cond_broadcast(&state->waitKudasai);
  pthread_mutex_unlock(&state->lock);
}

void playback_stop(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->paused = 0;
  state->running = 0;
  pthread_cond_broadcast(&state->waitKudasai);
  pthread_mutex_unlock(&state->lock);
}

void volume_increase(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->volume += 0.02f;
  if (state->volume > 1.25f) state->volume = 1.25f;
  pthread_mutex_unlock(&state->lock);
}

void volume_decrease(PlayBackState *state){
  pthread_mutex_lock(&state->lock);
  state->volume -= 0.02f;
  if (state->volume < 0.00f) state->volume = 0.00f;
  pthread_mutex_unlock(&state->lock);
}

void path_handle(const char *path){
	struct stat st;
	if (stat(path, &st)<0 )  goto defer;
	if (S_ISDIR(st.st_mode)) shuffle(path);
	if (S_ISREG(st.st_mode)) playback_run(path);
	else goto defer;

	return;
defer:
	die("FILE: %s",strerror(errno));
}

void shuffle(const char *path){
  DIR *dir = opendir(path);
  struct dirent *entry;
  int count = 0;
  srand(time(NULL));

  if (!dir ) goto defer;
  
  while ((entry = readdir(dir)) != NULL ){
    if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) 
		continue;
    count++;
  }

  if (count == 0) goto defer;

  int index_rand = rand() % count;
  rewinddir(dir);

  for (int i = 0; i <= index_rand;){
    entry = readdir(dir);
    if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) continue;
    if (i == index_rand){
      char filename[512];
      snprintf(filename, sizeof(filename), "%s/%s", path, entry->d_name);
      playback_run(filename);
      break;
    }
    i++;
  }

defer:
  closedir(dir);
  die("FILE: %s",strerror(errno));
}
