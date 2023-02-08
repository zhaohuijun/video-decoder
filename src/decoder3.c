 /**
  * @file
  * video decoding with libavcodec API example
  *
  * decode_video3.c
	* 针对多线程，接口要重新写
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <pthread.h>

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
	struct timeval tm;
	gettimeofday(&tm, NULL);
	snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%ld.%06ld]%s", tm.tv_sec, tm.tv_usec, logLevelStr(level));
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

typedef void (*FrameCallback)(void *opaque, Frame *frame);

// 链表，用来存buf
typedef struct _BufferList BufferList;
struct _BufferList
{
	BufferList *next;
	unsigned char *buf;
	int len;
};

// 链表，用来存结果frame
typedef struct _FrameList FrameList;
struct _FrameList
{
	FrameList *next;
	Frame *frame;
};

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
	Frame* latestFrame;	// 最新的一帧解码结果
	pthread_t decodeThread;	// 线程
	pthread_mutex_t bufferMutex;
	pthread_mutex_t frameMutex;
	BufferList *bufferHead;
	BufferList *bufferTail;
	FrameList *frameHead;
	FrameList *frameTail;
	int needStop; // 要结束 
} Decoder;

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
		(const uint8_t **)(de->frameYUV->data), de->frameYUV->linesize,
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

// 操作frame的list
int putFrame(void *ctx, Frame* frame) {
	if (!ctx) {
		return -1;
	}
	Decoder* de = (Decoder*)ctx;

	FrameList *item = malloc(sizeof(FrameList));
	if (!item) {
		av_log(NULL, AV_LOG_ERROR, "malloc err in putFrame\n");
		return -1;
	}
	pthread_mutex_lock(&de->frameMutex);
	item->next = NULL;
	item->frame = frame;
	if (de->frameTail == NULL) {
		// 空链
		de->frameHead = item;
		de->frameTail = item;
	} else {
		de->frameTail->next = item;
		de->frameTail = item;
	}
	pthread_mutex_unlock(&de->frameMutex);
	return 0;
}

Frame* getFrame(void *ctx) {
	if (!ctx) {
		return NULL;
	}
	Decoder* de = (Decoder*)ctx;

	Frame *ret = NULL;
	pthread_mutex_lock(&de->frameMutex);
	FrameList *head = de->frameHead;
	if (head) {
		ret = head->frame;
		de->frameHead = head->next;
		if (de->frameHead == NULL) {
			de->frameTail = NULL;
		}
		free(head);
	}
	pthread_mutex_unlock(&de->frameMutex);
	return ret;
}

// 输入新的数据
int putBuffer(void *ctx, unsigned char *buf, int len) {
	av_log(NULL, AV_LOG_DEBUG, "putBuffer %d\n", len);
	if (!ctx) {
		return -1;
	}
	Decoder* de = (Decoder*)ctx;
	BufferList *item = malloc(sizeof(BufferList));
	if (!item) {
		av_log(NULL, AV_LOG_ERROR, "malloc err in putBuffer\n");
		return -1;
	}
	pthread_mutex_lock(&de->bufferMutex);
	item->next = NULL;
	item->buf = buf;
	item->len = len;
	if (de->bufferTail == NULL) {
		// 空链
		de->bufferHead = item;
		de->bufferTail = item;
	} else {
		de->bufferTail->next = item;
		de->bufferTail = item;
	}
	pthread_mutex_unlock(&de->bufferMutex);
	return 0;
}

// 读buffer
int readBuffer(void *ctx, unsigned char* buf, int buf_size) {
	int ret = -(0x20464F45); // 'EOF '
	if (!ctx) {
		return -1;
	}
	Decoder* de = (Decoder*)ctx;
	av_log(NULL, AV_LOG_DEBUG, "readBuffer %p %p\n", de->bufferHead, de->bufferTail);
	
	pthread_mutex_lock(&de->bufferMutex);
	BufferList *head = de->bufferHead;
	if (head) {		
		if (buf_size >= head->len) {
			// 取出第一块buf
			memcpy(buf, head->buf, head->len);
			ret = head->len;
			de->bufferHead = head->next;
			if (de->bufferHead == NULL) {
				// 取空了
				de->bufferTail = NULL;
			}
			// 释放内存
			free(head->buf);	// 这个内存是js里面分配的
			free(head);
		} else {
			memcpy(buf, head->buf, buf_size);
			int nlen = head->len - buf_size;
			memmove(head->buf, head->buf + buf_size, nlen);
			head->len = nlen;
			ret = buf_size;
		}
	}
	pthread_mutex_unlock(&de->bufferMutex);
	return ret;
}

void *decodeThreadFun(void *ctx) {
	if (!ctx) {
		av_log(NULL, AV_LOG_ERROR, "decodeThreadFun without ctx.\n"); 
		return NULL;
	}
	Decoder* de = (Decoder*)ctx;
	while (!de->needStop){
		// av_log(NULL, AV_LOG_DEBUG, "decodeThreadFun while.\n");
		// 读数据
		int n = readBuffer(de, de->io_buffer, de->io_buffer_size);
		if (n <= 0) {
			usleep(10000);
			continue;
		}
		av_log(NULL, AV_LOG_DEBUG, "decodeThreadFun new data %d.\n", n);
		// parse
		uint8_t* buf = de->io_buffer;
		while (n > 0) {
			int ret = av_parser_parse2(de->parser, de->ctx, &(de->packet->data), &(de->packet->size), 
				buf, n, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
			av_log(NULL, AV_LOG_DEBUG, "decodeThreadFun av_parser_parse2 ret %d.\n", ret);
			if (ret < 0) {
				av_log(NULL, AV_LOG_VERBOSE, "av_parser_parse2 ret %d\n", ret);
				break;
			}
			buf += ret;
			n -= ret;
			if (de->packet->size > 0) {
				ret = avcodec_send_packet(de->ctx, de->packet);
				av_log(NULL, AV_LOG_DEBUG, "decodeThreadFun avcodec_send_packet ret %d.\n", ret);
				if (ret < 0) {
					av_log(NULL, AV_LOG_VERBOSE, "avcodec_send_packet ret: %d\n", ret);
				}
			}
		}
		while (1) {
			Frame *f = recvFrame(de);
			if (!f) {
				av_log(NULL, AV_LOG_DEBUG, "no new frame\n");
				break;
			}
			av_log(NULL, AV_LOG_DEBUG, "got frame\n");
			putFrame(de, f);
		}
	}
	return NULL;
}

void releaseDecoder(void *ctx) {
	if (!ctx) {
		return;
	}
	Decoder* de = (Decoder*)ctx;
	de->needStop = 1; // 停止线程
	pthread_join(de->decodeThread, NULL);
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

void* createDecoder(const char* fmt_name, enum AVCodecID type_id) {
	int ret = 0;

	Decoder* de = malloc(sizeof(Decoder));
	if (!de) {
		av_log(NULL, AV_LOG_ERROR, "malloc fail.");
		return NULL;
	}
	memset(de, 0, sizeof(Decoder));

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

	// 队列
	de->bufferHead = NULL;
	de->bufferTail = NULL;
	de->frameHead = NULL;
	de->frameTail = NULL;

	// 创建解码线程
	pthread_mutex_init(&de->bufferMutex, NULL);
	pthread_mutex_init(&de->frameMutex, NULL);
	ret = pthread_create(&de->decodeThread, NULL, decodeThreadFun, de);
	if (ret != 0) {
		av_log(NULL, AV_LOG_ERROR, "pthread_create fail %d.\n", ret);
		releaseDecoder(de);
		return NULL;
	}
	
	return de;
}

void* createH264Decoder() {
	return createDecoder("h264", AV_CODEC_ID_H264);
}

void* createH265Decoder() {
	return createDecoder("hevc", AV_CODEC_ID_H265);
}

#if 0

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

Frame* getFrame(void *ctx) {
	int ret;
	Frame* f;

	if (!ctx) {
		return NULL;
	}
	Decoder* de = (Decoder*)ctx;

	av_log(NULL, AV_LOG_DEBUG, "getFrame begin\n");
	if (!de->found_info) {
		av_log(NULL, AV_LOG_ERROR, "need findStreamInfo first\n");
		return NULL;
	}

	// av_log(NULL, AV_LOG_DEBUG, "getFrame 1\n");
	f = recvFrame(de);
	// av_log(NULL, AV_LOG_DEBUG, "getFrame 2\n");
	if (f) {
		av_log(NULL, AV_LOG_DEBUG, "getFrame end f\n");
		return f;
	}
	// av_log(NULL, AV_LOG_DEBUG, "getFrame 3\n");

	while (1) {
		// av_log(NULL, AV_LOG_DEBUG, "getFrame 4\n");
		int n = de->io_read_cb(de, de->io_buffer, de->io_buffer_size);
		// av_log(NULL, AV_LOG_DEBUG, "getFrame 4.4\n");
		if (n <= 0) {
			av_log(NULL, AV_LOG_DEBUG, "getFrame end NULL (no data) (%d)\n", n);
			return NULL;
		}
		uint8_t* buf = de->io_buffer;

		// av_log(NULL, AV_LOG_DEBUG, "getFrame 5\n");
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

		// av_log(NULL, AV_LOG_DEBUG, "getFrame 6\n");
		f = recvFrame(de);
		// av_log(NULL, AV_LOG_DEBUG, "getFrame 7\n");
		if (f) {
			av_log(NULL, AV_LOG_DEBUG, "getFrame end f 2\n");
			return f;
		}
	}
	av_log(NULL, AV_LOG_DEBUG, "getFrame end NULL 2\n");
	return NULL;
}

void *getFrameThreadFun(void *ctx) {
	av_log(NULL, AV_LOG_DEBUG, "getFrameThreadFun begin\n");
	Frame* f = getFrame(ctx);
	Decoder* de = (Decoder*)ctx;
	de->latestFrame = f;
	av_log(NULL, AV_LOG_DEBUG, "getFrameThreadFun end\n");
	return NULL;
}

Frame* getFrameMT(void *ctx) {
	if (!ctx) {
		return NULL;
	}
	av_log(NULL, AV_LOG_DEBUG, "getFrameMT begin\n");
	pthread_t bg_thread;
	int ret = pthread_create(&bg_thread, NULL, getFrameThreadFun, ctx);
	av_log(NULL, AV_LOG_DEBUG, "pthread_create ret: %d\n", ret);
	if (ret != 0) {
		av_log(NULL, AV_LOG_ERROR	, "pthread_create ret: %d\n", ret);
		return NULL;
	}
	ret = pthread_join(bg_thread, NULL);
	if (ret != 0) {
		av_log(NULL, AV_LOG_ERROR	, "pthread_join ret: %d\n", ret);
		return NULL;
	}
	Decoder* de = (Decoder*)ctx;
	av_log(NULL, AV_LOG_DEBUG, "getFrameMT end\n");
	return de->latestFrame;
}

#endif