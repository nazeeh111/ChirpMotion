CC = cc
CPPFLAGS = -Iinclude
CFLAGS = -O2 -std=c17 -Wall -Wextra -Werror -pedantic
LDLIBS = -lm
BUILD = build
CORE = src/common.c src/audio.c src/dsp.c src/analyze.c

.PHONY: all check demo sanitize clean
all: $(BUILD)/chirpmotion-native
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/chirpmotion-native: src/main.c $(CORE) include/chirpmotion.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c $(CORE) $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/test-native: tests/test_native.c $(CORE) include/chirpmotion.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_native.c $(CORE) $(LDFLAGS) $(LDLIBS) -o $@
check: all $(BUILD)/test-native
	mkdir -p build
	./$(BUILD)/test-native
	sh tests/cli.sh ./$(BUILD)/chirpmotion-native

demo: all $(BUILD)/test-native
	sh tests/demo.sh ./$(BUILD)/chirpmotion-native ./$(BUILD)/test-native

sanitize:
	$(MAKE) BUILD=build-sanitize CFLAGS='-O1 -g -std=c17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' check

clean:
	rm -f $(BUILD)/chirpmotion-native $(BUILD)/test-native
