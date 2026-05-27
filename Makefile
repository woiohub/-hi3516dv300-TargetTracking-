# Hi3516DV300 目标追踪项目 Makefile

# SDK路径
SDK_PATH := /home/woio/hisi/Hi3516CV500_SDK_V2.0.2.0

# 交叉编译器
CROSS_COMPILE := arm-himix200-linux-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip

# 目录定义
SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
TARGET := $(BUILD_DIR)/sample_target_tracking

# 源文件列表
SRCS := $(SRC_DIR)/main.c \
        $(SRC_DIR)/video_capture.c \
        $(SRC_DIR)/nnie_inference.c \
        $(SRC_DIR)/tracker.c \
        $(SRC_DIR)/web_server.c

OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# SDK头文件路径
MPP_INC := $(SDK_PATH)/smp/a7_linux/mpp/include
COMMON_INC := $(SDK_PATH)/smp/a7_linux/mpp/sample/common
SVP_COMMON_INC := $(SDK_PATH)/smp/a7_linux/mpp/sample/svp/common

# SDK sample公共源文件
SDK_COMMON_DIR := $(SDK_PATH)/smp/a7_linux/mpp/sample/common
SDK_SVP_COMMON_DIR := $(SDK_PATH)/smp/a7_linux/mpp/sample/svp/common
SDK_COMMON_SRCS := $(SDK_COMMON_DIR)/sample_comm_sys.c \
                   $(SDK_COMMON_DIR)/sample_comm_vi.c \
                   $(SDK_COMMON_DIR)/sample_comm_vpss.c \
                   $(SDK_COMMON_DIR)/sample_comm_venc.c \
                   $(SDK_COMMON_DIR)/sample_comm_isp.c \
                   $(SDK_COMMON_DIR)/sample_comm_vo.c \
                   $(SDK_SVP_COMMON_DIR)/sample_comm_svp.c \
                   $(SDK_SVP_COMMON_DIR)/sample_comm_nnie.c \
                   $(SDK_SVP_COMMON_DIR)/sample_comm_ive.c

SDK_COMMON_OBJS := $(patsubst $(SDK_PATH)/%.c,$(BUILD_DIR)/sdk/%.o,$(SDK_COMMON_SRCS))

INC_FLAGS := -I$(MPP_INC) \
             -I$(COMMON_INC) \
             -I$(SVP_COMMON_INC) \
             -I$(INCLUDE_DIR)

# SDK库文件路径
MPP_LIB := $(SDK_PATH)/smp/a7_linux/mpp/lib

# 静态库
STATIC_LIBS := $(MPP_LIB)/libmpi.a \
               $(MPP_LIB)/libnnie.a \
               $(MPP_LIB)/libsvpruntime.a \
               $(MPP_LIB)/libive.a \
               $(MPP_LIB)/libisp.a \
               $(MPP_LIB)/lib_hiae.a \
               $(MPP_LIB)/lib_hiawb.a \
               $(MPP_LIB)/lib_hidehaze.a \
               $(MPP_LIB)/lib_hidrc.a \
               $(MPP_LIB)/lib_hildci.a \
               $(MPP_LIB)/lib_hicalcflicker.a \
               $(MPP_LIB)/lib_hiir_auto.a \
               $(MPP_LIB)/libsns_gc2053.a \
               $(MPP_LIB)/libsns_imx327.a \
               $(MPP_LIB)/libsns_imx327_2l.a \
               $(MPP_LIB)/libsns_imx307.a \
               $(MPP_LIB)/libsns_imx307_2l.a \
               $(MPP_LIB)/libsns_imx458.a \
               $(MPP_LIB)/libsns_mn34220.a \
               $(MPP_LIB)/libsns_os05a.a \
               $(MPP_LIB)/libsns_os08a10.a \
               $(MPP_LIB)/libsns_sc4210.a \
               $(MPP_LIB)/libsns_ov12870.a \
               $(MPP_LIB)/libsns_os04b10.a \
               $(MPP_LIB)/libsns_imx415.a \
               $(MPP_LIB)/libsns_imx335.a \
               $(MPP_LIB)/libhdmi.a \
               $(MPP_LIB)/libtde.a \
               $(MPP_LIB)/libVoiceEngine.a \
               $(MPP_LIB)/libupvqe.a \
               $(MPP_LIB)/libdnvqe.a \
               $(MPP_LIB)/libsecurec.a

# 编译选项
CFLAGS := -Wall -O2 -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4
CFLAGS += -DHI3516CV500
CFLAGS += -DSENSOR0_TYPE=GALAXYCORE_GC2053_MIPI_2M_30FPS_10BIT
CFLAGS += -DSENSOR1_TYPE=GALAXYCORE_GC2053_MIPI_2M_30FPS_10BIT
CFLAGS += -DISP_V2
CFLAGS += -DHI_ACODEC_TYPE_INNER
CFLAGS += $(INC_FLAGS)

.PHONY: all clean install

all: $(TARGET)
	@echo "编译完成: $(TARGET)"

$(TARGET): $(OBJS) $(SDK_COMMON_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(STATIC_LIBS) -lpthread -lm -ldl

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# SDK公共源文件
$(BUILD_DIR)/sdk/%.o: $(SDK_PATH)/%.c | $(BUILD_DIR)/sdk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/sdk:
	mkdir -p $(BUILD_DIR)/sdk

clean:
	rm -rf $(BUILD_DIR)
	@echo "清理完成"

install: $(TARGET)
	@echo "部署文件列表:"
	@echo "  - $(TARGET)"
	@echo "  - scripts/load_ko.sh"
	@echo "  - model/data/nnie_model/"
	@echo "  - web/"
