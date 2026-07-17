CC = gcc
CFLAGS = -g -Wall -Wextra
INCLUDE = -I..
LDFLAGS = -lcmocka -lresolv

TEST_BIN = test_wrapsrv
SHIM = fake_res_query.so

all: $(TEST_BIN) $(SHIM)

$(TEST_BIN): test_wrapsrv.c ../wrapsrv.c ../list.h
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ test_wrapsrv.c $(LDFLAGS)

$(SHIM): fake_res_query.c
	$(CC) $(CFLAGS) -shared -fPIC -o $@ fake_res_query.c

clean:
	rm -f $(TEST_BIN) $(SHIM)

.PHONY: all clean
