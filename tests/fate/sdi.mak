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
fate-sdi-mux-1080p23_98: REF = 7352febeb06334c824989f0fc940abd9

FATE_SDI-yes += fate-sdi-mux-1080p24
fate-sdi-mux-1080p24: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p24: CMP = oneline
fate-sdi-mux-1080p24: REF = 3b4e1829b31f4c6a5ef482107b4361f1

FATE_SDI-yes += fate-sdi-mux-1080p25
fate-sdi-mux-1080p25: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p25: CMP = oneline
fate-sdi-mux-1080p25: REF = 193edec29e1eb36344afaebda27f603b

FATE_SDI-yes += fate-sdi-mux-1080p29_97
fate-sdi-mux-1080p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p29_97: CMP = oneline
fate-sdi-mux-1080p29_97: REF = be698a0151624e27f4838dea583d3636

FATE_SDI-yes += fate-sdi-mux-1080p30
fate-sdi-mux-1080p30: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p30: CMP = oneline
fate-sdi-mux-1080p30: REF = 809f9272b76c61b5587e03f271071c97

FATE_SDI-yes += fate-sdi-mux-1080i50
fate-sdi-mux-1080i50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i50: CMP = oneline
fate-sdi-mux-1080i50: REF = 80a5d82f186fb0581d9d9f0e9ae04270

FATE_SDI-yes += fate-sdi-mux-1080i59_94
fate-sdi-mux-1080i59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i59_94: CMP = oneline
fate-sdi-mux-1080i59_94: REF = a6da35a02637e039a28553bf6744d418

FATE_SDI-yes += fate-sdi-mux-1080i60
fate-sdi-mux-1080i60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i60: CMP = oneline
fate-sdi-mux-1080i60: REF = b66fd8f3bffdb03d3162e1876d49d5b6

FATE_SDI-yes += fate-sdi-mux-1080p50
fate-sdi-mux-1080p50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p50: CMP = oneline
fate-sdi-mux-1080p50: REF = 3ed5b6c8b8f58f2f19d81be31826b4f9

FATE_SDI-yes += fate-sdi-mux-1080p59_94
fate-sdi-mux-1080p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p59_94: CMP = oneline
fate-sdi-mux-1080p59_94: REF = 27ce88e1c5cf1ba82f58a77693ccd49c

FATE_SDI-yes += fate-sdi-mux-1080p60
fate-sdi-mux-1080p60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p60: CMP = oneline
fate-sdi-mux-1080p60: REF = b86d05c9618bcdb871dd29d247b32792

FATE_SDI-yes += fate-sdi-mux-2160p23_98
fate-sdi-mux-2160p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p23_98: CMP = oneline
fate-sdi-mux-2160p23_98: REF = a849212a607ac7fc56469b4aa4d8c322

FATE_SDI-yes += fate-sdi-mux-2160p24
fate-sdi-mux-2160p24: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p24: CMP = oneline
fate-sdi-mux-2160p24: REF = 1e1810fc0819b68564e559aa8f473635

FATE_SDI-yes += fate-sdi-mux-2160p25
fate-sdi-mux-2160p25: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p25: CMP = oneline
fate-sdi-mux-2160p25: REF = 5fb8b7f8075645bc56fa698a2fe8d3cd

FATE_SDI-yes += fate-sdi-mux-2160p29_97
fate-sdi-mux-2160p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p29_97: CMP = oneline
fate-sdi-mux-2160p29_97: REF = b45e41b153e1f8aa4248107ddf15838f

FATE_SDI-yes += fate-sdi-mux-2160p30
fate-sdi-mux-2160p30: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p30: CMP = oneline
fate-sdi-mux-2160p30: REF = 2877e0308335cae7f85f13b08d500421

FATE_SDI-yes += fate-sdi-mux-2160p50
fate-sdi-mux-2160p50: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p50: CMP = oneline
fate-sdi-mux-2160p50: REF = a0fbdb02cfc41af99daee99b79265f7e

FATE_SDI-yes += fate-sdi-mux-2160p59_94
fate-sdi-mux-2160p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p59_94: CMP = oneline
fate-sdi-mux-2160p59_94: REF = 86988de1437239d7fe552f6d6074baff

FATE_SDI-yes += fate-sdi-mux-2160p60
fate-sdi-mux-2160p60: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p60: CMP = oneline
fate-sdi-mux-2160p60: REF = 42adeaa6f42d447a9b2721a4f83b5c26


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

SDI_REF_A_1080P23_98 = 99ec37f5905441f1e85d6a693b422c15
FATE_SDI_A += fate-sdi-audio-1080p23_98
fate-sdi-audio-1080p23_98: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,24000/1001,$(SDI_VF_P),stereo,,HD1080p) -f sdi md5:
fate-sdi-audio-1080p23_98: CMP = oneline
fate-sdi-audio-1080p23_98: REF = $(SDI_REF_A_1080P23_98)
FATE_SDI_A += fate-sdi-copy-1080p23_98
fate-sdi-copy-1080p23_98: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,24000/1001,$(SDI_VF_P),stereo,,HD1080p)
fate-sdi-copy-1080p23_98: CMP = oneline
fate-sdi-copy-1080p23_98: REF = $(SDI_REF_A_1080P23_98)

SDI_REF_A_1080P29_97 = d5ba3b42c7dfe406e2566cf8e49ceff9
FATE_SDI_A += fate-sdi-audio-1080p29_97
fate-sdi-audio-1080p29_97: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_P),stereo,,HD1080p) -f sdi md5:
fate-sdi-audio-1080p29_97: CMP = oneline
fate-sdi-audio-1080p29_97: REF = $(SDI_REF_A_1080P29_97)
FATE_SDI_A += fate-sdi-copy-1080p29_97
fate-sdi-copy-1080p29_97: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_P),stereo,,HD1080p)
fate-sdi-copy-1080p29_97: CMP = oneline
fate-sdi-copy-1080p29_97: REF = $(SDI_REF_A_1080P29_97)

SDI_REF_A_1080P30 = e48d3d49c18835af7d6233327d692fb7
FATE_SDI_A += fate-sdi-audio-1080p30
fate-sdi-audio-1080p30: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30,$(SDI_VF_P),stereo,,HD1080p) -f sdi md5:
fate-sdi-audio-1080p30: CMP = oneline
fate-sdi-audio-1080p30: REF = $(SDI_REF_A_1080P30)
FATE_SDI_A += fate-sdi-copy-1080p30
fate-sdi-copy-1080p30: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,30,$(SDI_VF_P),stereo,,HD1080p)
fate-sdi-copy-1080p30: CMP = oneline
fate-sdi-copy-1080p30: REF = $(SDI_REF_A_1080P30)

SDI_REF_A_1080I59_94 = c48f5b8e6734c2fc79a55d09704c4f7c
FATE_SDI_A += fate-sdi-audio-1080i59_94
fate-sdi-audio-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i) -f sdi md5:
fate-sdi-audio-1080i59_94: CMP = oneline
fate-sdi-audio-1080i59_94: REF = $(SDI_REF_A_1080I59_94)
FATE_SDI_A += fate-sdi-copy-1080i59_94
fate-sdi-copy-1080i59_94: CMD = sdi_copy $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i)
fate-sdi-copy-1080i59_94: CMP = oneline
fate-sdi-copy-1080i59_94: REF = $(SDI_REF_A_1080I59_94)

SDI_REF_A_1080P59_94 = e447773783d26aae4ac0457a13011edf
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
fate-sdi-audio-8ch-1080i50: REF = 8a5c12cd36406f681e2296048de6d8cd
FATE_SDI_A += fate-sdi-audio-16ch-1080i50
fate-sdi-audio-16ch-1080i50: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,25,$(SDI_VF_I),hexadecagonal,,HD1080i) -f sdi md5:
fate-sdi-audio-16ch-1080i50: CMP = oneline
fate-sdi-audio-16ch-1080i50: REF = efcba9a2ecf3aeabf7f52675e3da24d2

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

SDI_REF_A_2160P29_97 = 0f311611d868ba823cf16f4031ba8986
FATE_SDI_A += fate-sdi-audio-2160p29_97
fate-sdi-audio-2160p29_97: CMD = ffmpeg $(call SDI_SRC_A_4K,30000/1001,6G2160p) -f sdi md5:
fate-sdi-audio-2160p29_97: CMP = oneline
fate-sdi-audio-2160p29_97: REF = $(SDI_REF_A_2160P29_97)
FATE_SDI_A += fate-sdi-copy-2160p29_97
fate-sdi-copy-2160p29_97: CMD = sdi_copy $(call SDI_SRC_A_4K,30000/1001,6G2160p)
fate-sdi-copy-2160p29_97: CMP = oneline
fate-sdi-copy-2160p29_97: REF = $(SDI_REF_A_2160P29_97)

SDI_REF_A_2160P59_94 = 59519721144b31c49ad9cf3b22984cc9
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
fate-sdi-nocrc-1080i59_94: REF = ffaa0722af94abddcfa1d5fa2e9e4c67
FATE_SDI_A += fate-sdi-nocrc-2160p59_94
fate-sdi-nocrc-2160p59_94: CMD = ffmpeg $(call SDI_SRC_A_4K,60000/1001,12G2160p) -calc_crc 0 -f sdi md5:
fate-sdi-nocrc-2160p59_94: CMP = oneline
fate-sdi-nocrc-2160p59_94: REF = a083ba8872f37b4fbeddc8019a2115ab

# Frames without the file header, read back with the standard given.
FATE_SDI_RT += fate-sdi-noheader-1080i59_94
fate-sdi-noheader-1080i59_94: CMD = sdi_roundtrip_noheader HD1080i29_97 $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i)

# 3G level B, whose frames are frames of the interface with two pictures each: the
# file, once from the source and once copied, with the standard given, as a copy
# chooses level B only when asked; the round trip, field 1 and field 2 of
# each frame with the audio each carries; an odd number of pictures, whose last frame
# gets a black field 2; and frames without the header.
SDI_REF_B_1080P50 = 453a2236510f98640a4751b1d6837074
FATE_SDI_A += fate-sdi-audio-1080p50b
fate-sdi-audio-1080p50b: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,50,$(SDI_VF_P),stereo,,3GB1080p) -f sdi md5:
fate-sdi-audio-1080p50b: CMP = oneline
fate-sdi-audio-1080p50b: REF = $(SDI_REF_B_1080P50)

SDI_REF_B_1080P59_94 = 2c8d66a38a4cacb3c5c2e8fe9874adc7
FATE_SDI_A += fate-sdi-audio-1080p59_94b
fate-sdi-audio-1080p59_94b: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,60000/1001,$(SDI_VF_P),stereo,,3GB1080p) -f sdi md5:
fate-sdi-audio-1080p59_94b: CMP = oneline
fate-sdi-audio-1080p59_94b: REF = $(SDI_REF_B_1080P59_94)
FATE_SDI_A += fate-sdi-copy-1080p59_94b
fate-sdi-copy-1080p59_94b: CMD = sdi_copy_std 3GB1080p $(call SDI_SRC_A,1920x1080,60000/1001,$(SDI_VF_P),stereo,,3GB1080p)
fate-sdi-copy-1080p59_94b: CMP = oneline
fate-sdi-copy-1080p59_94b: REF = $(SDI_REF_B_1080P59_94)

FATE_SDI_A += fate-sdi-audio-1080p60b
fate-sdi-audio-1080p60b: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,60,$(SDI_VF_P),stereo,,3GB1080p) -f sdi md5:
fate-sdi-audio-1080p60b: CMP = oneline
fate-sdi-audio-1080p60b: REF = 0fe09da802f1f6fec2a73f9c19b74b75

FATE_SDI_RT += fate-sdi-roundtrip-1080p50b
fate-sdi-roundtrip-1080p50b: CMD = sdi_roundtrip $(call SDI_SRC_A,1920x1080,50,$(SDI_VF_P),stereo,,3GB1080p)
FATE_SDI_RT += fate-sdi-roundtrip-1080p59_94b
fate-sdi-roundtrip-1080p59_94b: CMD = sdi_roundtrip $(call SDI_SRC_A,1920x1080,60000/1001,$(SDI_VF_P),stereo,,3GB1080p)
FATE_SDI_RT += fate-sdi-roundtrip-odd-1080p59_94b
fate-sdi-roundtrip-odd-1080p59_94b: CMD = sdi_roundtrip $(call SDI_SRC_A,1920x1080,60000/1001,$(SDI_VF_P),stereo,,3GB1080p) -frames:v 23
FATE_SDI_RT += fate-sdi-noheader-1080p50b
fate-sdi-noheader-1080p50b: CMD = sdi_roundtrip_noheader 3GB1080p50 $(call SDI_SRC_A,1920x1080,50,$(SDI_VF_P),stereo,,3GB1080p)

# The muxer's own conversion: a 1280x720 yuv420p source into 1080i.
SDI_VF_I_420 = setfield=tff,scale,format=yuv420p
FATE_SDI_A += fate-sdi-scale-1080i50
fate-sdi-scale-1080i50: CMD = ffmpeg $(call SDI_SRC_A,1280x720,25,$(SDI_VF_I_420),stereo,,HD1080i) -f sdi md5:
fate-sdi-scale-1080i50: CMP = oneline
fate-sdi-scale-1080i50: REF = e6d261bea242036f25f7f617c74d8023


# The images in the formats the builder takes as they are, and as v210: the same 10-bit
# images as yuv422p10le, y210le and v210 packets give one file, the same 8-bit images as
# yuv422p and uyvy422 another. v210 out of the demuxer, copied, gives the file back.
SDI_VF_I_Y210 = $(SDI_VF_I),scale,format=y210le
SDI_VF_I_8 = setfield=tff,scale,format=yuv422p
SDI_VF_I_UYVY = $(SDI_VF_I_8),scale,format=uyvy422

FATE_SDI_PIX += fate-sdi-y210-1080i59_94
fate-sdi-y210-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I_Y210),stereo,,HD1080i) -f sdi md5:
fate-sdi-y210-1080i59_94: CMP = oneline
fate-sdi-y210-1080i59_94: REF = $(SDI_REF_A_1080I59_94)

FATE_SDI_PIX += fate-sdi-v210-1080i59_94
fate-sdi-v210-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i) -c:v v210 -f sdi md5:
fate-sdi-v210-1080i59_94: CMP = oneline
fate-sdi-v210-1080i59_94: REF = $(SDI_REF_A_1080I59_94)

FATE_SDI_PIX += fate-sdi-v210-2160p59_94
fate-sdi-v210-2160p59_94: CMD = ffmpeg $(call SDI_SRC_A_4K,60000/1001,12G2160p) -c:v v210 -f sdi md5:
fate-sdi-v210-2160p59_94: CMP = oneline
fate-sdi-v210-2160p59_94: REF = $(SDI_REF_A_2160P59_94)

FATE_SDI_PIX += fate-sdi-copy-v210-1080i59_94
fate-sdi-copy-v210-1080i59_94: CMD = sdi_copy_v210 $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I),stereo,,HD1080i)
fate-sdi-copy-v210-1080i59_94: CMP = oneline
fate-sdi-copy-v210-1080i59_94: REF = $(SDI_REF_A_1080I59_94)

SDI_REF_8_1080I59_94 = 7b2bce3e8caab6713c648d7f64c1fec4
FATE_SDI_PIX += fate-sdi-yuv422p-1080i59_94
fate-sdi-yuv422p-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I_8),stereo,,HD1080i) -f sdi md5:
fate-sdi-yuv422p-1080i59_94: CMP = oneline
fate-sdi-yuv422p-1080i59_94: REF = $(SDI_REF_8_1080I59_94)

FATE_SDI_PIX += fate-sdi-uyvy422-1080i59_94
fate-sdi-uyvy422-1080i59_94: CMD = ffmpeg $(call SDI_SRC_A,1920x1080,30000/1001,$(SDI_VF_I_UYVY),stereo,,HD1080i) -f sdi md5:
fate-sdi-uyvy422-1080i59_94: CMP = oneline
fate-sdi-uyvy422-1080i59_94: REF = $(SDI_REF_8_1080I59_94)

SDI_A_DEPS = SDI_MUXER LAVFI_INDEV TESTSRC_FILTER SINE_FILTER SETFIELD_FILTER SCALE_FILTER \
             FORMAT_FILTER ARESAMPLE_FILTER AFORMAT_FILTER SETTB_FILTER ASETTB_FILTER \
             WRAPPED_AVFRAME_ENCODER PCM_S24LE_ENCODER MD5_PROTOCOL
FATE_SDI_A-$(call ALLYES, $(SDI_A_DEPS) SDI_DEMUXER) += $(FATE_SDI_A)
FATE_SDI_A-$(call ALLYES, $(SDI_A_DEPS) SDI_DEMUXER V210_ENCODER) += $(FATE_SDI_PIX)
FATE_SDI_A-$(call ALLYES, $(SDI_A_DEPS) SDI_DEMUXER WRAPPED_AVFRAME_DECODER \
             RAWVIDEO_ENCODER PCM_S24LE_DECODER FRAMEMD5_MUXER) += $(FATE_SDI_RT)

FATE_SDI += $(FATE_SDI-yes) $(FATE_SDI_A-yes)
FATE-$(call ALLYES, SDI_MUXER LAVFI_INDEV TESTSRC_FILTER SETFIELD_FILTER SCALE_FILTER FORMAT_FILTER WRAPPED_AVFRAME_ENCODER MD5_PROTOCOL) += $(FATE_SDI)
fate-sdi: $(FATE_SDI)
