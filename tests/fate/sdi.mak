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


# The muxer with audio: 0.4 seconds of testsrc of size $(1) at $(2) frames per second
# through the video filters $(3), and a sine in the channel layout $(4) through the
# audio filters after aformat, $(5), in the standard $(6). 0.4 seconds holds the five
# frames of the audio cadence at the 1001 rates twice.
SDI_VF_P = scale,format=yuv422p10le
SDI_VF_I = setfield=tff,scale,format=yuv422p10le
SDI_SRC_A = -f lavfi -i testsrc=s=$(1):r=$(2) -f lavfi -i sine=r=48000:f=1000 -t 0.4 \
            -vf $(3) -af aresample,aformat=sample_fmts=s32:channel_layouts=$(4)$(5) \
            -c:v wrapped_avframe -c:a pcm_s24le -flags +bitexact -fflags +bitexact \
            -sdi_standard $(6)

# The same sources in other time bases, which no frame rate but 25 and 50 divides
# exactly: the muxer must build the same file.
SDI_VF_P_US = $(SDI_VF_P),settb=1/1000000
SDI_VF_I_US = $(SDI_VF_I),settb=1/1000000
SDI_VF_P_90K = $(SDI_VF_P),settb=1/90000
SDI_VF_I_90K = $(SDI_VF_I),settb=1/90000
SDI_AF_US = ,asettb=1/1000000
SDI_AF_90K = ,asettb=1/90000

# Each file once from the source and once copied with -c copy, which must give the
# same file, at every kind of rate.
SDI_REF_A_576I50 = d06f7b2022b2306b9c506ca3d8096d2e
FATE_SDI_A += fate-sdi-audio-576i50
fate-sdi-audio-576i50: CMD = ffmpeg $(call SDI_SRC_A,720x576,25,$(SDI_VF_I),stereo,,SD576i) -f sdi md5:
fate-sdi-audio-576i50: CMP = oneline
fate-sdi-audio-576i50: REF = $(SDI_REF_A_576I50)
FATE_SDI_A += fate-sdi-copy-576i50
fate-sdi-copy-576i50: CMD = sdi_copy $(call SDI_SRC_A,720x576,25,$(SDI_VF_I),stereo,,SD576i)
fate-sdi-copy-576i50: CMP = oneline
fate-sdi-copy-576i50: REF = $(SDI_REF_A_576I50)

SDI_REF_A_480I59_94 = 94980cf1e346a2e8e92968e6b270caff
FATE_SDI_A += fate-sdi-audio-480i59_94
fate-sdi-audio-480i59_94: CMD = ffmpeg $(call SDI_SRC_A,720x487,30000/1001,$(SDI_VF_I),stereo,,SD480i) -f sdi md5:
fate-sdi-audio-480i59_94: CMP = oneline
fate-sdi-audio-480i59_94: REF = $(SDI_REF_A_480I59_94)
FATE_SDI_A += fate-sdi-copy-480i59_94
fate-sdi-copy-480i59_94: CMD = sdi_copy $(call SDI_SRC_A,720x487,30000/1001,$(SDI_VF_I),stereo,,SD480i)
fate-sdi-copy-480i59_94: CMP = oneline
fate-sdi-copy-480i59_94: REF = $(SDI_REF_A_480I59_94)

SDI_REF_A_1080P23_98 = 2872dd0cf7e720b71199a0f82004157c
FATE_SDI_A += fate-sdi-audio-1080p23_98
fate-sdi-audio-1080p23_98: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,24000/1001,$(SDI_VF_P),stereo,,HD1080p) -f sdi md5:
fate-sdi-audio-1080p23_98: CMP = oneline
fate-sdi-audio-1080p23_98: REF = $(SDI_REF_A_1080P23_98)
FATE_SDI_A += fate-sdi-copy-1080p23_98
fate-sdi-copy-1080p23_98: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,24000/1001,$(SDI_VF_P),stereo,,HD1080p)
fate-sdi-copy-1080p23_98: CMP = oneline
fate-sdi-copy-1080p23_98: REF = $(SDI_REF_A_1080P23_98)

SDI_REF_A_1080P29_97 = d68b1628840740780523e2c40fadc33b
FATE_SDI_A += fate-sdi-audio-1080p29_97
fate-sdi-audio-1080p29_97: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_P),stereo,,HD1080p) -f sdi md5:
fate-sdi-audio-1080p29_97: CMP = oneline
fate-sdi-audio-1080p29_97: REF = $(SDI_REF_A_1080P29_97)
FATE_SDI_A += fate-sdi-copy-1080p29_97
fate-sdi-copy-1080p29_97: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_P),stereo,,HD1080p)
fate-sdi-copy-1080p29_97: CMP = oneline
fate-sdi-copy-1080p29_97: REF = $(SDI_REF_A_1080P29_97)

SDI_REF_A_1080P30 = aeddd5923d84a90e753fe23bd71f7eb9
FATE_SDI_A += fate-sdi-audio-1080p30
fate-sdi-audio-1080p30: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30,$(SDI_VF_P),stereo,,HD1080p) -f sdi md5:
fate-sdi-audio-1080p30: CMP = oneline
fate-sdi-audio-1080p30: REF = $(SDI_REF_A_1080P30)
FATE_SDI_A += fate-sdi-copy-1080p30
fate-sdi-copy-1080p30: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,30,$(SDI_VF_P),stereo,,HD1080p)
fate-sdi-copy-1080p30: CMP = oneline
fate-sdi-copy-1080p30: REF = $(SDI_REF_A_1080P30)

SDI_REF_A_1080I59_94 = f711c7d740a2419990fd46e30d9cbb80
FATE_SDI_A += fate-sdi-audio-1080i59_94
fate-sdi-audio-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i) -f sdi md5:
fate-sdi-audio-1080i59_94: CMP = oneline
fate-sdi-audio-1080i59_94: REF = $(SDI_REF_A_1080I59_94)
FATE_SDI_A += fate-sdi-copy-1080i59_94
fate-sdi-copy-1080i59_94: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i)
fate-sdi-copy-1080i59_94: CMP = oneline
fate-sdi-copy-1080i59_94: REF = $(SDI_REF_A_1080I59_94)

SDI_REF_A_1080P59_94 = acf33a5fa8ae518e26873d4e39876ecc
FATE_SDI_A += fate-sdi-audio-1080p59_94
fate-sdi-audio-1080p59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,60000/1001,$(SDI_VF_P),stereo,,3GA1080p) -f sdi md5:
fate-sdi-audio-1080p59_94: CMP = oneline
fate-sdi-audio-1080p59_94: REF = $(SDI_REF_A_1080P59_94)
FATE_SDI_A += fate-sdi-copy-1080p59_94
fate-sdi-copy-1080p59_94: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,60000/1001,$(SDI_VF_P),stereo,,3GA1080p)
fate-sdi-copy-1080p59_94: CMP = oneline
fate-sdi-copy-1080p59_94: REF = $(SDI_REF_A_1080P59_94)

# Sources in microseconds and in 1/90000.
FATE_SDI_A += fate-sdi-tb-us-480i59_94
fate-sdi-tb-us-480i59_94: CMD = ffmpeg $(call SDI_SRC_A,720x487,30000/1001,$(SDI_VF_I_US),stereo,$(SDI_AF_US),SD480i) -f sdi md5:
fate-sdi-tb-us-480i59_94: CMP = oneline
fate-sdi-tb-us-480i59_94: REF = $(SDI_REF_A_480I59_94)
FATE_SDI_A += fate-sdi-tb-us-1080p29_97
fate-sdi-tb-us-1080p29_97: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_P_US),stereo,$(SDI_AF_US),HD1080p) -f sdi md5:
fate-sdi-tb-us-1080p29_97: CMP = oneline
fate-sdi-tb-us-1080p29_97: REF = $(SDI_REF_A_1080P29_97)
FATE_SDI_A += fate-sdi-tb-90k-1080p29_97
fate-sdi-tb-90k-1080p29_97: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_P_90K),stereo,$(SDI_AF_90K),HD1080p) -f sdi md5:
fate-sdi-tb-90k-1080p29_97: CMP = oneline
fate-sdi-tb-90k-1080p29_97: REF = $(SDI_REF_A_1080P29_97)
FATE_SDI_A += fate-sdi-tb-us-1080i59_94
fate-sdi-tb-us-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I_US),stereo,$(SDI_AF_US),HD1080i) -f sdi md5:
fate-sdi-tb-us-1080i59_94: CMP = oneline
fate-sdi-tb-us-1080i59_94: REF = $(SDI_REF_A_1080I59_94)
FATE_SDI_A += fate-sdi-tb-90k-1080i59_94
fate-sdi-tb-90k-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I_90K),stereo,$(SDI_AF_90K),HD1080i) -f sdi md5:
fate-sdi-tb-90k-1080i59_94: CMP = oneline
fate-sdi-tb-90k-1080i59_94: REF = $(SDI_REF_A_1080I59_94)

# 8 and 16 channels, in SD, which carries 20 bits a sample, and in HD, 24.
FATE_SDI_A += fate-sdi-audio-8ch-576i50
fate-sdi-audio-8ch-576i50: CMD = ffmpeg $(call SDI_SRC_A,720x576,25,$(SDI_VF_I),7.1,,SD576i) -f sdi md5:
fate-sdi-audio-8ch-576i50: CMP = oneline
fate-sdi-audio-8ch-576i50: REF = ac49a75ad765f3ea4f9f2e36422f4c6f
FATE_SDI_A += fate-sdi-audio-16ch-576i50
fate-sdi-audio-16ch-576i50: CMD = ffmpeg $(call SDI_SRC_A,720x576,25,$(SDI_VF_I),hexadecagonal,,SD576i) -f sdi md5:
fate-sdi-audio-16ch-576i50: CMP = oneline
fate-sdi-audio-16ch-576i50: REF = e27d7ef4e30c726743a5375a228f9847
FATE_SDI_A += fate-sdi-audio-8ch-1080i50
fate-sdi-audio-8ch-1080i50: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,25,$(SDI_VF_I),7.1,,HD1080i) -f sdi md5:
fate-sdi-audio-8ch-1080i50: CMP = oneline
fate-sdi-audio-8ch-1080i50: REF = 4fe059eb1369d8631a6b0684e21d6b7f
FATE_SDI_A += fate-sdi-audio-16ch-1080i50
fate-sdi-audio-16ch-1080i50: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,25,$(SDI_VF_I),hexadecagonal,,HD1080i) -f sdi md5:
fate-sdi-audio-16ch-1080i50: CMP = oneline
fate-sdi-audio-16ch-1080i50: REF = 6ca9a4e4e98788ad72d71572db1f5b45

# A round trip through the demuxer: the images and the audio, with their timestamps.
FATE_SDI_RT += fate-sdi-roundtrip-480i59_94
fate-sdi-roundtrip-480i59_94: CMD = sdi_roundtrip $(call SDI_SRC_A,720x487,30000/1001,$(SDI_VF_I),stereo,,SD480i)
FATE_SDI_RT += fate-sdi-roundtrip-1080p29_97
fate-sdi-roundtrip-1080p29_97: CMD = sdi_roundtrip $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_P),stereo,,HD1080p)
FATE_SDI_RT += fate-sdi-roundtrip-1080i59_94
fate-sdi-roundtrip-1080i59_94: CMD = sdi_roundtrip $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i)
FATE_SDI_RT += fate-sdi-roundtrip-1080p59_94
fate-sdi-roundtrip-1080p59_94: CMD = sdi_roundtrip $(call SDI_SRC_A,1920x1080,60000/1001,$(SDI_VF_P),stereo,,3GA1080p)


# 4K, which takes 25 to 30 MB a frame, for 0.2 seconds: six frames at 29.97, more
# than the five of the audio cadence.
SDI_SRC_A_4K = -f lavfi -i testsrc=s=3840x2160:r=$(1) -f lavfi -i sine=r=48000:f=1000 -t 0.2 \
               -vf $(SDI_VF_P) -af aresample,aformat=sample_fmts=s32:channel_layouts=stereo \
               -c:v wrapped_avframe -c:a pcm_s24le -flags +bitexact -fflags +bitexact \
               -sdi_standard $(2)

SDI_REF_A_2160P29_97 = 608f8e0c21eb813edb25ea1a2077d10b
FATE_SDI_A += fate-sdi-audio-2160p29_97
fate-sdi-audio-2160p29_97: CMD = ffmpeg $(call SDI_SRC_A_4K,30000/1001,6G2160p) -f sdi md5:
fate-sdi-audio-2160p29_97: CMP = oneline
fate-sdi-audio-2160p29_97: REF = $(SDI_REF_A_2160P29_97)
FATE_SDI_A += fate-sdi-copy-2160p29_97
fate-sdi-copy-2160p29_97: CMD = sdi_copy $(call SDI_SRC_A_4K,30000/1001,6G2160p)
fate-sdi-copy-2160p29_97: CMP = oneline
fate-sdi-copy-2160p29_97: REF = $(SDI_REF_A_2160P29_97)

SDI_REF_A_2160P59_94 = 5b2d4de4869e9a999cfa14dd2da57b1c
FATE_SDI_A += fate-sdi-audio-2160p59_94
fate-sdi-audio-2160p59_94: CMD = ffmpeg $(call SDI_SRC_A_4K,60000/1001,12G2160p) -f sdi md5:
fate-sdi-audio-2160p59_94: CMP = oneline
fate-sdi-audio-2160p59_94: REF = $(SDI_REF_A_2160P59_94)
FATE_SDI_A += fate-sdi-copy-2160p59_94
fate-sdi-copy-2160p59_94: CMD = sdi_copy $(call SDI_SRC_A_4K,60000/1001,12G2160p)
fate-sdi-copy-2160p59_94: CMP = oneline
fate-sdi-copy-2160p59_94: REF = $(SDI_REF_A_2160P59_94)

FATE_SDI_RT += fate-sdi-roundtrip-2160p59_94
fate-sdi-roundtrip-2160p59_94: CMD = sdi_roundtrip $(call SDI_SRC_A_4K,60000/1001,12G2160p)

# The muxer without the line CRCs, which the dektec output device builds that way, in
# HD and in 4K.
FATE_SDI_A += fate-sdi-nocrc-1080i59_94
fate-sdi-nocrc-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i) -calc_crc 0 -f sdi md5:
fate-sdi-nocrc-1080i59_94: CMP = oneline
fate-sdi-nocrc-1080i59_94: REF = 7214d193b0f57499bc29c711a3d87720
FATE_SDI_A += fate-sdi-nocrc-2160p59_94
fate-sdi-nocrc-2160p59_94: CMD = ffmpeg $(call SDI_SRC_A_4K,60000/1001,12G2160p) -calc_crc 0 -f sdi md5:
fate-sdi-nocrc-2160p59_94: CMP = oneline
fate-sdi-nocrc-2160p59_94: REF = b58a734b365039e7e788b62079f33be0

# Frames without the file header, read back with the standard given.
FATE_SDI_RT += fate-sdi-noheader-1080i59_94
fate-sdi-noheader-1080i59_94: CMD = sdi_roundtrip_noheader HD1080i29_97 $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i)

# The muxer's own conversion: a 1280x720 yuv420p source into 1080i.
SDI_VF_I_420 = setfield=tff,scale,format=yuv420p
FATE_SDI_A += fate-sdi-scale-1080i50
fate-sdi-scale-1080i50: CMD = ffmpeg $(call SDI_SRC_A,1280x720,25,$(SDI_VF_I_420),stereo,,HD1080i) -f sdi md5:
fate-sdi-scale-1080i50: CMP = oneline
fate-sdi-scale-1080i50: REF = f6f288a69797b288060dcb589c20f18f


SDI_A_DEPS = SDI_MUXER LAVFI_INDEV TESTSRC_FILTER SINE_FILTER SETFIELD_FILTER SCALE_FILTER \
             FORMAT_FILTER ARESAMPLE_FILTER AFORMAT_FILTER SETTB_FILTER ASETTB_FILTER \
             WRAPPED_AVFRAME_ENCODER PCM_S24LE_ENCODER MD5_PROTOCOL
FATE_SDI_A-$(call ALLYES, $(SDI_A_DEPS) SDI_DEMUXER) += $(FATE_SDI_A)
FATE_SDI_A-$(call ALLYES, $(SDI_A_DEPS) SDI_DEMUXER WRAPPED_AVFRAME_DECODER \
             RAWVIDEO_ENCODER PCM_S24LE_DECODER FRAMEMD5_MUXER) += $(FATE_SDI_RT)

FATE_SDI += $(FATE_SDI-yes) $(FATE_SDI_A-yes)
FATE-$(call ALLYES, SDI_MUXER LAVFI_INDEV TESTSRC_FILTER SETFIELD_FILTER SCALE_FILTER FORMAT_FILTER WRAPPED_AVFRAME_ENCODER MD5_PROTOCOL) += $(FATE_SDI)
fate-sdi: $(FATE_SDI)
