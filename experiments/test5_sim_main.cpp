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

#define V_NAME Vtest5
#define TIMEOUT 300
// Include model header, generated from Verilating "top.v"
#include "Vtest5.h"
unsigned int test(int*  a, unsigned int count){
//    *a+=b;
    long sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += a[i];
    }
    return sum;
}

void clock_cycle();

#define TRACE_CHUNK_SIZE 100000
// Current simulation time (64-bit unsigned)
vluint64_t main_time = 0;
// Called by $time in Verilog
double sc_time_stamp() {
    return main_time;  // Note does conversion to real, to match SystemC
}
V_NAME* top;
VerilatedFstC* tfp;
bool terminate = false;
void term(int signum)
{
    terminate = true;
}
void verilator_init(int argc, char** argv){
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
    top->reset = 1;

    top->mem_req_ready = true;
    top->mem_res_valid = false;
    top->mem_res_data = 1111111;
    clock_cycle();
    clock_cycle();
    top->reset = 0;
    clock_cycle();
}


void verilator_finish(){
    // Final model cleanup
    tfp->dump(main_time*2);
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
void clock_cycle(){
//    if(main_time%TRACE_CHUNK_SIZE==0){
//        if(main_time){
//            tfp->close();
//        }
//        std::ostringstream os;
//        os << "trace" << main_time << ".fst";
//        tfp->open(os.str().c_str());
//        std::cout << "writing to: " << os.str();
//    }
    if(terminate){
        std::cout << "terminating\n";
        verilator_finish();
        exit(-1);
    }
    assert(!Verilated::gotFinish());
    top->clk = 1;
    top->eval();
    top->mem_res_valid = mem_resp_valid;
    top->mem_res_data = mem_resp_data;
    if(!top->reset && top->mem_req_valid){
        mem_resp_valid = true;
        void * addr = (void*) top->mem_req_addr;
        uint64_t data = top->mem_req_data;
        if(top->mem_req_write) {
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
                mem_resp_data = *(uint8_t*)addr;
                break;
            case 1:
                mem_resp_data = *(uint16_t*)addr;
                break;
            case 2:
                mem_resp_data = *(uint32_t*)addr;
                break;
            case 3:
                mem_resp_data = *(uint64_t*)addr;
                break;
            default:
                assert(false);
        }
    } else {
        mem_resp_valid = false;
    }
    tfp->dump(main_time*2);
    assert(!Verilated::gotFinish());
    top->clk = 0;
    top->eval();
    tfp->dump(main_time*2+1);
    main_time++;
}

int hls_verilator_test_func(int* a, unsigned int b){
    top->i_data0 = (uint64_t)a;
    top->i_data1 = b;
    int start = main_time;
    for(int i=0; i<TIMEOUT && !top->i_ready; i++){
        clock_cycle();
    }
    if(!top->i_ready){
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
    for(int i=0; i<TIMEOUT && !top->o_valid; i++){
        clock_cycle();
    }
    if(!top->o_valid){
        std::cout << "o_valid not set\n";
        verilator_finish();
        exit(-1);
    }
    std::cout << "finished - took " << (main_time-start) << "cycles\n";
    int result = top->o_data0;
    bool match = result==test(a,b);
    if(!match){
        std::cout << "results do not match\n";
        verilator_finish();
        exit(-1);
    }
    clock_cycle();
    clock_cycle();
    return result;
}

int main(int argc, char** argv, char** env) {
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = term;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGKILL, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    verilator_init(argc, argv);

    int a[6] = {1,2,3,4,5, -3};
    hls_verilator_test_func(a, 6);

    verilator_finish();
    return 0;
}