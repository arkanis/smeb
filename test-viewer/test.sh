#!/bin/bash
ffmpeg -re -i "$1" -map 0:0 -map 0:1 -c:v rawvideo -c:a pcm_s16le -f matroska - | tee \
	>( ffmpeg -v warning -i -                       -quality realtime -minrate 3M -maxrate 3M -b:v 3M -threads 3 -chunked_post 0 http://localhost:1234/1080p.webm ) \
	>( ffmpeg -v warning -i - -vf scale=w=1280:h=-1 -quality realtime -minrate 1M -maxrate 1M -b:v 1M -threads 3 -chunked_post 0 http://localhost:1234/720p.webm ) \
	|  ffmpeg -v warning -i - -vf scale=w=1024:h=-1 -quality realtime -minrate 300K -maxrate 300K -b:v 300K -threads 3 -chunked_post 0 http://localhost:1234/sd.webm