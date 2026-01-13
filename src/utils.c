#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavcodec/codec.h>
#include <stdio.h>
#include <sys/stat.h>

#include "backend.h"
#include "control.h"
#include "utils.h"

void cleanUP(AVFormatContext *fmtCTX, AVCodecContext *codecCTX){
  if (fmtCTX ) avformat_close_input(&fmtCTX);
  if (codecCTX ) avcodec_free_context(&codecCTX);
}

void path_handle(const char *path, uint loop)
{
  struct stat st;

  if (stat(path, &st) < 0 ) goto bad_path;

    if (S_ISDIR(st.st_mode)) shuffle(path, loop);
    else if (S_ISREG(st.st_mode)) playback_run(path, loop);
    else goto bad_path;

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
