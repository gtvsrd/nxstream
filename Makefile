#---------------------------------------------------------------------------------
# NXStream - Makefile baseado no template padrão do libnx
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Defina DEVKITPRO no ambiente. export DEVKITPRO=<caminho>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Configuração do projeto
#---------------------------------------------------------------------------------
TARGET      := NXStream
BUILD       := build
SOURCES     := source source/core source/auth source/api source/net source/media source/ui source/input compat/src third_party/cjson
DATA        := data
# Shaders GLSL da GPU (deko3d): compilados pelo uam e embutidos no executável
SHADERS     := shaders
# deps/prefix/include vem antes das portlibs: o mbedTLS 3 do projeto precisa
# ter prioridade sobre o 2.28 que vem na imagem do devkitPro.
INCLUDES    := source deps/prefix/include compat/include third_party/cjson

APP_TITLE   := NXStream
APP_AUTHOR  := gtvsrd
APP_VERSION := 0.12.0

#---------------------------------------------------------------------------------
# Flags de compilação
#---------------------------------------------------------------------------------
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS   := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS   += $(INCLUDE) -D__SWITCH__ -DAPP_VERSION=\"$(APP_VERSION)\"
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
# --wrap=getnameinfo: a libnx ignora NI_NUMERICHOST (ver compat/src/getnameinfo.c)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) \
            -Wl,--wrap=getnameinfo

# WebRTC (deps/prefix, ver scripts/compilar-deps.sh): a ordem importa no link
# estático, cada biblioteca antes das que ela usa.
# libcurl do devkitPro usa o TLS nativo do Switch (backend libnx), não o mbedTLS.
# FFmpeg do devkitPro (com NVDEC/nvtegra): dependências pelo pkg-config das portlibs
FFMPEG_LIBS := $(shell $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config --libs --static libavcodec libavutil 2>/dev/null)
ifeq ($(strip $(FFMPEG_LIBS)),)
FFMPEG_LIBS := -lavcodec -lavutil -lm
endif
# Opus (áudio, fase 3): libopus das portlibs se existir; senão o Opus nativo do FFmpeg
OPUS_LIBS := $(shell $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config --libs --static opus 2>/dev/null)
ifneq ($(strip $(OPUS_LIBS)),)
CFLAGS   += -DTEM_LIBOPUS
CXXFLAGS += -DTEM_LIBOPUS
endif
LIBS     := -ldatachannel -lusrsctp -lsrtp2 -ljuice \
            -lmbedtls -lmbedx509 -lmbedcrypto \
            $(FFMPEG_LIBS) $(OPUS_LIBS) \
            -lcurl -lz -ldeko3d -lnx
# deps/prefix primeiro, pelo mesmo motivo dos cabeçalhos (mbedTLS 3 antes do 2.28)
LIBDIRS  := $(TOPDIR)/deps/prefix $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(SHADERS),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
GLSLFILES := $(foreach dir,$(SHADERS),$(notdir $(wildcard $(dir)/*.glsl)))

# A libdatachannel é C++: o link sempre usa g++ (traz a libstdc++)
export LD := $(CXX)

export OFILES_BIN := $(addsuffix .o,$(BINFILES)) $(GLSLFILES:.glsl=.dksh.o)
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES))) $(GLSLFILES:.glsl=_dksh.h)

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Ícone: usa icon.jpg na raiz se existir, senão o ícone padrão do libnx
ifneq (,$(wildcard $(TOPDIR)/icon.jpg))
	export APP_ICON := $(TOPDIR)/icon.jpg
else
	export APP_ICON := $(LIBNX)/default_icon.jpg
endif

export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo limpando ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

# Shaders: _vsh = vértice, _fsh = fragmento (convenção dos exemplos do deko3d)
%_vsh.dksh: %_vsh.glsl
	@echo $(notdir $<)
	@$(DEVKITPRO)/tools/bin/uam -s vert -o $@ $<

%_fsh.dksh: %_fsh.glsl
	@echo $(notdir $<)
	@$(DEVKITPRO)/tools/bin/uam -s frag -o $@ $<

%.dksh.o %_dksh.h: %.dksh
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
