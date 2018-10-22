CC=clang

LDFLAGS += -lglfw -lvulkan

app: app.o rutils.a

sample: sample.o rutils.a
