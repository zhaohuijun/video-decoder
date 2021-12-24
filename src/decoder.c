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
	AVCodecParserContext* parser;
	AVCodecContext* context;
	AVFrame* frame;
	AVPacket* packet;
} Decoder;

typedef struct {
	unsigned char* buf;
	int width;
	int height;
} Frame;

typedef struct {
	int count;
	Frame frames[]; // 长度由上面的count决定
} Frames;


void releaseDecoder(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;
	if (de->packet) {
		av_packet_free(&de->packet);
	}
	if (de->frame) {
		av_frame_free(&de->frame);
	}
	if (de->context) {
		avcodec_free_context(&de->context);
	}
	if (de->parser) {
		av_parser_close(de->parser);
	}
	free(de);

	av_log(NULL, AV_LOG_DEBUG, "releaseDecoder end");
}

void* createDecoder(enum AVCodecID id, FrameCallback cb) {
	if (!cb) {
		av_log(NULL, AV_LOG_ERROR, "no cb.");
		return NULL;
	}
	Decoder* de = malloc(sizeof(Decoder));
	if (!de) {
		av_log(NULL, AV_LOG_ERROR, "malloc fail.");
		return NULL;
	}
	memset(de, 0, sizeof(Decoder));
	de->cb = cb;
	de->codec = avcodec_find_decoder(id);
	if (!de->codec) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_find_decoder fail.");
		releaseDecoder(de);
		return NULL;
	}
	de->parser = av_parser_init(de->codec->id);
	if (!de->parser) {
		av_log(NULL, AV_LOG_ERROR, "av_parser_init fail.");
		releaseDecoder(de);
		return NULL;
	}
	de->context = avcodec_alloc_context3(de->codec);
	if (!de->context) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3 fail.");
		releaseDecoder(de);
		return NULL;
	}
	if (avcodec_open2(de->context, de->codec, NULL) < 0) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_open2 fail.");
		releaseDecoder(de);
		return NULL;
	}
	de->frame = av_frame_alloc();
	if (!de->frame) {
		av_log(NULL, AV_LOG_ERROR, "av_frame_alloc inFrame fail.");
		releaseDecoder(de);
		return NULL;
	}
	de->packet = av_packet_alloc();
	if (!de->packet) {
		av_log(NULL, AV_LOG_ERROR, "av_packet_alloc fail.");
		releaseDecoder(de);
		return NULL;
	}

	av_log(NULL, AV_LOG_DEBUG, "createDecoder %d end", id);
	return de;
}


void* createH264Decoder(FrameCallback cb) {
	return createDecoder(AV_CODEC_ID_H264, cb);
}

void* createH265Decoder(FrameCallback cb) {
	return createDecoder(AV_CODEC_ID_H265, cb);
}

void put(void *ctx, unsigned char* buf, int size) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;

	while (size > 0) {
		av_log(NULL, AV_LOG_TRACE, "put %d", size);
		int n = av_parser_parse2(de->parser, de->context, &de->packet->data, &de->packet->size, buf, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (n < 0) {
			av_log(NULL, AV_LOG_ERROR, "av_parser_parse2 fail %d.", n);
			break;
		}
		av_log(NULL, AV_LOG_TRACE, "av_parser_parse2 ret %d", n);
		buf += n;
		size -= n;
		if (de->packet->size > 0) {
			av_log(NULL, AV_LOG_DEBUG, "packet: size(%d), flag(%d).", de->packet->size, de->packet->flags);
			int ret;
			ret = avcodec_send_packet(de->context, de->packet);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet fail %d.", ret);
				break;
			}
			av_log(NULL, AV_LOG_TRACE, "avcodec_send_packet ret %d", ret);
			while (ret >= 0) {
				ret = avcodec_receive_frame(de->context, de->frame);
				if (ret < 0) {
					if (!(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)) {
						av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame fail %d.", ret);
					}
					break;
				}
				av_log(NULL, AV_LOG_TRACE, "avcodec_receive_frame ret %d", ret);
				de->cb(de->frame->data[0], de->frame->width, de->frame->height);
			}
		}
	}

	return;
}

void flush(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;
	return;
}
