# DekTec's input and output device on CDTAPI's emulated DTA-2178, which receives SDI
# from a file and sends it to one, so that no card is needed. The output tests take
# the md5 of what the port sent; the input tests make a file of frames with the sdi
# muxer and take the framemd5 of what the port receives.

DEKTEC_VF_P = scale,format=yuv422p10le
DEKTEC_VF_I = setfield=tff,scale,format=yuv422p10le

# A source of 0.4 seconds: testsrc of size $(1) at $(2) frames per second through the
# video filters $(3), and a sine.
DEKTEC_SRC = -f lavfi -i testsrc=s=$(1):r=$(2) -f lavfi -i sine=r=48000 -t 0.4 \
             -vf $(3) -af aresample,aformat=sample_fmts=s32 -c:a pcm_s24le \
             -flags +bitexact -fflags +bitexact

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

FATE_DEKTEC_IN += fate-dektec-sdi-in-576i50
fate-dektec-sdi-in-576i50: CMD = dektec_sdi_in 625I50 10 $(call DEKTEC_SRC,720x576,25,$(DEKTEC_VF_I))

FATE_DEKTEC_IN += fate-dektec-sdi-in-720p50
fate-dektec-sdi-in-720p50: CMD = dektec_sdi_in 720P50 20 $(call DEKTEC_SRC,1280x720,50,$(DEKTEC_VF_P))

FATE_DEKTEC_IN += fate-dektec-sdi-in-1080i50
fate-dektec-sdi-in-1080i50: CMD = dektec_sdi_in 1080I50 10 $(call DEKTEC_SRC,1920x1080,25,$(DEKTEC_VF_I))

DEKTEC_SRC_DEPS = LAVFI_INDEV TESTSRC_FILTER SINE_FILTER SETFIELD_FILTER SCALE_FILTER \
                  FORMAT_FILTER ARESAMPLE_FILTER AFORMAT_FILTER \
                  WRAPPED_AVFRAME_ENCODER PCM_S24LE_ENCODER

FATE_DEKTEC-$(call ALLYES, DEKTEC_OUTDEV $(DEKTEC_SRC_DEPS)) += $(FATE_DEKTEC_OUT)
FATE_DEKTEC-$(call ALLYES, DEKTEC_INDEV SDI_MUXER FRAMEMD5_MUXER $(DEKTEC_SRC_DEPS)) += $(FATE_DEKTEC_IN)

FATE_FFMPEG += $(FATE_DEKTEC-yes)
fate-dektec: $(FATE_DEKTEC-yes)
