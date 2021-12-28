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
	AVFormatContext* fmt; 	// 流封装
	AVCodec* codec;			// 编码
	int stream_index;		// 视频流的序号
	AVCodecContext* ctx;	// 解码句柄
	AVPacket* packet;		// 数据帧
	AVFrame* frame;			// 接触的图片帧
} Decoder;

void releaseDecoder(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;
	if (de->packet) {
		av_packet_free(&(de->packet));
		de->packet = NULL;
	}
	if (de->frame) {
		av_frame_free(&(de->frame));
		de->frame = NULL;
	}
	if (de->ctx) {
		avcodec_close(de->ctx);
		avcodec_free_context(&(de->ctx));
		de->ctx = NULL;
	}
	if (de->fmt) {
		avformat_close_input(&(de->fmt));
		if (de->fmt) {
			avformat_free_context(de->fmt);
		}
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

void* createDecoder(const char* fmt_name, IOReadCallback cb) {
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

	log_fmts(AV_LOG_DEBUG);

	AVInputFormat *input_format = av_find_input_format(fmt_name);
    if (!input_format) {
        av_log(NULL, AV_LOG_ERROR, "av_find_input_format fail.\n");
		releaseDecoder(de);
        return NULL;
    }
    av_log(NULL, AV_LOG_DEBUG, "input_format: %s, %s, %s.\n", 
        input_format->name, input_format->long_name, input_format->mime_type);

    de->io_buffer_size = 4096;
    de->io_buffer = av_malloc(de->io_buffer_size);
	if (!de->io_buffer) {
        av_log(NULL, AV_LOG_ERROR, "av_malloc fail.\n");
		releaseDecoder(de);
        return NULL;
    }
	de->io_ctx = avio_alloc_context(de->io_buffer, de->io_buffer_size, 0, NULL, de->io_read_cb, NULL, NULL);
	if (!de->io_ctx) {
        av_log(NULL, AV_LOG_ERROR, "avio_alloc_context fail.\n");
		releaseDecoder(de);
        return NULL;
    }

    de->fmt = avformat_alloc_context();
    if (!de->fmt) {
        av_log(NULL, AV_LOG_ERROR, "avformat_alloc_context fail.\n");
		releaseDecoder(de);
        return NULL;
    }
	de->fmt->pb = de->io_ctx;

	ret = avformat_open_input(&(de->fmt), NULL, input_format, NULL);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "avformat_open_input fail %d.\n", ret);
		releaseDecoder(de);
        return NULL;
	}

	ret = avformat_find_stream_info(de->fmt, NULL);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info fail %d.\n", ret);
		releaseDecoder(de);
        return NULL;
	}

	ret = av_find_best_stream(de->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &(de->codec), 0);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "av_find_best_stream fail %d.\n", ret);
		releaseDecoder(de);
        return NULL;
	}
	de->stream_index = ret;

	av_log(NULL, AV_LOG_DEBUG, "stream: %d, codec: %d\n", de->stream_index, de->codec->id);

	log_parameters(AV_LOG_DEBUG, de->fmt->streams[de->stream_index]->codecpar);

	de->ctx = avcodec_alloc_context3(de->codec);
	if (!de->ctx) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3 fail.\n");
		releaseDecoder(de);
        return NULL;
    }

	ret = avcodec_parameters_to_context(de->ctx, de->fmt->streams[de->stream_index]->codecpar);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context fail %d.\n", ret);
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

	de->frame = av_frame_alloc();
	if (!de->frame) {
		av_log(NULL, AV_LOG_ERROR, "av_frame_alloc inFrame fail.");
		releaseDecoder(de);
		return NULL;
	}
	
	return de;
}

void* createH264Decoder(IOReadCallback cb) {
	return createDecoder("h264", cb);
}

void* createH265Decoder(IOReadCallback cb) {
	return createDecoder("hevc", cb);
}

void* 

Frame* recvFrame(AVCodecContext* ctx, AVFrame* frame) {
	int ret = avcodec_receive_frame(ctx, frame);
	if (ret < 0) {
		av_log(NULL, AV_LOG_DEBUG, "avcodec_receive_frame ret: %d\n", ret);
		return NULL;
	}
	int size = frame->width * frame->height;
	size <<= 2; // 一个像素4个byte，rgba
	Frame* f = malloc(sizeof(Frame) + size);
	if (!f) {
		av_log(NULL, AV_LOG_ERROR, "av_malloc err: %d\n", size);
		return NULL;
	}
	f->width = frame->width;
	f->height = frame->height;
	av_frame_unref(frame);
	return f;
}

Frame* getFrame(void *ctx) {
	int ret;
	Frame* f;

	if (!ctx) {
		return NULL;
	}
	Decoder* de = (Decoder*)ctx;

	f = recvFrame(de->ctx, de->frame);
	if (f) {
		return f;
	}

	ret = av_read_frame(de->fmt, de->packet);
	if (ret < 0) {
		av_log(NULL, AV_LOG_VERBOSE, "no fmt frame, av_read_frame ret %d\n", ret);
		return NULL;
	}

	ret = avcodec_send_packet(de->ctx, de->packet);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet err: %d\n", ret);
		return NULL;
	}

	f = recvFrame(de->ctx, de->frame);
	if (f) {
		return f;
	}
	
	return NULL;
}
