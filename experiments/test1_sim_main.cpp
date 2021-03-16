// DESCRIPTION: Verilator: Verilog example module
//
// This file ONLY is placed under the Creative Commons Public Domain, for
// any use, without warranty, 2017 by Wilson Snyder.
// SPDX-License-Identifier: CC0-1.0
//======================================================================

// Include common routines
#include <verilated.h>
#include "verilated_vcd_c.h"

// Include model header, generated from Verilating "top.v"
#include "Vtest1_manual.h"
#include <cassert>

// Current simulation time (64-bit unsigned)
vluint64_t main_time = 0;
// Called by $time in Verilog
double sc_time_stamp() {
    return main_time;  // Note does conversion to real, to match SystemC
}
Vtest1_manual* top;
VerilatedVcdC* tfp;
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
    top = new Vtest1_manual;  // Or use a const unique_ptr, or the VL_UNIQUE_PTR wrapper
    tfp = new VerilatedVcdC;
    top->trace(tfp, 99);   // Trace 99 levels of hierarchy
    tfp->open("trace.vcd");
}

void verilator_finish(){
    // Final model cleanup
    tfp->dump(main_time);
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

int test(int a, int b){
    int c = 0;
    if(a>b){
        return a;
    } else if(a==b){
        return a+b;
    }
    return c;
}

int hls_verilator_test_func(int a, int b){
    assert(!Verilated::gotFinish());
    top->argument_a1 = a;
    top->argument_a2 = b;
    top->eval();
    tfp->dump(main_time);
    int result = top->result_o8;
    main_time++;
    assert(result==test(a,b));
    return result;
}

int main(int argc, char** argv, char** env) {
    verilator_init(argc, argv);

    hls_verilator_test_func(12, 2);
    hls_verilator_test_func(2, 12);
    hls_verilator_test_func(12, 12);

    verilator_finish();
    return 0;
}