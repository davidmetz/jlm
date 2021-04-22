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

#define V_NAME Vtest2
#define TIMEOUT 300
// Include model header, generated from Verilating "top.v"
#include "Vtest2.h"
unsigned int test(unsigned int a, unsigned int b){
    //gcd
    while(a!=b){
        if(a>b)
            a -= b;
        else
            b -= a;
    }
//    do{
//        a--;
//    } while (a!=0);
    return a;
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
    tfp->dump(main_time*2);
    assert(!Verilated::gotFinish());
    top->clk = 0;
    top->eval();
    tfp->dump(main_time*2+1);
    main_time++;
}

int hls_verilator_test_func(int a, int b){
    top->i_data0 = a;
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
    assert(result==test(a,b));
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

    hls_verilator_test_func(12, 2);
    hls_verilator_test_func(2, 12);
    hls_verilator_test_func(12, 12);

    verilator_finish();
    return 0;
}