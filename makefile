-include deps.mk


WARNINGS += -Wall -Wextra -Werror -Wno-error=unused-variable			\
-Wno-error=unused-parameter -Wno-missing-field-initializers			\
-Wno-unused-function -Wno-unknown-pragmas -Wno-sign-conversion -Wno-padded	\
-Wno-reserved-id-macro -Wno-shorten-64-to-32 -Wno-double-promotion		\
-Wno-missing-braces -Wno-missing-variable-declarations -Wno-vla

CFLAGS += $(WARNINGS) --std=c99 -MD -MP -masm=intel $(OPTFLAGS)
CCFLAGS += $(WARNINGS) --std=c++17 -MD -MP -masm=intel $(OPTFLAGS)
DEPS += $(shell find . -name "*.d")


LDFLAGS += $(LIBS)


ifeq ($(mode),release)
	OPTFLAGS += -flto -O2 -g
	LDFLAGS += -flto -g
endif
ifeq ($(mode),debug)
	OPTFLAGS += -g -O0
endif
ifeq ($(mode),debugopt)
	OPTFLAGS += -O2 -g
endif





-include $(DEPS)

-include .user.mk		#User defined includes. Use this to configure specific settings on build files
RUTILS_DIR = rutils/
-include rutils/rutils.mk


%.o: %.cpp
	$(CXX) -c $< -o $@ $(CCFLAGS)


%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.vert.test: %.vert
	glslangValidator $<
	date > $@

%.frag.test: %.frag
	glslangValidator $<
	date > $@


clean:
	-$(RM)  `find . -name "*.o"` ` find . -name "*.d"` $(COMPONENTS)

.PHONY: all clean info debug debug-opt release default
