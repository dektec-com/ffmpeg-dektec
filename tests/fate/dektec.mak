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

# A still source for the 4K input tests: SMPTE bars of size $(1) at $(2) frames per
# second, and a 750 Hz sine, which makes whole periods in a frame at 30 and 50 frames
# per second. Every frame is the same, so that the test holds when the emulated port
# drops frames, as it may while ffmpeg sets up its output: a 2160p frame fills much of
# the port's buffer.
DEKTEC_SRC_STILL_4K = -f lavfi -i smptehdbars=s=$(1):r=$(2) -f lavfi -i sine=r=48000:f=750 \
                      -t 0.1 -vf $(3) -af aresample,aformat=sample_fmts=s32 \
                      -c:a pcm_s24le -flags +bitexact -fflags +bitexact

FATE_DEKTEC_OUT += fate-dektec-sdi-out-576i50
fate-dektec-sdi-out-576i50: CMD = dektec_sdi_out 3 $(call DEKTEC_SRC,720x576,25,$(DEKTEC_VF_I))
fate-dektec-sdi-out-576i50: CMP = oneline
fate-dektec-sdi-out-576i50: REF = 2caaf938fb1df0e5849a28f2e99c5866

FATE_DEKTEC_OUT += fate-dektec-sdi-out-720p50
fate-dektec-sdi-out-720p50: CMD = dektec_sdi_out 3 $(call DEKTEC_SRC,1280x720,50,$(DEKTEC_VF_P))
fate-dektec-sdi-out-720p50: CMP = oneline
fate-dektec-sdi-out-720p50: REF = decf6e03352b729e1bacfe98532c64db

FATE_DEKTEC_OUT += fate-dektec-sdi-out-1080i50
fate-dektec-sdi-out-1080i50: CMD = dektec_sdi_out 3 $(call DEKTEC_SRC,1920x1080,25,$(DEKTEC_VF_I))
fate-dektec-sdi-out-1080i50: CMP = oneline
fate-dektec-sdi-out-1080i50: REF = 54bae403d870e2b1957a3703cc966632

# 2160p over one link, on ports 1 and 5, which are the 12G ports of the emulated card:
# 2160p30 is carried on one 6G link and 2160p50 on one 12G link (0014).
FATE_DEKTEC_OUT += fate-dektec-sdi-out-2160p30
fate-dektec-sdi-out-2160p30: CMD = dektec_sdi_out 5 $(call DEKTEC_SRC_4K,3840x2160,30,$(DEKTEC_VF_P))
fate-dektec-sdi-out-2160p30: CMP = oneline
fate-dektec-sdi-out-2160p30: REF = 08385a7ebbcac79b018c18ace4272fe5

FATE_DEKTEC_OUT += fate-dektec-sdi-out-2160p50
fate-dektec-sdi-out-2160p50: CMD = dektec_sdi_out 5 $(call DEKTEC_SRC_4K,3840x2160,50,$(DEKTEC_VF_P))
fate-dektec-sdi-out-2160p50: CMP = oneline
fate-dektec-sdi-out-2160p50: REF = 836aaebb08a65751a026187dbadd14ab

# 1080i59.94, a rate whose frame does not last a whole number of microseconds, and whose
# audio follows a cadence of five frames: sent from a source, and copied from an .sdi
# file with -c copy, which must send the same.
FATE_DEKTEC_OUT += fate-dektec-sdi-out-1080i59_94
fate-dektec-sdi-out-1080i59_94: CMD = dektec_sdi_out 3 $(call DEKTEC_SRC,1920x1080,30000/1001,$(DEKTEC_VF_I))
fate-dektec-sdi-out-1080i59_94: CMP = oneline
fate-dektec-sdi-out-1080i59_94: REF = ea4598e433da20197ce88a49a904188e

FATE_DEKTEC_COPY += fate-dektec-sdi-copy-out-1080i59_94
fate-dektec-sdi-copy-out-1080i59_94: CMD = dektec_sdi_copy_out 3 $(call DEKTEC_SRC,1920x1080,30000/1001,$(DEKTEC_VF_I))
fate-dektec-sdi-copy-out-1080i59_94: CMP = oneline
fate-dektec-sdi-copy-out-1080i59_94: REF = ea4598e433da20197ce88a49a904188e

# 2160p50 copied from an .sdi file over 12G, which must send what the source sent.
FATE_DEKTEC_COPY += fate-dektec-sdi-copy-out-2160p50
fate-dektec-sdi-copy-out-2160p50: CMD = dektec_sdi_copy_out 5 $(call DEKTEC_SRC_4K,3840x2160,50,$(DEKTEC_VF_P))
fate-dektec-sdi-copy-out-2160p50: CMP = oneline
fate-dektec-sdi-copy-out-2160p50: REF = 836aaebb08a65751a026187dbadd14ab

FATE_DEKTEC_IN += fate-dektec-sdi-in-576i50
fate-dektec-sdi-in-576i50: CMD = dektec_sdi_in 625I50 10 $(call DEKTEC_SRC,720x576,25,$(DEKTEC_VF_I))

FATE_DEKTEC_IN += fate-dektec-sdi-in-720p50
fate-dektec-sdi-in-720p50: CMD = dektec_sdi_in 720P50 20 $(call DEKTEC_SRC,1280x720,50,$(DEKTEC_VF_P))

FATE_DEKTEC_IN += fate-dektec-sdi-in-1080i50
fate-dektec-sdi-in-1080i50: CMD = dektec_sdi_in 1080I50 10 $(call DEKTEC_SRC,1920x1080,25,$(DEKTEC_VF_I))

FATE_DEKTEC_IN += fate-dektec-sdi-in-1080i59_94
fate-dektec-sdi-in-1080i59_94: CMD = dektec_sdi_in 1080I59_94 10 $(call DEKTEC_SRC,1920x1080,30000/1001,$(DEKTEC_VF_I))

FATE_DEKTEC_IN += fate-dektec-sdi-in-2160p30
fate-dektec-sdi-in-2160p30: CMD = dektec_sdi_in 2160P30 3 $(call DEKTEC_SRC_STILL_4K,3840x2160,30,$(DEKTEC_VF_P))

FATE_DEKTEC_IN += fate-dektec-sdi-in-2160p50
fate-dektec-sdi-in-2160p50: CMD = dektec_sdi_in 2160P50 5 $(call DEKTEC_SRC_STILL_4K,3840x2160,50,$(DEKTEC_VF_P))

DEKTEC_SRC_DEPS = LAVFI_INDEV TESTSRC_FILTER SMPTEHDBARS_FILTER SINE_FILTER \
                  SETFIELD_FILTER SCALE_FILTER FORMAT_FILTER ARESAMPLE_FILTER AFORMAT_FILTER \
                  WRAPPED_AVFRAME_ENCODER PCM_S24LE_ENCODER

FATE_DEKTEC-$(call ALLYES, DEKTEC_OUTDEV $(DEKTEC_SRC_DEPS)) += $(FATE_DEKTEC_OUT)
FATE_DEKTEC-$(call ALLYES, DEKTEC_INDEV SDI_MUXER FRAMEMD5_MUXER $(DEKTEC_SRC_DEPS)) += $(FATE_DEKTEC_IN)
FATE_DEKTEC-$(call ALLYES, DEKTEC_OUTDEV SDI_MUXER SDI_DEMUXER $(DEKTEC_SRC_DEPS)) += $(FATE_DEKTEC_COPY)

FATE_FFMPEG += $(FATE_DEKTEC-yes)
fate-dektec: $(FATE_DEKTEC-yes)
