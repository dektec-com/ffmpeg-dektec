FATE_SDI-yes += fate-sdi-mux-576i50
fate-sdi-mux-576i50: CMD = ffmpeg -f lavfi -i testsrc -s 720x576 -vf "setfield=tff" -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard SD576i md5:
fate-sdi-mux-576i50: CMP = oneline
fate-sdi-mux-576i50: REF = 7698f7bea209e2e8b0762335df27a57e

FATE_SDI-yes += fate-sdi-mux-480i59_94
fate-sdi-mux-480i59_94: CMD = ffmpeg -f lavfi -i testsrc -s 720x480 -vf "setfield=tff" -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard SD480i md5:
fate-sdi-mux-480i59_94: CMP = oneline
fate-sdi-mux-480i59_94: REF = 583fc33926835ce3eb4e52261338adf4

FATE_SDI-yes += fate-sdi-mux-720p23_98
fate-sdi-mux-720p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p23_98: CMP = oneline
fate-sdi-mux-720p23_98: REF = bfe22d8f77d29cc525d7e568631f2f9c

FATE_SDI-yes += fate-sdi-mux-720p24
fate-sdi-mux-720p24: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p24: CMP = oneline
fate-sdi-mux-720p24: REF = 20a87637cd1e085df19c836b1f4ac73e

FATE_SDI-yes += fate-sdi-mux-720p25
fate-sdi-mux-720p25: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p25: CMP = oneline
fate-sdi-mux-720p25: REF = dd38b0d97f81611faa1a5551f7067763

FATE_SDI-yes += fate-sdi-mux-720p29_97
fate-sdi-mux-720p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p29_97: CMP = oneline
fate-sdi-mux-720p29_97: REF = 3b9b4ccbf87a43a8bdfd30683bedd276

FATE_SDI-yes += fate-sdi-mux-720p30
fate-sdi-mux-720p30: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p30: CMP = oneline
fate-sdi-mux-720p30: REF = 2b797545b972019cc50b8b24c5d530ef

FATE_SDI-yes += fate-sdi-mux-720p50
fate-sdi-mux-720p50: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p50: CMP = oneline
fate-sdi-mux-720p50: REF = a95ec739f02875dcab6798d10e0a782c

FATE_SDI-yes += fate-sdi-mux-720p59_94
fate-sdi-mux-720p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p59_94: CMP = oneline
fate-sdi-mux-720p59_94: REF = e5d2a1618e728e7afcecc9ab37f4a217

FATE_SDI-yes += fate-sdi-mux-720p60
fate-sdi-mux-720p60: CMD = ffmpeg -f lavfi -i testsrc -s 1280x720 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD720p md5:
fate-sdi-mux-720p60: CMP = oneline
fate-sdi-mux-720p60: REF = 95bfbca84f9175a8678be3bb97ddda49

FATE_SDI-yes += fate-sdi-mux-1080p23_98
fate-sdi-mux-1080p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p23_98: CMP = oneline
fate-sdi-mux-1080p23_98: REF = 08523af173c1ed345bd475774e43d273

FATE_SDI-yes += fate-sdi-mux-1080p24
fate-sdi-mux-1080p24: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p24: CMP = oneline
fate-sdi-mux-1080p24: REF = 6219cd3338ea67d7748e21c39b7df4e8

FATE_SDI-yes += fate-sdi-mux-1080p25
fate-sdi-mux-1080p25: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p25: CMP = oneline
fate-sdi-mux-1080p25: REF = d56ddd922c34cfc1803b4fa7c05646ac

FATE_SDI-yes += fate-sdi-mux-1080p29_97
fate-sdi-mux-1080p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p29_97: CMP = oneline
fate-sdi-mux-1080p29_97: REF = 0eaca6eedc5e51de936967fe5ad9e393

FATE_SDI-yes += fate-sdi-mux-1080p30
fate-sdi-mux-1080p30: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080p md5:
fate-sdi-mux-1080p30: CMP = oneline
fate-sdi-mux-1080p30: REF = 647cf5e53298e599d8277f2f5202b4b2

FATE_SDI-yes += fate-sdi-mux-1080i50
fate-sdi-mux-1080i50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i50: CMP = oneline
fate-sdi-mux-1080i50: REF = ffa13165ff0022d8112abdadb1515d32

FATE_SDI-yes += fate-sdi-mux-1080i59_94
fate-sdi-mux-1080i59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i59_94: CMP = oneline
fate-sdi-mux-1080i59_94: REF = 4ad82cf839b61bcfd18c0e4de85de420

FATE_SDI-yes += fate-sdi-mux-1080i60
fate-sdi-mux-1080i60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -vf "setfield=tff" -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard HD1080i md5:
fate-sdi-mux-1080i60: CMP = oneline
fate-sdi-mux-1080i60: REF = 335ab9146a9bd60f1e93727066acc0f8

FATE_SDI-yes += fate-sdi-mux-1080p50
fate-sdi-mux-1080p50: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p50: CMP = oneline
fate-sdi-mux-1080p50: REF = 08e8fa57a57796fe678dca9f737ab3e9

FATE_SDI-yes += fate-sdi-mux-1080p59_94
fate-sdi-mux-1080p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p59_94: CMP = oneline
fate-sdi-mux-1080p59_94: REF = 328ae6a7116b343d0b4e0f12b074ccbe

FATE_SDI-yes += fate-sdi-mux-1080p60
fate-sdi-mux-1080p60: CMD = ffmpeg -f lavfi -i testsrc -s 1920x1080 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 3GA1080p md5:
fate-sdi-mux-1080p60: CMP = oneline
fate-sdi-mux-1080p60: REF = 3881241969cd42a96e634b853bd3c767

FATE_SDI-yes += fate-sdi-mux-2160p23_98
fate-sdi-mux-2160p23_98: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p23_98: CMP = oneline
fate-sdi-mux-2160p23_98: REF = 2c79c398762b8544d9736df60497ff4f

FATE_SDI-yes += fate-sdi-mux-2160p24
fate-sdi-mux-2160p24: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "24" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p24: CMP = oneline
fate-sdi-mux-2160p24: REF = db6f395d5ebcd3c3c2f45b03033f105b

FATE_SDI-yes += fate-sdi-mux-2160p25
fate-sdi-mux-2160p25: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "25" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p25: CMP = oneline
fate-sdi-mux-2160p25: REF = d3ea1f4ce1ed4686afc69274495c36df

FATE_SDI-yes += fate-sdi-mux-2160p29_97
fate-sdi-mux-2160p29_97: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p29_97: CMP = oneline
fate-sdi-mux-2160p29_97: REF = 031c9bd632b46fa0668f94903a14105b

FATE_SDI-yes += fate-sdi-mux-2160p30
fate-sdi-mux-2160p30: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "30" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 6G2160p md5:
fate-sdi-mux-2160p30: CMP = oneline
fate-sdi-mux-2160p30: REF = 293ae483b132575c5f2ca2e07bb2ac7b

FATE_SDI-yes += fate-sdi-mux-2160p50
fate-sdi-mux-2160p50: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "50" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p50: CMP = oneline
fate-sdi-mux-2160p50: REF = 951d98f22e392f82b5b88d4f3acb1879

FATE_SDI-yes += fate-sdi-mux-2160p59_94
fate-sdi-mux-2160p59_94: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60000/1001" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p59_94: CMP = oneline
fate-sdi-mux-2160p59_94: REF = fc27574758abb4814f663269e6760615

FATE_SDI-yes += fate-sdi-mux-2160p60
fate-sdi-mux-2160p60: CMD = ffmpeg -f lavfi -i testsrc -s 3840x2160 -r "60" -t 1 -pix_fmt yuv422p10le -flags +bitexact -fflags +bitexact -f sdi -sdi_standard 12G2160p md5:
fate-sdi-mux-2160p60: CMP = oneline
fate-sdi-mux-2160p60: REF = 9306fb8debf15d00613f9b3b91841e64


FATE_SDI += $(FATE_SDI-yes)
FATE-$(CONFIG_AVFORMAT) += $(FATE_SDI)
fate-sdi: $(FATE_SDI)
