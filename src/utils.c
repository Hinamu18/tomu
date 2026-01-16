#include <libavformat/avformat.h>
#include <libavcodec/codec.h>
#include <stdio.h>
#include <sys/stat.h>

#include "backend.h"
#include "control.h"
#include "utils.h"

inline void help()
{
  printf(
    "Usage: tomu [COMMAND] [PATH]\n"
    " Commands:\n\n"

    "   --loop            : loop same sound\n"
    "   --version         : show version of program\n"
    "   --help            : show help message\n"

    "\nkeys:\n"
    " (Space) = pause/resume\n"
    " (q) = quit\n"
    " (-) = decrease volume\n"
    " (+) = increase volume\n"
    " (↑/→) = audio seek forward +5s/1m\n"
    " (←/↓) = audio seek backward -5s/1m\n"
    " ([) = audio speed decrease\n"
    " (]) = audio speed increase\n"

    "\nExample: tomu loop [FILE.mp3]\n"
  );
}

void cleanUP(AVFormatContext *fmtCTX, AVCodecContext *codecCTX){
  if (fmtCTX ) avformat_close_input(&fmtCTX);
  if (codecCTX ) avcodec_free_context(&codecCTX);
}

void path_handle(const char *path, uint loop)
{
  struct stat st;

  if (stat(path, &st) < 0 ) goto bad_path; //

    // start checking
    if (S_ISDIR(st.st_mode)) shuffle(path, loop); // this folder
    else if (S_ISREG(st.st_mode)) playback_run(path, loop); // this file
    else goto bad_path; // other

  return;

bad_path:
  die("File:");
}

void verr(const char *fmt, va_list ap)
{
	vfprintf(stderr, fmt, ap);
	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verr(fmt, ap);
	va_end(ap);
}

void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verr(fmt, ap);
	va_end(ap);
	exit(-1);
}
