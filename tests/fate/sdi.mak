FATE_SDI-yes += fate-sdi-mux-576i50
fate-sdi-mux-576i50: CMD = ffmpeg -f lavfi -i testsrc -s 720x576 -vf "setfield=tff" -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard SD576i md5:
fate-sdi-mux-576i50: CMP = oneline
fate-sdi-mux-576i50: REF = 44ad3213adb0fcb1e97e6ac298bdce75

FATE_SDI-yes += fate-sdi-mux-480i59_94
fate-sdi-mux-480i59_94: CMD = ffmpeg -f lavfi -i testsrc -s 720x480 -vf "setfield=tff" -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard SD480i md5:
fate-sdi-mux-480i59_94: CMP = oneline
fate-sdi-mux-480i59_94: REF = 6f4f1771decce3161503c49f133f8f3d

FATE_SDI-yes += fate-sdi-mux-720p23_98
fate-sdi-mux-720p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p23_98: CMP = oneline
fate-sdi-mux-720p23_98: REF = deb3b39e2b3d42f8b8e8e460b81328bb

FATE_SDI-yes += fate-sdi-mux-720p24
fate-sdi-mux-720p24: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p24: CMP = oneline
fate-sdi-mux-720p24: REF = 0635613f754401d571f9a2425cfb209b

FATE_SDI-yes += fate-sdi-mux-720p25
fate-sdi-mux-720p25: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p25: CMP = oneline
fate-sdi-mux-720p25: REF = 0248c4a1b0fffa3c07c46157feb2f272

FATE_SDI-yes += fate-sdi-mux-720p29_97
fate-sdi-mux-720p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p29_97: CMP = oneline
fate-sdi-mux-720p29_97: REF = c725e3ba989acb9d18c9e385d18039a4

FATE_SDI-yes += fate-sdi-mux-720p30
fate-sdi-mux-720p30: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p30: CMP = oneline
fate-sdi-mux-720p30: REF = 705f6453845e17d651e04f7b6b5ed77f

FATE_SDI-yes += fate-sdi-mux-720p50
fate-sdi-mux-720p50: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p50: CMP = oneline
fate-sdi-mux-720p50: REF = 343c0cc822be29ab1f0e0cec0730f44e

FATE_SDI-yes += fate-sdi-mux-720p59_94
fate-sdi-mux-720p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p59_94: CMP = oneline
fate-sdi-mux-720p59_94: REF = ac2b6afc320edd0f85e35a546371d64c

FATE_SDI-yes += fate-sdi-mux-720p60
fate-sdi-mux-720p60: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p60: CMP = oneline
fate-sdi-mux-720p60: REF = 3d728ad0f408d32a0bc2857f0c0d1a49

FATE_SDI-yes += fate-sdi-mux-1080p23_98
fate-sdi-mux-1080p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p23_98: CMP = oneline
fate-sdi-mux-1080p23_98: REF = 8cdd34e52298f94f0856c2d08d9f9a90

FATE_SDI-yes += fate-sdi-mux-1080p24
fate-sdi-mux-1080p24: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p24: CMP = oneline
fate-sdi-mux-1080p24: REF = a6afc27ee763b2b0e14cc90db9243672

FATE_SDI-yes += fate-sdi-mux-1080p25
fate-sdi-mux-1080p25: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p25: CMP = oneline
fate-sdi-mux-1080p25: REF = ed1cc1a59922d0875be4940ea4eaee1b

FATE_SDI-yes += fate-sdi-mux-1080p29_97
fate-sdi-mux-1080p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p29_97: CMP = oneline
fate-sdi-mux-1080p29_97: REF = b32969f783457621d6f5cb53bb9ae557

FATE_SDI-yes += fate-sdi-mux-1080p30
fate-sdi-mux-1080p30: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p30: CMP = oneline
fate-sdi-mux-1080p30: REF = 98a8d4bfb9a068866fb8db88010ccd8f

FATE_SDI-yes += fate-sdi-mux-1080i50
fate-sdi-mux-1080i50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i50: CMP = oneline
fate-sdi-mux-1080i50: REF = b9de70287078d0d4a390889c7ea5cfbf

FATE_SDI-yes += fate-sdi-mux-1080i59_94
fate-sdi-mux-1080i59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i59_94: CMP = oneline
fate-sdi-mux-1080i59_94: REF = 676317b0e64b67fb4aabc9975910f919

FATE_SDI-yes += fate-sdi-mux-1080i60
fate-sdi-mux-1080i60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i60: CMP = oneline
fate-sdi-mux-1080i60: REF = cf87873da0f807ae9c2d9016bdc3be24

FATE_SDI-yes += fate-sdi-mux-1080p50
fate-sdi-mux-1080p50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p50: CMP = oneline
fate-sdi-mux-1080p50: REF = 1c7b20f81587a5c20b8fe588beff38e2

FATE_SDI-yes += fate-sdi-mux-1080p59_94
fate-sdi-mux-1080p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p59_94: CMP = oneline
fate-sdi-mux-1080p59_94: REF = 92949d46c8be6d07d70bd4df9dbef9fd

FATE_SDI-yes += fate-sdi-mux-1080p60
fate-sdi-mux-1080p60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p60: CMP = oneline
fate-sdi-mux-1080p60: REF = 615f39994ceb1cce5de463308a78bfb3

FATE_SDI-yes += fate-sdi-mux-2160p23_98
fate-sdi-mux-2160p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p23_98: CMP = oneline
fate-sdi-mux-2160p23_98: REF = 078dc95f1d9a18dcb253284d449ac7c7

FATE_SDI-yes += fate-sdi-mux-2160p24
fate-sdi-mux-2160p24: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p24: CMP = oneline
fate-sdi-mux-2160p24: REF = 7ff3ed79d8ad595953dfb2998c1170b2

FATE_SDI-yes += fate-sdi-mux-2160p25
fate-sdi-mux-2160p25: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p25: CMP = oneline
fate-sdi-mux-2160p25: REF = 731a31c595b2e3fb4385d5223b1fb2d8

FATE_SDI-yes += fate-sdi-mux-2160p29_97
fate-sdi-mux-2160p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p29_97: CMP = oneline
fate-sdi-mux-2160p29_97: REF = a3f0b90f003c1033f526debca60a6cbc

FATE_SDI-yes += fate-sdi-mux-2160p30
fate-sdi-mux-2160p30: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p30: CMP = oneline
fate-sdi-mux-2160p30: REF = 42cedcf91e94ce5a96ab83e95289b344

FATE_SDI-yes += fate-sdi-mux-2160p50
fate-sdi-mux-2160p50: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p50: CMP = oneline
fate-sdi-mux-2160p50: REF = a18fc2abef7c409f0688ad4ce0d45a3f

FATE_SDI-yes += fate-sdi-mux-2160p59_94
fate-sdi-mux-2160p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p59_94: CMP = oneline
fate-sdi-mux-2160p59_94: REF = b492be8f878e2e0d711f6a7261ec88a9

FATE_SDI-yes += fate-sdi-mux-2160p60
fate-sdi-mux-2160p60: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p60: CMP = oneline
fate-sdi-mux-2160p60: REF = 3ffb4d0e13eeb3515bc47b1da183540a


FATE_SDI += $(FATE_SDI-yes)
FATE-$(call ALLYES, SDI_MUXER LAVFI_INDEV TESTSRC_FILTER SETFIELD_FILTER WRAPPED_AVFRAME_ENCODER MD5_PROTOCOL) += $(FATE_SDI)
fate-sdi: $(FATE_SDI)
