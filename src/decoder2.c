 /**
  * @file
  * video decoding with libavcodec API example
  *
  * decode_video.c
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>

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

static void log_parameters(int level, AVCodecParameters* pars) {
	av_log(NULL, level, "== ctx == type:%d, id:%d, tag:%d, format:%d\n", pars->codec_type, pars->codec_id, pars->codec_tag, pars->format);
	av_log(NULL, level, "== ctx == br:%lld, bpcs:%d, bprs:%d\n", pars->bit_rate, pars->bits_per_coded_sample, pars->bits_per_raw_sample);
	av_log(NULL, level, "== ctx == profile:%d, lvl:%d, width:%d, height:%d\n", pars->profile, pars->level, pars->width, pars->height);
	av_log(NULL, level, "== ctx == aspect:%d/%d, field_order:%d\n", pars->sample_aspect_ratio.num, pars->sample_aspect_ratio.den, pars->field_order);
	av_log(NULL, level, "== ctx == c_range:%d, c_primaries:%d, c_trc:%d, c_space:%d, chroma_location:%d\n", pars->color_range, pars->color_primaries, pars->color_trc, pars->color_space, pars->chroma_location);
	av_log(NULL, level, "== ctx == video_delay:%d, channel_layout:%llu, channels:%d, sample_rate:%d\n", pars->video_delay, pars->channel_layout, pars->channels, pars->sample_rate);
	av_log(NULL, level, "== ctx == block_align:%d, frame_size:%d, initial_padding:%d, trailing_padding:%d, seek_preroll:%d\n", pars->block_align, pars->frame_size, pars->initial_padding, pars->trailing_padding, pars->seek_preroll);
}

static void log_fmts(int level) {
	const AVInputFormat *fmt = NULL;
	void *i = 0;
    do {
		fmt = av_demuxer_iterate(&i);
		if (fmt) {
			av_log(NULL, level, "== fmt ==: %s, %s, %s\n", fmt->name, fmt->long_name, fmt->mime_type);
		}
	} while (fmt);
}

typedef int (*IOReadCallback)(void *opaque, unsigned char* buf, int buf_size);

typedef struct {
	int width;
	int height;
	unsigned char buf[];
} Frame;

typedef struct {
	IOReadCallback io_read_cb;	// 读数据的回调
	AVIOContext* io_ctx;	// avio
    size_t io_buffer_size;	// 缓存(avio用的)大小
    uint8_t* io_buffer;		// 缓存(avio用的)
	AVInputFormat* input_format;	// 封装格式
	AVFormatContext* fmt; 	// 流封装
	AVCodecParserContext* parser;	// 相当于fmt
	AVCodec* codec;			// 编码
	int stream_index;		// 视频流的序号
	AVCodecContext* ctx;	// 解码句柄
	AVPacket* packet;		// 数据帧
	AVFrame* frameYUV;		// 解出的图片帧
	AVFrame* frameRGBA;		// 解出的图片帧
	struct SwsContext* sws;	// 转格式
	int width;				// 图像宽度
	int height;				// 图像高度
	int found_info;			// 找到流信息
	int offset;				// 已经处理过的数据
} Decoder;

void releaseDecoder(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;
	if (de->sws) {
		sws_freeContext(de->sws);
		de->sws = NULL;
	}
	if (de->packet) {
		av_packet_free(&(de->packet));
		de->packet = NULL;
	}
	if (de->frameYUV) {
		av_frame_free(&(de->frameYUV));
		de->frameYUV = NULL;
	}
	if (de->frameRGBA) {
		av_frame_free(&(de->frameRGBA));
		de->frameRGBA = NULL;
	}
	if (de->ctx) {
		avcodec_close(de->ctx);
		avcodec_free_context(&(de->ctx));
		de->ctx = NULL;
	}
	if (de->parser) {
		av_parser_close(de->parser);
		de->parser = NULL;
	}
	if (de->fmt) {
		avformat_close_input(&(de->fmt));
		de->fmt = NULL;
	}
	if (de->io_ctx) {
		avio_context_free(&(de->io_ctx));
		de->io_ctx = NULL;
	}
	if (de->io_buffer) {
		av_free(de->io_buffer);
		de->io_buffer = NULL;
	}
	free(de);

	av_log(NULL, AV_LOG_DEBUG, "releaseDecoder end");
}

int io_read_cb_wrapper(void *opaque, unsigned char* buf, int buf_size) {
	if (!opaque) {
		return AVERROR_EXIT;
	}
	Decoder* de = (Decoder*)opaque;
	int ret = de->io_read_cb(NULL, buf, buf_size);
	if (ret > 0) {
		av_log(NULL, AV_LOG_TRACE, "io_read: off:%d, len:%d, first:%x, end:%x", 
			de->offset, ret, buf[0], buf[ret-1]);
		de->offset += ret;
	}
	return ret;
}

void* createDecoder(const char* fmt_name, enum AVCodecID type_id, IOReadCallback cb) {
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
	de->io_read_cb = cb;

	de->codec = avcodec_find_decoder(type_id);
    if (!de->codec) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_find_decoder fail.\n");
        return NULL;
    }

	de->parser = av_parser_init(type_id);
	if (!de->parser) {
        av_log(NULL, AV_LOG_ERROR, "av_parser_init fail.\n");
        return NULL;
    }

	de->ctx = avcodec_alloc_context3(de->codec);
	if (!de->ctx) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3 fail.\n");
		releaseDecoder(de);
        return NULL;
    }

	ret = avcodec_open2(de->ctx, de->codec, NULL);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_open2 fail %d.\n", ret);
		releaseDecoder(de);
        return NULL;
	}

	de->packet = av_packet_alloc();
	if (!de->packet) {
		av_log(NULL, AV_LOG_ERROR, "av_packet_alloc fail.");
		releaseDecoder(de);
		return NULL;
	}

	de->frameYUV = av_frame_alloc();
	if (!de->frameYUV) {
		av_log(NULL, AV_LOG_ERROR, "av_frame_alloc YUV fail.");
		releaseDecoder(de);
		return NULL;
	}
	de->frameRGBA = av_frame_alloc();
	if (!de->frameRGBA) {
		av_log(NULL, AV_LOG_ERROR, "av_frame_alloc RGBA fail.");
		releaseDecoder(de);
		return NULL;
	}

	de->io_buffer_size = 4096;
	de->io_buffer = av_malloc(de->io_buffer_size);
	if (!de->io_buffer) {
        av_log(NULL, AV_LOG_ERROR, "av_malloc fail.\n");
		releaseDecoder(de);
		return NULL;
    }
	
	return de;
}

void* createH264Decoder(IOReadCallback cb) {
	return createDecoder("h264", AV_CODEC_ID_H264, cb);
}

void* createH265Decoder(IOReadCallback cb) {
	return createDecoder("hevc", AV_CODEC_ID_H265, cb);
}

int streamInfoReady(void *ctx) {
	if (!ctx) {
		return 0;
	}
	Decoder* de = (Decoder*)ctx;
	return de->found_info;
}

int findStreamInfo(void* ctx) {
	int ret;

	if (!ctx) {
		return AVERROR_EXTERNAL;
	}
	Decoder* de = (Decoder*)ctx;

	de->found_info = 1;

	return 0;
}

Frame* recvFrameRGBAOld(Decoder* de) {
	int ret = avcodec_receive_frame(de->ctx, de->frameYUV);
	if (ret < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avcodec_receive_frame ret: %d\n", ret);
		return NULL;
	}
	// 拿到的图片是yuv的，转rgba
	if (de->sws) {
		if (de->width != de->frameYUV->width || de->height != de->frameYUV->height) {
			sws_freeContext(de->sws);
			de->sws = NULL;
		}
	}
	if (!de->sws) {
		de->sws = sws_getContext(
			de->frameYUV->width, de->frameYUV->height, AV_PIX_FMT_YUV420P,
			de->frameYUV->width, de->frameYUV->height, AV_PIX_FMT_RGBA,
			0, NULL, NULL, NULL);
		if (!de->sws) {
			av_log(NULL, AV_LOG_ERROR, "sws_getContext fail.");
			return NULL;
		}
		de->width = de->frameYUV->width;
		de->height = de->frameYUV->height;
	}
	de->frameRGBA->width = de->frameYUV->width;
	de->frameRGBA->height = de->frameYUV->height;
	de->frameRGBA->format = AV_PIX_FMT_RGBA;
	av_frame_get_buffer(de->frameRGBA, 0);
	ret = sws_scale(de->sws, 
		de->frameYUV->data, de->frameYUV->linesize,
		0, de->frameYUV->height, 
		de->frameRGBA->data, de->frameRGBA->linesize);
	av_frame_unref(de->frameYUV);
	if (ret < 0) {
		av_log(NULL, AV_LOG_DEBUG, "sws_scale_frame ret: %d\n", ret);
		return NULL;
	}
	// 创建返回用的对象
	int size = de->frameRGBA->width * de->frameRGBA->height;
	size <<= 2; // 一个像素4个byte，rgba
	Frame* f = malloc(sizeof(Frame) + size);
	if (!f) {
		av_log(NULL, AV_LOG_ERROR, "av_malloc err: %d\n", size);
		return NULL;
	}
	f->width = de->frameRGBA->width;
	f->height = de->frameRGBA->height;
	memcpy(f->buf, de->frameRGBA->buf[0]->data, de->frameRGBA->buf[0]->size);
	av_frame_unref(de->frameRGBA);
	return f;
}

Frame* recvFrameMONO(Decoder* de) {
	int ret = avcodec_receive_frame(de->ctx, de->frameYUV);
	if (ret < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avcodec_receive_frame ret: %d\n", ret);
		return NULL;
	}
	// 创建返回用的对象
	int size = de->frameYUV->width * de->frameYUV->height;
	size <<= 2; // 一个像素4个byte，rgba
	Frame* f = malloc(sizeof(Frame) + size);
	if (!f) {
		av_log(NULL, AV_LOG_ERROR, "av_malloc err: %d\n", size);
		return NULL;
	}
	// av_log(NULL, AV_LOG_ERROR, "buf size: %d\n", size);
	f->width = de->frameYUV->width;
	f->height = de->frameYUV->height;
	// memcpy(f->buf, de->frameYUV->buf[0]->data, de->frameRGBA->buf[0]->size);
	// memset(f->buf, 255, size);
	for (int y = 0; y < f->height; y++) {
		for (int x = 0; x < f->width; x++) {
			unsigned char v = de->frameYUV->buf[0]->data[y * f->width + x];
			int p = y * f->width * 4 + x * 4;
			f->buf[p] = v;
			f->buf[p+1] = 0;
			f->buf[p+2] = 0;
			f->buf[p+3] = 255;
		}
	}
	av_frame_unref(de->frameYUV);
	return f;
}

Frame* recvFrame(Decoder* de) {
	int ret = avcodec_receive_frame(de->ctx, de->frameYUV);
	if (ret < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avcodec_receive_frame ret: %d\n", ret);
		return NULL;
	}
	int width = de->frameYUV->width;
	int height = de->frameYUV->height;
	// 创建返回用的对象
	int size = (width * height) << 2;	// 一个像素4个byte，rgba
	Frame* f = malloc(sizeof(Frame) + size);
	if (!f) {
		av_log(NULL, AV_LOG_ERROR, "av_malloc err: %d\n", size);
		return NULL;
	}
	f->width = width;
	f->height = height;
	// 拿到的图片是yuv的，转rgba
	if (de->sws) {
		if (de->width != width || de->height != height) {
			sws_freeContext(de->sws);
			de->sws = NULL;
		}
	}
	if (!de->sws) {
		de->sws = sws_getContext(
			width, height, AV_PIX_FMT_YUV420P,
			width, height, AV_PIX_FMT_RGBA,
			0, NULL, NULL, NULL);
		if (!de->sws) {
			av_log(NULL, AV_LOG_ERROR, "sws_getContext fail.");
			return NULL;
		}
		de->width = width;
		de->height = height;
	}
	
	uint8_t* dstSlice[AV_NUM_DATA_POINTERS] = {f->buf};
	int dstStride[AV_NUM_DATA_POINTERS] = {width<<2};
	// av_log(NULL, AV_LOG_ERROR, "dstSlice: %p, %p, %p, %p, %p, %p, %p, %p\n", dstSlice[0], dstSlice[1], dstSlice[2], dstSlice[3], dstSlice[4], dstSlice[5], dstSlice[6], dstSlice[7]);
	// av_log(NULL, AV_LOG_ERROR, "dstStride: %d, %d, %d, %d, %d, %d, %d, %d\n", dstStride[0], dstStride[1], dstStride[2], dstStride[3], dstStride[4], dstStride[5], dstStride[6], dstStride[7]);
	// av_log(NULL, AV_LOG_ERROR, "srcSlice: %p, %p, %p, %p, %p, %p, %p, %p\n", de->frameYUV->data[0], de->frameYUV->data[1], de->frameYUV->data[2], de->frameYUV->data[3], de->frameYUV->data[4], de->frameYUV->data[5], de->frameYUV->data[6], de->frameYUV->data[7]);
	// av_log(NULL, AV_LOG_ERROR, "srcStride: %d, %d, %d, %d, %d, %d, %d, %d\n", de->frameYUV->linesize[0], de->frameYUV->linesize[1], de->frameYUV->linesize[2], de->frameYUV->linesize[3], de->frameYUV->linesize[4], de->frameYUV->linesize[5], de->frameYUV->linesize[6], de->frameYUV->linesize[7]);
	ret = sws_scale(de->sws, 
		de->frameYUV->data, de->frameYUV->linesize,
		0, height, 
		dstSlice, dstStride);
	// av_log(NULL, AV_LOG_ERROR, "sws_scale end\n");
	if (ret < 0) {
		av_log(NULL, AV_LOG_DEBUG, "sws_scale_frame ret: %d\n", ret);
		return NULL;
	}
	av_frame_unref(de->frameYUV);
	return f;
}

Frame* getFrame(void *ctx) {
	int ret;
	Frame* f;

	if (!ctx) {
		return NULL;
	}
	Decoder* de = (Decoder*)ctx;

	if (!de->found_info) {
		av_log(NULL, AV_LOG_ERROR, "need findStreamInfo first\n");
		return NULL;
	}

	f = recvFrame(de);
	if (f) {
		return f;
	}

	while (1) {
		int n = de->io_read_cb(de, de->io_buffer, de->io_buffer_size);
		if (n <= 0) {
			av_log(NULL, AV_LOG_TRACE, "no data\n");
			// // flush, flush之后就不能再灌入数据了
			// ret = avcodec_send_packet(de->ctx, NULL);
			// if (ret < 0) {
			// 	av_log(NULL, AV_LOG_VERBOSE, "flush avcodec_send_packet ret: %d\n", ret);
			// 	return NULL;
			// }
			// return recvFrame(de);
			return NULL;
		}
		uint8_t* buf = de->io_buffer;

		while (n > 0)
		{
			ret = av_parser_parse2(de->parser, de->ctx, 
				&(de->packet->data), &(de->packet->size), 
				buf, n, 
				AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
			if (ret < 0) {
				av_log(NULL, AV_LOG_VERBOSE, "av_parser_parse2 ret %d\n", ret);
				break;
			}
			buf += ret;
			n -= ret;
			if (de->packet->size > 0) {
				ret = avcodec_send_packet(de->ctx, de->packet);
				if (ret < 0) {
					av_log(NULL, AV_LOG_VERBOSE, "avcodec_send_packet ret: %d\n", ret);
				}
			}
		}

		f = recvFrame(de);
		if (f) {
			return f;
		}
	}
	return NULL;
}
