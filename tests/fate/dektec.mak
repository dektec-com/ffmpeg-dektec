# DekTec's input and output device on CDTAPI's emulated DTA-2178, which receives SDI
# from a file and sends it to one, so that no card is needed. The output tests take
# the md5 of what the port sent; the input tests make a file of frames with the sdi
# muxer and take the framemd5 of what the port receives.

DEKTEC_VF_P = scale,format=yuv422p10le
DEKTEC_VF_I = setfield=tff,scale,format=yuv422p10le

# A source of $(4) seconds: testsrc of size $(1) at $(2) frames per second through the
# video filters $(3), and a sine.
DEKTEC_SRC_T = -f lavfi -i testsrc=s=$(1):r=$(2) -f lavfi -i sine=r=48000 -t $(4) \
               -vf $(3) -af aresample,aformat=sample_fmts=s32 -c:a pcm_s24le \
               -flags +bitexact -fflags +bitexact

# The same, of 0.4 seconds. A 2160p frame is 25 MB, so the 4K cases take 0.1 seconds.
DEKTEC_SRC = $(call DEKTEC_SRC_T,$(1),$(2),$(3),0.4)
DEKTEC_SRC_4K = $(call DEKTEC_SRC_T,$(1),$(2),$(3),0.1)

FATE_DEKTEC_OUT += fate-dektec-sdi-out-576i50
fate-dektec-sdi-out-576i50: CMD = dektec_sdi_out 3 $(call DEKTEC_SRC,720x576,25,$(DEKTEC_VF_I))
fate-dektec-sdi-out-576i50: CMP = oneline
fate-dektec-sdi-out-576i50: REF = b914ee1a1bac903b4849700de36deebe

FATE_DEKTEC_OUT += fate-dektec-sdi-out-720p50
fate-dektec-sdi-out-720p50: CMD = dektec_sdi_out 3 $(call DEKTEC_SRC,1280x720,50,$(DEKTEC_VF_P))
fate-dektec-sdi-out-720p50: CMP = oneline
fate-dektec-sdi-out-720p50: REF = 2979534d743ac0a34cc14865f13cd348

FATE_DEKTEC_OUT += fate-dektec-sdi-out-1080i50
fate-dektec-sdi-out-1080i50: CMD = dektec_sdi_out 3 $(call DEKTEC_SRC,1920x1080,25,$(DEKTEC_VF_I))
fate-dektec-sdi-out-1080i50: CMP = oneline
fate-dektec-sdi-out-1080i50: REF = 759544ac5ed476d409c778c36d8a56e2

# 2160p over one link, on ports 1 and 5, which are the 12G ports of the emulated card:
# 2160p30 is carried on one 6G link and 2160p50 on one 12G link (0014).
FATE_DEKTEC_OUT += fate-dektec-sdi-out-2160p30
fate-dektec-sdi-out-2160p30: CMD = dektec_sdi_out 5 $(call DEKTEC_SRC_4K,3840x2160,30,$(DEKTEC_VF_P))
fate-dektec-sdi-out-2160p30: CMP = oneline
fate-dektec-sdi-out-2160p30: REF = 1223af9866ee740e46c6f17ed3ff4a64

FATE_DEKTEC_OUT += fate-dektec-sdi-out-2160p50
fate-dektec-sdi-out-2160p50: CMD = dektec_sdi_out 5 $(call DEKTEC_SRC_4K,3840x2160,50,$(DEKTEC_VF_P))
fate-dektec-sdi-out-2160p50: CMP = oneline
fate-dektec-sdi-out-2160p50: REF = e3f8766847fe6f14410b7048b7f2b546

FATE_DEKTEC_IN += fate-dektec-sdi-in-576i50
fate-dektec-sdi-in-576i50: CMD = dektec_sdi_in 625I50 10 $(call DEKTEC_SRC,720x576,25,$(DEKTEC_VF_I))

FATE_DEKTEC_IN += fate-dektec-sdi-in-720p50
fate-dektec-sdi-in-720p50: CMD = dektec_sdi_in 720P50 20 $(call DEKTEC_SRC,1280x720,50,$(DEKTEC_VF_P))

FATE_DEKTEC_IN += fate-dektec-sdi-in-1080i50
fate-dektec-sdi-in-1080i50: CMD = dektec_sdi_in 1080I50 10 $(call DEKTEC_SRC,1920x1080,25,$(DEKTEC_VF_I))

FATE_DEKTEC_IN += fate-dektec-sdi-in-2160p30
fate-dektec-sdi-in-2160p30: CMD = dektec_sdi_in 2160P30 3 $(call DEKTEC_SRC_4K,3840x2160,30,$(DEKTEC_VF_P))

FATE_DEKTEC_IN += fate-dektec-sdi-in-2160p50
fate-dektec-sdi-in-2160p50: CMD = dektec_sdi_in 2160P50 5 $(call DEKTEC_SRC_4K,3840x2160,50,$(DEKTEC_VF_P))

DEKTEC_SRC_DEPS = LAVFI_INDEV TESTSRC_FILTER SINE_FILTER SETFIELD_FILTER SCALE_FILTER \
                  FORMAT_FILTER ARESAMPLE_FILTER AFORMAT_FILTER \
                  WRAPPED_AVFRAME_ENCODER PCM_S24LE_ENCODER

FATE_DEKTEC-$(call ALLYES, DEKTEC_OUTDEV $(DEKTEC_SRC_DEPS)) += $(FATE_DEKTEC_OUT)
FATE_DEKTEC-$(call ALLYES, DEKTEC_INDEV SDI_MUXER FRAMEMD5_MUXER $(DEKTEC_SRC_DEPS)) += $(FATE_DEKTEC_IN)

FATE_FFMPEG += $(FATE_DEKTEC-yes)
fate-dektec: $(FATE_DEKTEC-yes)
