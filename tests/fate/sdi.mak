FATE_SDI-yes += fate-sdi-mux-576i50
fate-sdi-mux-576i50: CMD = ffmpeg -f lavfi -i testsrc -s 720x576 -vf "setfield=tff" -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard SD576i md5:
fate-sdi-mux-576i50: CMP = oneline
fate-sdi-mux-576i50: REF = da34bfdb22d222e54b76cbff238fbaa3

FATE_SDI-yes += fate-sdi-mux-480i59_94
fate-sdi-mux-480i59_94: CMD = ffmpeg -f lavfi -i testsrc -s 720x480 -vf "setfield=tff" -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard SD480i md5:
fate-sdi-mux-480i59_94: CMP = oneline
fate-sdi-mux-480i59_94: REF = 675acb8c100566a01bed65da5287c750

FATE_SDI-yes += fate-sdi-mux-720p23_98
fate-sdi-mux-720p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p23_98: CMP = oneline
fate-sdi-mux-720p23_98: REF = 8b71a605846b85950da3b4f5f16d0817

FATE_SDI-yes += fate-sdi-mux-720p24
fate-sdi-mux-720p24: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p24: CMP = oneline
fate-sdi-mux-720p24: REF = af614797c1e21593db56fef283e354b8

FATE_SDI-yes += fate-sdi-mux-720p25
fate-sdi-mux-720p25: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p25: CMP = oneline
fate-sdi-mux-720p25: REF = 95ca8727df4465657a7927dba49ccdb8

FATE_SDI-yes += fate-sdi-mux-720p29_97
fate-sdi-mux-720p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p29_97: CMP = oneline
fate-sdi-mux-720p29_97: REF = 436f115ab725e793617fd57c8cebed3f

FATE_SDI-yes += fate-sdi-mux-720p30
fate-sdi-mux-720p30: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p30: CMP = oneline
fate-sdi-mux-720p30: REF = ca2ad56a3afb942f01924ce72feecbd9

FATE_SDI-yes += fate-sdi-mux-720p50
fate-sdi-mux-720p50: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p50: CMP = oneline
fate-sdi-mux-720p50: REF = 73eeb65b871ec95feb0a3022c6e474d2

FATE_SDI-yes += fate-sdi-mux-720p59_94
fate-sdi-mux-720p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p59_94: CMP = oneline
fate-sdi-mux-720p59_94: REF = ecfaeb681ed3e9b6ca26876aef248deb

FATE_SDI-yes += fate-sdi-mux-720p60
fate-sdi-mux-720p60: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p60: CMP = oneline
fate-sdi-mux-720p60: REF = 03d36f83cab41d3acbe1815dea704c30

FATE_SDI-yes += fate-sdi-mux-1080p23_98
fate-sdi-mux-1080p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p23_98: CMP = oneline
fate-sdi-mux-1080p23_98: REF = 75957e212c600e4e38e9a059c6b1866d

FATE_SDI-yes += fate-sdi-mux-1080p24
fate-sdi-mux-1080p24: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p24: CMP = oneline
fate-sdi-mux-1080p24: REF = fce1376952dc2df000eafaed31bf6c46

FATE_SDI-yes += fate-sdi-mux-1080p25
fate-sdi-mux-1080p25: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p25: CMP = oneline
fate-sdi-mux-1080p25: REF = 092c67ca76fea2ff6ce2f77f6a9eb272

FATE_SDI-yes += fate-sdi-mux-1080p29_97
fate-sdi-mux-1080p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p29_97: CMP = oneline
fate-sdi-mux-1080p29_97: REF = d93421dfe8b03146badb76500737a2ef

FATE_SDI-yes += fate-sdi-mux-1080p30
fate-sdi-mux-1080p30: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p30: CMP = oneline
fate-sdi-mux-1080p30: REF = 780d017d77b87d18728b06f536b26806

FATE_SDI-yes += fate-sdi-mux-1080i50
fate-sdi-mux-1080i50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i50: CMP = oneline
fate-sdi-mux-1080i50: REF = 71083e6565be1141160e0cb9a346fd61

FATE_SDI-yes += fate-sdi-mux-1080i59_94
fate-sdi-mux-1080i59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i59_94: CMP = oneline
fate-sdi-mux-1080i59_94: REF = 81c020f51a2c7a73ad78446df88d1d53

FATE_SDI-yes += fate-sdi-mux-1080i60
fate-sdi-mux-1080i60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i60: CMP = oneline
fate-sdi-mux-1080i60: REF = 658e5a9eaeb64c54a5d0b7454c1c3735

FATE_SDI-yes += fate-sdi-mux-1080p50
fate-sdi-mux-1080p50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p50: CMP = oneline
fate-sdi-mux-1080p50: REF = 24a17849aa8a511fab94e3f46925e248

FATE_SDI-yes += fate-sdi-mux-1080p59_94
fate-sdi-mux-1080p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p59_94: CMP = oneline
fate-sdi-mux-1080p59_94: REF = ca2da1d063ed4301d3196953472e781c

FATE_SDI-yes += fate-sdi-mux-1080p60
fate-sdi-mux-1080p60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p60: CMP = oneline
fate-sdi-mux-1080p60: REF = 068d12b5f67bf99cd003dfc1536a677b

FATE_SDI-yes += fate-sdi-mux-2160p23_98
fate-sdi-mux-2160p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p23_98: CMP = oneline
fate-sdi-mux-2160p23_98: REF = fe465435b428d1e0da88c367be849710

FATE_SDI-yes += fate-sdi-mux-2160p24
fate-sdi-mux-2160p24: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p24: CMP = oneline
fate-sdi-mux-2160p24: REF = 140b460c28c41e9a6064dfdd128091a3

FATE_SDI-yes += fate-sdi-mux-2160p25
fate-sdi-mux-2160p25: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p25: CMP = oneline
fate-sdi-mux-2160p25: REF = 71313e2b01c8989b64a144fcf001c914

FATE_SDI-yes += fate-sdi-mux-2160p29_97
fate-sdi-mux-2160p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p29_97: CMP = oneline
fate-sdi-mux-2160p29_97: REF = 117319ff84f0b27a82635f2ba55a8caa

FATE_SDI-yes += fate-sdi-mux-2160p30
fate-sdi-mux-2160p30: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p30: CMP = oneline
fate-sdi-mux-2160p30: REF = 8c674e73fc265690b2f5e57dbe8e6c66

FATE_SDI-yes += fate-sdi-mux-2160p50
fate-sdi-mux-2160p50: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p50: CMP = oneline
fate-sdi-mux-2160p50: REF = 52ae85c773b3a17f0d4e4775afe114a6

FATE_SDI-yes += fate-sdi-mux-2160p59_94
fate-sdi-mux-2160p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p59_94: CMP = oneline
fate-sdi-mux-2160p59_94: REF = f4c3af88eab2212d1b0523ae2e64be3c

FATE_SDI-yes += fate-sdi-mux-2160p60
fate-sdi-mux-2160p60: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p60: CMP = oneline
fate-sdi-mux-2160p60: REF = 6b99f2e92fae3c2644daac17bcdbab7b


FATE_SDI += $(FATE_SDI-yes)
FATE-$(CONFIG_AVFORMAT) += $(FATE_SDI)
fate-sdi: $(FATE_SDI)
