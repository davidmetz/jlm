//
// Created by david on 3/10/22.
//

#include <string>
#include <llvm/Support/SourceMgr.h>
#include <iostream>
#include "mlir/IR/Builders.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Translation.h"
#include "mlir/Pass/Pass.h"

#include "circt/Dialect/FIRRTL/FIREmitter.h"
#include "circt/Dialect/FIRRTL/FIRParser.h"
#include "circt/Dialect/FIRRTL/FIRRTLDialect.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/FIRRTL/Namespace.h"
#include "circt/Support/LLVM.h"

circt::firrtl::FModuleOp get_sr_mod(circt::firrtl::CircuitOp &circuit) {
    auto lambda_mod = circuit.lookupSymbol<circt::firrtl::FModuleOp>(circuit.name());
    for (auto &op: make_early_inc_range(lambda_mod.getBody()->getOperations())) {
        if (auto inst = llvm::dyn_cast<circt::firrtl::InstanceOp>(&op)) {
            if (inst.name() == "sr") {
                auto sr_name = inst.moduleName();
                auto sr_mod = circuit.lookupSymbol<circt::firrtl::FModuleOp>(sr_name);
                return sr_mod;
            }
        }
    }
    throw std::logic_error("Could not find subregion instantiation");
}

llvm::StringMap<llvm::StringRef> *get_instace_map(circt::firrtl::FModuleOp &module) {
    auto map = new llvm::StringMap<llvm::StringRef>();
    for (auto &op: make_early_inc_range(module.getBody()->getOperations())) {
        if (auto inst = llvm::dyn_cast<circt::firrtl::InstanceOp>(&op)) {
            map->try_emplace(inst.name(), inst.moduleName());
        }
    }
    return map;
}

circt::firrtl::CircuitOp get_circuit(const mlir::OwningModuleRef &module) {
    circt::firrtl::CircuitOp circuit;
    auto topLevelModule = &module.get().body().front();
    for (auto &op: *topLevelModule) {
        if ((circuit = llvm::dyn_cast<circt::firrtl::CircuitOp>(&op)))
            break;
    }
    if (!circuit)
        throw std::logic_error("No circuit");
    return circuit;
}

void connect_shadow(mlir::OpBuilder &builder, mlir::Location &location, mlir::Value ref_val, mlir::Value shadow_val,
                    const circt::firrtl::FIRRTLType type, bool is_input, mlir::Value &clock, mlir::Value &reset,
                    std::string name) {
    if (type.isa<circt::firrtl::BundleType>()) {
        auto bt =type.cast<circt::firrtl::BundleType>();
        for (size_t i = 0; i < bt.getNumElements(); i++) {
            auto shadow_so = builder.create<circt::firrtl::SubfieldOp>(location, shadow_val, i);
            auto so = builder.create<circt::firrtl::SubfieldOp>(location, ref_val, i);
            auto element = bt.getElement(i);
            auto element_is_input = is_input ^ element.isFlip;
            connect_shadow(builder, location, so.result(), shadow_so.result(), element.type, element_is_input, clock,
                           reset, name + "_" + element.name.str());
        }
    } else {
        if (is_input) {
            builder.create<circt::firrtl::ConnectOp>(location, shadow_val, ref_val);
        } else {
            auto message = name;
            auto eq = builder.create<circt::firrtl::EQPrimOp>(location, ref_val, shadow_val);
            auto not1 = builder.create<circt::firrtl::NotPrimOp>(location, eq);
            auto not2 = builder.create<circt::firrtl::NotPrimOp>(location, reset);
            auto cond = builder.create<circt::firrtl::AndPrimOp>(location, not1, not2);
            llvm::SmallVector<mlir::Value> operands;
            operands.push_back(ref_val);
            operands.push_back(shadow_val);
            auto formatString = name+" mismatch: %x %x\n";
            // TODO: use assert instead?
            builder.create<circt::firrtl::PrintFOp>(location, clock, cond, formatString, operands, name);
        }
    }
}

int
main(int argc, char **argv) {
    std::string firrtl_in1 = "/home/david/git/jlm-eval-hls/hls-test-suite/build/jlm_hls.normal.fir";
    std::string firrtl_in2 = "/home/david/git/jlm-eval-hls/hls-test-suite/build/jlm_hls.circt.fir";
    std::string firrtl_out = "/home/david/git/jlm-eval-hls/hls-test-suite/build/jlm_hls.check.fir";

    mlir::MLIRContext mlirContext;

    std::string annotationFilenameDetermined1;
    llvm::SourceMgr sourceMgr1;
    sourceMgr1.AddIncludeFile(firrtl_in1, llvm::SMLoc(), annotationFilenameDetermined1);
    auto module1 = circt::firrtl::importFIRFile(sourceMgr1, &mlirContext);

    std::string annotationFilenameDetermined2;
    llvm::SourceMgr sourceMgr2;
    sourceMgr2.AddIncludeFile(firrtl_in2, llvm::SMLoc(), annotationFilenameDetermined2);
    auto module2 = circt::firrtl::importFIRFile(sourceMgr2, &mlirContext);


    auto circuit2 = get_circuit(module2);
    auto sr_mod2 = get_sr_mod(circuit2);
    auto map2 = get_instace_map(sr_mod2);

    auto circuit1 = get_circuit(module1);
    auto sr_mod1 = get_sr_mod(circuit1);
    auto map1 = get_instace_map(sr_mod1);

    auto circuit_body = circuit1.getBody();
    auto sr_body = sr_mod1.getBody();
    auto clock = sr_mod1.getArgument(0);
    auto reset = sr_mod1.getArgument(1);
    for (auto &op: make_early_inc_range(sr_mod1.getBody()->getOperations())) {
        if (auto inst = llvm::dyn_cast<circt::firrtl::InstanceOp>(&op)) {
            auto module_name = inst.moduleName();
            auto inst_name = inst.name();
            llvm::StringRef shadow_module_name;
            auto f = map2->find(inst_name);
            if (f != map2->end()) {
                shadow_module_name = f->getValue();
            } else {
                throw std::logic_error("Instance not found: " + inst_name.str());
            }
            auto instace_module = circuit1.lookupSymbol<circt::firrtl::FModuleOp>(module_name);
            auto shadow_module = circuit1.lookupSymbol<circt::firrtl::FModuleOp>(shadow_module_name);
            if (shadow_module == nullptr) {
                std::cout << "adding " << shadow_module_name.str() << std::endl;
                mlir::BlockAndValueMapping bvm;
                auto clone_module = circuit2.lookupSymbol(shadow_module_name);
                // clone module into circuit1
                shadow_module = llvm::dyn_cast<circt::firrtl::FModuleOp>(
                        *mlir::OpBuilder(instace_module).clone(*clone_module, bvm));
            }
            assert(shadow_module != nullptr);

            mlir::OpBuilder builder(inst);
            builder.setInsertionPointAfter(inst);
            auto location = builder.getUnknownLoc();
            auto shadow_instace = builder.create<circt::firrtl::InstanceOp>(location, shadow_module, inst_name.str() + "_shadow");
            shadow_instace.print(llvm::outs());

            auto ports = instace_module.getPorts();
            for (unsigned i = 0, e = instace_module.getNumPorts(); i < e; ++i) {
                auto val = inst->getResult(i);
                auto port = ports[i];
                auto is_input = port.direction == circt::firrtl::Direction::In;
                auto name = inst_name.str() + "_" + port.name.str();
                auto wire_name = inst_name.str() + "__port__" + port.name.str();
                std::cout << "redirecting " << wire_name << std::endl;
                auto wire = builder.create<circt::firrtl::WireOp>(location, port.type, wire_name);
                val.replaceAllUsesWith(wire);
                // TODO: not sure if this is necessary
                if (is_input) {
                    builder.create<circt::firrtl::ConnectOp>(location, val, wire);
                } else {
                    builder.create<circt::firrtl::ConnectOp>(location, wire, val);
                }
                connect_shadow(builder, location, wire, shadow_instace.getResult(i), port.type, is_input, clock, reset,
                               name);
            }
        }
    }


    if (failed(mlir::verify(module1.get()))) {
        module1.get().emitError("module verification error");
        throw std::logic_error("Verification of firrtl failed");
    }
    std::error_code EC;
    // Print the FIRRTL IR
//    llvm::raw_fd_ostream debug(firrtl_out+".mlir", EC);
//    module1.get().print(debug);
//    debug.close();

    llvm::raw_fd_ostream output(firrtl_out, EC);
    auto status = circt::firrtl::exportFIRFile(module1.get(), output);
    if (status.failed())
        throw std::logic_error("Exporting of firrtl failed");
    output.close();
    std::cout << "\nWritten firrtl to " << firrtl_out << "\n";
    return 0;
}
