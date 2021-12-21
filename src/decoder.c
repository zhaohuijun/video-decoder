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

void logCB(void* ptr, int level, const char* fmt, va_list vl) {
	char line[1024] = { 0 };
	AVClass* avc = ptr ? *(AVClass**)ptr : NULL;
	printf("ptr:%p,level:%d,fmt:%s,vl:%p\n", ptr, level, fmt, vl);
	if (level > g_logLevel) {
		return;
	}
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

void enableLog(const char *level) {
	if (!level) {
		g_logLevel = AV_LOG_DEBUG;
	} else if (strcasecmp("panic", level) == 0) {
		g_logLevel = AV_LOG_PANIC;
	} else if (strcasecmp("fatal", level) == 0) {
		g_logLevel = AV_LOG_FATAL;
	} else if (strcasecmp("error", level) == 0 || strcasecmp("err", level) == 0) {
		g_logLevel = AV_LOG_ERROR;
	} else if (strcasecmp("warning", level) == 0 || strcasecmp("warn", level) == 0) {
		g_logLevel = AV_LOG_WARNING;
	} else if (strcasecmp("info", level) == 0) {
		g_logLevel = AV_LOG_INFO;
	} else if (strcasecmp("verbose", level) == 0) {
		g_logLevel = AV_LOG_VERBOSE;
	} else if (strcasecmp("debug", level) == 0) {
		g_logLevel = AV_LOG_DEBUG;
	} else if (strcasecmp("trace", level) == 0) {
		g_logLevel = AV_LOG_TRACE;
	} else {
		g_logLevel = AV_LOG_DEBUG;
	}
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

void *createH264Decoder(FrameCallback cb) {
	if (!cb) {
		av_log(NULL, AV_LOG_ERROR, "need cb");
		return NULL;
	}
	Decoder *de = malloc(sizeof(Decoder));
	de->cb = cb;
	de->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!de->codec) {
		releaseDecoder(de);
		return NULL;
	}
	return de;
}

void *createH265Decoder(FrameCallback cb) {
	Decoder *de = malloc(sizeof(Decoder));
	de->cb = cb;
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
