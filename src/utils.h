#ifndef UTILS_H
#define UTILS_H

#include <libavformat/avformat.h>

#define false 0
#define true 1

void cleanUP(AVFormatContext *fmtCTX, AVCodecContext *codecCTX);
void path_handle(const char *path, uint loop);

void verr(const char *fmt, va_list ap);
void warn(const char *fmt, ...);
void die(const char *fmt, ...);

#endif
