// DESCRIPTION: Verilator: Verilog example module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2017 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0
//======================================================================

// Include common routines
#include <verilated.h>
#include "verilated_fst_c.h"
#include <cassert>
#include <iostream>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>

#define V_NAME Vtest6
#define TIMEOUT 80000
// Include model header, generated from Verilating "top.v"
#include "Vtest6.h"

#define A(i, j) a[(i)*size+(j)]
#define B(i, j) b[(i)*size+(j)]
#define C_own(i, j) c[(i)*size+(j)]

void test(int *a, int *b, int *c, int size) {
    for (int i = 0; i < size; i++)
        for (int j = 0; j < size; j++)
            for (int k = 0; k < size; k++)
                C_own(i, j) += A(i, k) * B(k, j);
}

void
init_dummy_data(int *a, int *b, int *c, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            A(i, j) = (int) (i + 1);
            B(i, j) = (int) (-i - 1);
            C_own(i, j) = 0;
        }
    }
}
void print_matrix(int *a, int size){
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            std::cout << std::setw(10) << A(i, j);
        }
        std::cout << std::endl;
    }

}

void clock_cycle();

#define TRACE_CHUNK_SIZE 100000
// Current simulation time (64-bit unsigned)
vluint64_t main_time = 0;

// Called by $time in Verilog
double sc_time_stamp() {
    return main_time;  // Note does conversion to real, to match SystemC
}

V_NAME *top;
VerilatedFstC *tfp;
bool terminate = false;

void term(int signum) {
    terminate = true;
}

void verilator_init(int argc, char **argv) {
    // Set debug level, 0 is off, 9 is highest presently used
    // May be overridden by commandArgs
    Verilated::debug(0);

    // Randomization reset policy
    // May be overridden by commandArgs
    Verilated::randReset(2);

    // Verilator must compute traced signals
    Verilated::traceEverOn(true);

    // Pass arguments so Verilated code can see them, e.g. $value$plusargs
    // This needs to be called before you create any model
    Verilated::commandArgs(argc, argv);

    // Construct the Verilated model, from Vtop.h generated from Verilating "top.v"
    top = new V_NAME;  // Or use a const unique_ptr, or the VL_UNIQUE_PTR wrapper
    tfp = new VerilatedFstC;
    top->trace(tfp, 99);   // Trace 99 levels of hierarchy
    tfp->open("trace.fst");

    top->i_valid = 0;
    top->i_data0 = 0;
    top->i_data1 = 0;
    top->i_data2 = 0;
    top->i_data3 = 0;
    top->i_data4 = 0;
    top->i_data5 = 0;
//    top->i_data6 = 0;
    top->reset = 1;

    top->mem_req_ready = true;
    top->mem_res_valid = false;
    top->mem_res_data = 1111111;
    clock_cycle();
    clock_cycle();
    top->reset = 0;
    clock_cycle();
}


void verilator_finish() {
    // Final model cleanup
    tfp->dump(main_time * 2);
    top->final();

    //  Coverage analysis (since test passed)
#if VM_COVERAGE
    Verilated::mkdir("logs");
    VerilatedCov::write("logs/coverage.dat");
#endif
    tfp->close();
    // Destroy model
    delete top;
    top = NULL;
}

bool mem_resp_valid;
uint64_t mem_resp_data;

void clock_cycle() {
//    if(main_time%TRACE_CHUNK_SIZE==0){
//        if(main_time){
//            tfp->close();
//        }
//        std::ostringstream os;
//        os << "trace" << main_time << ".fst";
//        tfp->open(os.str().c_str());
//        std::cout << "writing to: " << os.str();
//    }
    if (terminate) {
        std::cout << "terminating\n";
        verilator_finish();
        exit(-1);
    }
    assert(!Verilated::gotFinish());
    top->clk = 1;
    top->eval();
    top->mem_res_valid = mem_resp_valid;
    top->mem_res_data = mem_resp_data;
    if (!top->reset && top->mem_req_valid) {
        mem_resp_valid = true;
        void *addr = (void *) top->mem_req_addr;
        uint64_t data = top->mem_req_data;
        if (top->mem_req_write) {
            std::cout << "writing to " << addr << "\n";
            switch (top->mem_req_width) {
                case 0:
                    *(uint8_t *) addr = data;
                    break;
                case 1:
                    *(uint16_t *) addr = data;
                    break;
                case 2:
                    *(uint32_t *) addr = data;
                    break;
                case 3:
                    *(uint64_t *) addr = data;
                    break;
                default:
                    assert(false);
            }
        } else {
            std::cout << "reading from " << addr << "\n";
        }
        switch (top->mem_req_width) {
            case 0:
                mem_resp_data = *(uint8_t *) addr;
                break;
            case 1:
                mem_resp_data = *(uint16_t *) addr;
                break;
            case 2:
                mem_resp_data = *(uint32_t *) addr;
                break;
            case 3:
                mem_resp_data = *(uint64_t *) addr;
                break;
            default:
                assert(false);
        }
    } else {
        mem_resp_valid = false;
    }
    tfp->dump(main_time * 2);
    assert(!Verilated::gotFinish());
    top->clk = 0;
    top->eval();
    tfp->dump(main_time * 2 + 1);
    main_time++;
}

void hls_verilator_test_func(int *a, int *b, int *c, int size) {
    top->i_data0 = (uint64_t) a;
    top->i_data1 = (uint64_t) b;
    top->i_data2 = (uint64_t) c;
    top->i_data3 = size;
    int start = main_time;
    for (int i = 0; i < TIMEOUT && !top->i_ready; i++) {
        clock_cycle();
    }
    if (!top->i_ready) {
        std::cout << "i_ready not set\n";
        verilator_finish();
        exit(-1);
    }
    top->i_valid = 1;
    top->o_ready = 1;
    clock_cycle();
    top->i_valid = 0;
    top->i_data0 = 0;
    top->i_data1 = 0;
    top->i_data2 = 0;
    top->i_data3 = 0;
    for (int i = 0; i < TIMEOUT && !top->o_valid; i++) {
        clock_cycle();
    }
    if (!top->o_valid) {
        std::cout << "o_valid not set\n";
        verilator_finish();
        exit(-1);
    }
    std::cout << "finished - took " << (main_time - start) << "cycles\n";
    std::cout << "a\n";
    print_matrix(a, size);
    std::cout << "b\n";
    print_matrix(b, size);
    std::cout << "c\n";
    print_matrix(c, size);
    init_dummy_data(a, b, c, size);
    test(a, b, c, size);
    std::cout << "ref c\n";
    print_matrix(c, size);
//    int result = top->o_data0;
//    bool match = result == test(a, b);
//    if (!match) {
//        std::cout << "results do not match\n";
//        verilator_finish();
//        exit(-1);
//    }
    clock_cycle();
    clock_cycle();
}

int main(int argc, char **argv, char **env) {
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = term;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGKILL, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    verilator_init(argc, argv);

    int size = 8;
    int *a = (int*)malloc(size * size * sizeof(int));
    int *b = (int*)malloc(size * size * sizeof(int));
    int *c = (int*)malloc(size * size * sizeof(int));
    init_dummy_data(a, b, c, size);
    hls_verilator_test_func(a, b, c, size);

    verilator_finish();
    return 0;
}