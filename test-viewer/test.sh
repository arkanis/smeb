#!/bin/bash
FFMPEG_OPTS="-v warning -i -"
ENCODER_OPTS="-quality realtime -threads 3"
OUTPUT_OPTS="-f webm -chunked_post 0"
ffmpeg -re -noaccurate_seek -ss 0:00 -i "$1" -map 0:0 -map 0:1 -c:v rawvideo -c:a pcm_s16le -f matroska - | tee \
	>( ffmpeg $FFMPEG_OPTS                       $ENCODER_OPTS -minrate 3M -maxrate 3M -b:v 3M       $OUTPUT_OPTS http://localhost:1234/test.1080p.webm ) \
	>( ffmpeg $FFMPEG_OPTS -vf scale=w=1280:h=-1 $ENCODER_OPTS -minrate 1M -maxrate 1M -b:v 1M       $OUTPUT_OPTS http://localhost:1234/test.720p.webm ) \
	|  ffmpeg $FFMPEG_OPTS -vf scale=w=1024:h=-1 $ENCODER_OPTS -minrate 300K -maxrate 300K -b:v 300K $OUTPUT_OPTS http://localhost:1234/test.sd.webm