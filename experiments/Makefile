C_TESTS = $(wildcard test*.c)
$(info $$C_TESTS is [${C_TESTS}])
VERILATOR_BINS = $(patsubst %.c, obj_dir/V%, $(C_TESTS))
$(info $$VERILATOR_BINS is [${VERILATOR_BINS}])

FIRRTL_PATH = ~/git/firrtl/utils/bin/firrtl

JLM_HLS_CPP = ../jlm-hls/jlm-hls.cpp
JLM_HLS = ../bin/jlm-hls

TEST_C ?= test2.c
TEST_LL = $(patsubst %.c, %.ll, $(TEST_C))
TEST_OPT_LL = $(patsubst %.c, %.opt.ll, $(TEST_C))
TEST_FIRRTL = $(patsubst %.c, %.fir, $(TEST_C))
TEST_V = $(patsubst %.c, %.v, $(TEST_C))
TEST_VERILATOR_HARNESS = $(patsubst %.c, %_sim_main.cpp, $(TEST_C))
TEST_VERILATOR_BIN = $(patsubst %.c, obj_dir/V%, $(TEST_C))

$(JLM_HLS): $(JLM_HLS_CPP)
	make -C .. jlm-hls-debug

$(TEST_LL): $(TEST_C)
	clang -S -emit-llvm -Xclang -disable-O0-optnone $(TEST_C)

$(TEST_OPT_LL): $(TEST_LL)
	opt -mem2reg -S $(TEST_LL) -o $(TEST_OPT_LL)

$(TEST_FIRRTL): $(TEST_OPT_LL) $(JLM_HLS)
	$(JLM_HLS) --file $(TEST_OPT_LL)

$(TEST_V): $(TEST_FIRRTL)
	$(FIRRTL_PATH) -i $(TEST_FIRRTL) -o $(TEST_V) -X verilog --no-dce

$(TEST_VERILATOR_BIN): $(TEST_V) $(TEST_VERILATOR_HARNESS)
	verilator --cc --exe --build --trace-fst --assert -j $(TEST_V) $(TEST_VERILATOR_HARNESS)

run: $(TEST_VERILATOR_BIN)
	$(TEST_VERILATOR_BIN)

jlm-hls: $(JLM_HLS)