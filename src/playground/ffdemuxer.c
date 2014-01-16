#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>
#include <fcntl.h>

#include <libavformat/avformat.h>
#include <libavutil/error.h>


int main(int argc, char** argv) {
	if (argc != 2)
		fprintf(stderr, "usage: %s webm-file\n", argv[0]), exit(1);
	
	char errmsg[512];
	// Switch stdin to non-blocking IO to test out a non-blocking av_read_frame()
	if ( fcntl(0, F_SETFL, fcntl(0, F_GETFL, NULL) | O_NONBLOCK) == -1 )
		perror("fcntl"), exit(1);
	
	av_register_all();
	
	AVInputFormat* webm_fmt = av_find_input_format("webm");
	AVFormatContext* demuxer = avformat_alloc_context();
	demuxer->flags |= AVFMT_FLAG_NONBLOCK;
	int error = avformat_open_input(&demuxer, argv[1], webm_fmt, NULL);
	//int error = avformat_open_input(&demuxer, "pipe:0", webm_fmt, NULL);
	if (error < 0)
		fprintf(stderr, "avformat_open_input(): %s\n", av_make_error_string(errmsg, sizeof(errmsg), error)), exit(1);
	
	printf("found %d streams:\n", demuxer->nb_streams);
	for(size_t i = 0; i < demuxer->nb_streams; i++) {
		AVStream* stream = demuxer->streams[i];
		printf("%d: time base %d/%d, codec: %s, extradata: %p, %d bytes\n",
			stream->index, stream->time_base.num, stream->time_base.den,
			stream->codec->codec_name, stream->codec->extradata, stream->codec->extradata_size);
		switch (stream->codec->codec_type) {
			case AVMEDIA_TYPE_VIDEO:
				printf("   video, w: %d, h: %d, sar: %d/%d, %dx%d\n",
					stream->codec->width, stream->codec->height, stream->sample_aspect_ratio.num, stream->sample_aspect_ratio.den,
					stream->codec->width * stream->sample_aspect_ratio.num / stream->sample_aspect_ratio.den, stream->codec->height);
				break;
			case AVMEDIA_TYPE_AUDIO:
				printf("   audio, %d channels, sampel rate: %d, bits per sample: %d\n",
					stream->codec->channels, stream->codec->sample_rate, stream->codec->bits_per_coded_sample);
				break;
			default:
				break;
		}
	}
	
	AVPacket packet;
	int ret =0;
	while (true) {
		ret = av_read_frame(demuxer, &packet);
		if (ret == AVERROR(EAGAIN)) {
			printf("sleep\n");
			struct timespec duration = {0, 250 * 1000000};
			nanosleep(&duration, NULL);
			continue;
		} else if (ret != 0) {
			break;
		}
		
		if (packet.flags & AV_PKT_FLAG_KEY && packet.stream_index == 0)
			printf("keyframe: stream %d, pts: %lu, dts: %lu, duration: %d, buf: %p\n", packet.stream_index, packet.pts, packet.dts, packet.duration, packet.buf);
		
		av_free_packet(&packet);
	}
	
	avformat_close_input(&demuxer);
	
	return 0;
}