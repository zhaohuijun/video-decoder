 /**
  * @file
  * video decoding with libavcodec API example
  *
  * decode_video.c
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

int g_logLevel = AV_LOG_DEBUG;

const char* logLevelStr(int level) {
	switch (level) {
		case AV_LOG_PANIC:
			return "[panic]";
		case AV_LOG_FATAL:
			return "[fatal]";
		case AV_LOG_ERROR:
			return "[err]";
		case AV_LOG_WARNING:
			return "[warn]";
		case AV_LOG_INFO:
			return "[info]";
		case AV_LOG_VERBOSE:
			return "[verbose]";
		case AV_LOG_DEBUG:
			return "[debug]";
		case AV_LOG_TRACE:
			return "[trace]";
	}
	return "[]";
}

void logCB(void* ptr, int level, const char* fmt, va_list vl) {
	char line[1024] = { 0 };
	AVClass* avc = ptr ? *(AVClass**)ptr : NULL;
	if (level > g_logLevel) {
		return;
	}
	snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s", logLevelStr(level));
	if (avc) {
		if (avc->parent_log_context_offset) {
			AVClass** parent = *(AVClass***)(((uint8_t*)ptr) + avc->parent_log_context_offset);
			if (parent && *parent) {
				snprintf(line, sizeof(line), "[%s @ %p] ", (*parent)->item_name(parent), parent);
			}
		}
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%s @ %p] ", avc->item_name(ptr), ptr);
	}

	vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);
	puts(line);
}

void enableLog(int level) {
	g_logLevel = level;
	av_log_set_level(g_logLevel);
	av_log_set_callback(logCB);
}

void disableLog() {
	av_log_set_callback(NULL);
}

typedef void(*FrameCallback)(unsigned char* rgba, int width, int height);

typedef struct {
	FrameCallback cb;
	AVCodec* codec;
} Decoder;

void releaseDecoder(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder *de = (Decoder *)ctx;
	free(de);
}

void *createH264Decoder() {
	// if (!cb) {
	// 	av_log(NULL, AV_LOG_ERROR, "need cb");
	// 	return NULL;
	// }
	Decoder *de = malloc(sizeof(Decoder));
	// de->cb = cb;
	de->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!de->codec) {
		releaseDecoder(de);
		return NULL;
	}
	av_log(NULL, AV_LOG_DEBUG, "createH264Decoder end");
	return de;
}

void *createH265Decoder() {
	Decoder *de = malloc(sizeof(Decoder));
	// de->cb = cb;
	return de;
}

void put(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder *de = (Decoder *)ctx;
}

void flush(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder *de = (Decoder *)ctx;
}
