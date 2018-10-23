CC=clang

LDFLAGS += -lglfw -lvulkan
CFLAGS += -g
app: app.o rutils.a

sample: sample.o rutils.a
