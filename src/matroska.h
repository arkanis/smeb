#pragma once

//
// Matroska element IDs and values
// http://matroska.org/technical/specs/index.html
//

#define MKV_EBML           0x1A45DFA3
#define MKV_DocType        0x4282
#define MKV_Segment        0x18538067

#define MKV_Info           0x1549A966
#define MKV_TimecodeScale  0x2AD7B1
#define MKV_Duration       0x4489
#define MKV_MuxingApp      0x4D80
#define MKV_WritingApp     0x5741

#define MKV_Tracks         0x1654AE6B
#define MKV_TrackEntry     0xAE
#define MKV_TrackNumber    0xD7
#define MKV_TrackUID       0x73C5
#define MKV_TrackType      0x83
#define MKV_TrackType_Video     0x01
#define MKV_TrackType_Audio     0x02
#define MKV_TrackType_Subtitle  0x12
#define MKV_FlagEnabled    0xB9
#define MKV_FlagDefault    0x88
#define MKV_FlagForced     0x55AA
#define MKV_FlagLacing     0x9C
#define MKV_Name           0x536E
#define MKV_Language       0x22B59C
#define MKV_CodecID        0x86
#define MKV_ColourSpace    0x2EB524

#define MKV_Video          0xE0
#define MKV_PixelWidth     0xB0
#define MKV_PixelHeight    0xBA
#define MKV_DisplayWidth   0x54B0
#define MKV_DisplayHeight  0x54BA
#define MKV_DisplayUnit    0x54B2
#define MKV_DisplayUnit_Pixel               0
#define MKV_DisplayUnit_Centimeter          1
#define MKV_DisplayUnit_Inch                2
#define MKV_DisplayUnit_DisplayAspectRatio  3

#define MKV_Audio              0xE1
#define MKV_SamplingFrequency  0xB5
#define MKV_Channels           0x9F
#define MKV_BitDepth           0x6264

#define MKV_Cluster      0x1F43B675
#define MKV_Timecode     0xE7
#define MKV_SimpleBlock  0xA3