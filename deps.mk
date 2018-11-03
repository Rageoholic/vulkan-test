CC=clang
VERT_SHADERS = $(shell find shaders/ -name "*.vert")
FRAG_SHADERS = $(shell find shaders/ -name "*.frag")
VERT_SHADER_TARGETS = $(patsubst shaders/%.vert, shaders/%.vert.spv,	\
$(VERT_SHADERS))

FRAG_SHADER_TARGETS = $(patsubst shaders/%.frag, shaders/%.frag.spv,	\
$(FRAG_SHADERS))

LDFLAGS += -lglfw -lvulkan -lm

CFLAGS += -g
all: app $(VERT_SHADER_TARGETS) $(FRAG_SHADER_TARGETS)
app: app.o vk-basic.o rutils/math.o rutils/file.o rutils/string.o

shaders/%.vert.spv: shaders/%.vert
	glslangValidator -V $< -o $@

shaders/%.frag.spv: shaders/%.frag
	glslangValidator -V $< -o $@

sample: sample.o $(RUTILS)
