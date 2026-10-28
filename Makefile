CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS ?= -pthread

TARGET := scanner
SOURCE := port_scanner.c

.PHONY: all clean test sanitizer

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

scanner-sanitized: $(SOURCE)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o $@ $< $(LDLIBS) -fsanitize=address,undefined

test: $(TARGET)
	SCANNER=./$(TARGET) python3 -m unittest discover -s tests -v

sanitizer: scanner-sanitized
	SCANNER=./scanner-sanitized python3 -m unittest discover -s tests -v

clean:
	rm -f $(TARGET) scanner-sanitized
