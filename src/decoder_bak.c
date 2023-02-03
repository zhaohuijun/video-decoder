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

void log_context(int level, AVCodecContext* context) {
	// av_log(NULL, level, "ctx: type:%d, id:%d, tag:%d", context->codec_type, context->codec_id, context->codec_tag);
	// av_log(NULL, level, "ctx: bit_rate:%lld, bit_rate_tolerance:%d, global_quality:%d, compression_level:%d", context->bit_rate, context->bit_rate_tolerance, context->global_quality, context->compression_level);
	// av_log(NULL, level, "ctx: flags:%d, flags2:%d, extradata_size:%d, time_base:%d/%d, ticks_per_frame:%d", context->flags, context->flags2, context->extradata_size, context->time_base.num, context->time_base.den, context->ticks_per_frame);
	// av_log(NULL, level, "ctx: delay:%d, width:%d, height:%d, coded_width:%d, coded_height:%d", context->delay, context->width, context->height, context->coded_width, context->coded_height);
	// av_log(NULL, level, "ctx: gop_size:%d, pix_fmt:%d, max_b_frames:%d, b_quant_factor:%f, b_quant_offset:%f, has_b_frames:%d", context->gop_size, context->pix_fmt, context->max_b_frames, context->b_quant_factor, context->b_quant_offset, context->has_b_frames);
	AVCodecParameters par;
	par.extradata = NULL;
	avcodec_parameters_from_context(&par, context);
	av_log(NULL, level, "== ctx == type:%d, id:%d, tag:%d, format:%d", par.codec_type, par.codec_id, par.codec_tag, par.format);
	av_log(NULL, level, "== ctx == br:%lld, bpcs:%d, bprs:%d", par.bit_rate, par.bits_per_coded_sample, par.bits_per_raw_sample);
	av_log(NULL, level, "== ctx == profile:%d, lvl:%d, width:%d, height:%d", par.profile, par.level, par.width, par.height);
	av_log(NULL, level, "== ctx == aspect:%d/%d, field_order:%d", par.sample_aspect_ratio.num, par.sample_aspect_ratio.den, par.field_order);
	av_log(NULL, level, "== ctx == c_range:%d, c_primaries:%d, c_trc:%d, c_space:%d, chroma_location:%d", par.color_range, par.color_primaries, par.color_trc, par.color_space, par.chroma_location);
	av_log(NULL, level, "== ctx == video_delay:%d, channel_layout:%llu, channels:%d, sample_rate:%d", par.video_delay, par.channel_layout, par.channels, par.sample_rate);
	av_log(NULL, level, "== ctx == block_align:%d, frame_size:%d, initial_padding:%d, trailing_padding:%d, seek_preroll:%d", par.block_align, par.frame_size, par.initial_padding, par.trailing_padding, par.seek_preroll);
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
	int ret = 0;
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
	AVCodecParameters par;
	// par.width = 3840;
	// par.height = 2160;
	par.width = 1920;
	par.height = 1080;
	par.profile = 2;
	par.level = 120;
	par.color_range = 1;
	par.color_primaries = 2;
	par.color_trc = 2;
	par.color_space = 2;
	// ret = avcodec_parameters_to_context(de->context, &par);
	// if (ret < 0) {
	// 	av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context fail %d.", ret);
	// 	releaseDecoder(de);
	// 	return NULL;
	// }
	av_log(NULL, AV_LOG_TRACE, "avcodec_parameters_to_context");
	log_context(AV_LOG_DEBUG, de->context);
	// AVDictionary* opts = NULL;
	// av_dict_set(&opts, "image_size", "3840x2160", AV_DICT_MATCH_CASE);
	ret = avcodec_open2(de->context, de->codec, NULL);
	av_log(NULL, AV_LOG_DEBUG, "avcodec_open2 ctx, width:%d, height:%d.", de->context->width, de->context->height);
	// av_dict_free(&opts);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_open2 fail %d.", ret);
		releaseDecoder(de);
		return NULL;
	}
	av_log(NULL, AV_LOG_TRACE, "avcodec_open2");
	log_context(AV_LOG_DEBUG, de->context);
	// de->context->width = 3840;
	// de->context->height = 2160;
	// de->context->coded_width = de->context->width;
	// de->context->coded_height = de->context->height;
	// de->context->pix_fmt = 0;
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

// void putOO(void *ctx, unsigned char* buf, int size) {
// 	if (!ctx) {
// 		return;
// 	}
// 	Decoder* de = (Decoder*)ctx;

// 	int ret;

// 	av_init_packet(de->packet);
// 	de->packet->data = buf;
// 	de->packet->size = size;
// 	int got;
// 	ret = avcodec_decode_video2(de->context, de->frame, &got, de->packet);
// 	av_log(NULL, AV_LOG_TRACE, "avcodec_decode_video2 ret:%d, got:%d, fw:%d, fh:%d", ret, got, de->frame->width, de->frame->height);
// 	log_context(AV_LOG_DEBUG, de->context);
// 	if (got) {
// 		av_log(NULL, AV_LOG_DEBUG, "=============================================================");
// 	}

// 	return;
// }

void putO(void *ctx, unsigned char* buf, int size) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;

	while (size > 0) {
		av_log(NULL, AV_LOG_TRACE, "put %d", size);
		log_context(AV_LOG_DEBUG, de->context);
		int n = av_parser_parse2(de->parser, de->context, &de->packet->data, &de->packet->size, buf, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (n < 0) {
			av_log(NULL, AV_LOG_ERROR, "av_parser_parse2 fail %d.", n);
			break;
		}
		av_log(NULL, AV_LOG_TRACE, "av_parser_parse2 ret %d", n);
		log_context(AV_LOG_DEBUG, de->context);
		buf += n;
		size -= n;
		if (de->packet->size > 0) {
			av_log(NULL, AV_LOG_DEBUG, "packet: size(%d), flag(%d).", de->packet->size, de->packet->flags);
			int ret;
			av_log(NULL, AV_LOG_TRACE, "avcodec_send_packet packet: data:%p, size:%d, buf: %p, buf_size:%d", de->packet->data, de->packet->size, de->packet->buf->data, de->packet->buf->size);
			ret = avcodec_send_packet(de->context, de->packet);
			av_packet_unref(de->packet);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet fail %d.", ret);
				break;
			}
			av_log(NULL, AV_LOG_TRACE, "avcodec_send_packet ret %d", ret);
			log_context(AV_LOG_DEBUG, de->context);
			while (ret >= 0) {
				ret = avcodec_receive_frame(de->context, de->frame);
				if (ret < 0) {
					if (!(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)) {
						av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame fail %d.", ret);
					}
					break;
				}
				av_log(NULL, AV_LOG_TRACE, "avcodec_receive_frame ret %d", ret);
				log_context(AV_LOG_DEBUG, de->context);
				de->cb(de->frame->data[0], de->frame->width, de->frame->height);
			}
		}
	}

	return;
}

void put(void *ctx, unsigned char* buf, int size) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;

	while (size > 0) {
		av_log(NULL, AV_LOG_TRACE, "put %d", size);
		log_context(AV_LOG_DEBUG, de->context);
		int n = av_parser_parse2(de->parser, de->context, &de->packet->data, &de->packet->size, buf, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (n < 0) {
			av_log(NULL, AV_LOG_ERROR, "av_parser_parse2 fail %d.", n);
			break;
		}
		av_log(NULL, AV_LOG_TRACE, "av_parser_parse2 ret %d", n);
		log_context(AV_LOG_DEBUG, de->context);
		buf += n;
		size -= n;
		if (de->packet->size > 0) {
			av_log(NULL, AV_LOG_DEBUG, "packet: size(%d), flag(%d).", de->packet->size, de->packet->flags);
			int ret;
			av_log(NULL, AV_LOG_TRACE, "avcodec_send_packet packet: data:%p, size:%d, buf: %p, buf_size:%d", 
				de->packet->data, de->packet->size, de->packet->buf->data, de->packet->buf->size);
			ret = avcodec_send_packet(de->context, de->packet);
			av_packet_unref(de->packet);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet fail %d.", ret);
				break;
			}
			av_log(NULL, AV_LOG_TRACE, "avcodec_send_packet ret %d", ret);
			log_context(AV_LOG_DEBUG, de->context);
			while (ret >= 0) {
				ret = avcodec_receive_frame(de->context, de->frame);
				if (ret < 0) {
					if (!(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)) {
						av_log(NULL, AV_LOG_ERROR, "avcodec_receive_frame fail %d.", ret);
					}
					break;
				}
				av_log(NULL, AV_LOG_TRACE, "avcodec_receive_frame ret %d", ret);
				log_context(AV_LOG_DEBUG, de->context);
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
