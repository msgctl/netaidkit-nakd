BUILD = .

SRC = $(wildcard *.c)
OBJ = $(patsubst %.c, $(BUILD)/%.o, $(SRC))
DEPEND = $(patsubst %.c, $(BUILD)/%.d, $(SRC))
LDSCRIPT = $(patsubst %.ld, -T%.ld, $(wildcard *.ld))

.PRECIOUS: $(DEPEND)

INC = -Iinc
CFLAGS += -std=c99 -D_GNU_SOURCE $(INC)

LDLIBS += -lpthread -lrt -ljson-c -lubus -lubox -lblobmsg_json -luci -liwinfo

TARGETS = $(BUILD)/nakd

all: $(TARGETS)

-include $(DEPEND)

$(BUILD)/nakd: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) $(LDSCRIPT) -o $@

$(BUILD)/%.d: %.c
	$(CC) $(CFLAGS) -MM $< -o $(BUILD)/$*.d

$(BUILD)/%.o: %.c $(BUILD)/%.d
	$(CC) -c $(CFLAGS) $< -o $(BUILD)/$*.o

clean:
	rm -f $(TARGETS) $(OBJ) $(DEPEND)

.PHONY: all clean
.DEFAULT: all
