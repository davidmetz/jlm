//
// Created by Magnus Sjalander 26 December 2021
//

#include "jlm/backend/hls/rhls2firrtl/mlirgen.hpp"

mlir::ModuleOp
jlm::hls::MLIRGenImpl::mlirGen(jlm::rvsdg_module &rvsdg_module) {
	auto module = mlir::ModuleOp::create(location);

	// Add the only lambda to MLIR
	auto circuit = mlirGen(get_hls_lambda(rvsdg_module));
	if (!circuit)
		return nullptr;
	module.push_back(circuit);

	module.print(llvm::outs());
	
	// Verify the module
	if (failed(mlir::verify(module))) {
		module.emitError("module verification error");
		return nullptr;
	}

	return module;
}

// Handles nodes with 2 inputs and 1 output
circt::firrtl::FModuleOp
jlm::hls::MLIRGenImpl::mlirGenSimpleNode(const jive::simple_node *node) {
	// Only handles nodes with a single output
	if (node->noutputs() != 1) {
		throw std::logic_error(node->operation().debug_string() + " has more than 1 output");
	}

	// Create the module and its input/output ports
	auto module = nodeToModule(node);
	// Get the body of the module such that we can add contents to the module
	auto body = module.getBody();

	// Get the first input bundle
	auto bundle0 = module.getArgument(2);
	// Get the data signal from the bundle
	auto data0 = getSubfield(body, bundle0, 2);

	// Get the second input bundle
	auto bundle1 = module.getArgument(3);
	// Get the ready data from the bundle
	auto data1 = getSubfield(body, bundle1, 2);

	// Get the output bundle
	auto outBundle = module.getArgument(4);
	// Get the data signal from the bundle
	auto outData = getSubfield(body, outBundle, 2);

	if (dynamic_cast<const jive::bitadd_op *>(&(node->operation()))) {
		auto op = builder.create<circt::firrtl::AddPrimOp>(
					location,
					getIntType(&node->output(0)->type(), 1),
					data0,
					data1);
		body->push_back(op);
		// Connect the op to the output data
		partialConnect(body, outData, op);
	} else if (auto o = dynamic_cast<const jive::bitconstant_op *>(&(node->operation()))) {
		auto value = o->value();
		auto size = value.nbits();

		// Create a constant of UInt<size>(value) and connect to output data
		auto constant = getConstant(body, size, value.to_uint());
		connect(body, outData, constant);
	} else {
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	}

	// Generate the output valid signal
	auto valid0 = getSubfield(body, bundle0, 1);
	auto valid1 = getSubfield(body, bundle1, 1);
	auto andValid = builder.create<circt::firrtl::AndPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				valid0,
				valid1);
	body->push_back(andValid);
	// Connect the valide signal to the output bundle
	auto outValid = getSubfield(body, outBundle, 1);
	connect(body, outValid, andValid);
	
	// Generate the ready signal
	auto outReady = getSubfield(body, outBundle, 0);
	auto andReady = builder.create<circt::firrtl::AndPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				outReady,
				andValid);
	body->push_back(andReady);
	// Connect it to the ready signal of the two input bundles
	auto ready0 = getSubfield(body, bundle0, 0);
	connect(body, ready0, andReady);
	auto ready1 = getSubfield(body, bundle1, 0);
	connect(body, ready1, andReady);

	return module;
}

circt::firrtl::FModuleOp
jlm::hls::MLIRGenImpl::mlirGenSink(const jive::simple_node *node) {
	// Create the module and its input/output ports
	auto module = nodeToModule(node);
	auto body = module.getBody();

	// Create a constant of UInt<1>(1)
	auto intType= getIntType(1);
	auto constant = builder.create<circt::firrtl::ConstantOp>(
				location,
				intType,
				llvm::APInt(1, 1));
	body->push_back(constant);

	// Get the input bundle
	auto bundle = module.getArgument(2);
	// Get the ready signal from the bundle (first signal in the bundle)
	auto ready = getSubfield(body, bundle, 0);
	// Connect the constant to the ready signal
	connect(body, ready, constant);

	return module;
}

circt::firrtl::FModuleOp
jlm::hls::MLIRGenImpl::mlirGenFork(const jive::simple_node *node) {
	// Create the module and its input/output ports
	auto module = nodeToModule(node);
	/*
	auto body = module.getBody();

	// Create a constant of UInt<1>(1)
	auto intType= getIntType(1);
	auto constant = builder.create<circt::firrtl::ConstantOp>(
				location,
				intType,
				llvm::APInt(1, 1));
	body->push_back(constant);
	*/
	return module;
}


circt::firrtl::FModuleOp
jlm::hls::MLIRGenImpl::mlirGen(const jive::simple_node *node) {
	if (dynamic_cast<const hls::sink_op *>(&(node->operation()))) {
		return mlirGenSink(node);
	} else if (dynamic_cast<const hls::fork_op *>(&(node->operation()))) {
		//			return mlirGenFork(node);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const jlm::load_op *>(&(node->operation()))) {
		//return mem_node_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const jlm::store_op *>(&(node->operation()))) {
		//return mem_node_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::predicate_buffer_op *>(&(node->operation()))) {
		//return pred_buffer_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::buffer_op *>(&(node->operation()))) {
		//return buffer_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::branch_op *>(&(node->operation()))) {
		//return branch_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::trigger_op *>(&(node->operation()))) {
		//return trigger_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::sink_op *>(&(node->operation()))) {
		//return sink_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::print_op *>(&(node->operation()))) {
		//return print_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::fork_op *>(&(node->operation()))) {
		//return fork_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (dynamic_cast<const hls::merge_op *>(&(node->operation()))) {
		// return merge_to_firrtl(n);
		throw std::logic_error(node->operation().debug_string() + " not implemented!");
	} else if (auto o = dynamic_cast<const hls::mux_op *>(&(node->operation()))) {
		if (o->discarding) {
			// return dmux_to_firrtl(n);
			throw std::logic_error(node->operation().debug_string() + " not implemented!");
		} else {
			// return ndmux_to_firrtl(n);
			throw std::logic_error(node->operation().debug_string() + " not implemented!");
		}
	}
	return mlirGenSimpleNode(node);
}

circt::firrtl::FModuleOp
jlm::hls::MLIRGenImpl::mlirGen(hls::loop_node *looNode, mlir::Block *circuitBody) {
	throw std::logic_error("Loop nodes are not implemented");
}

circt::firrtl::FModuleOp
jlm::hls::MLIRGenImpl::mlirGen(jive::region *subRegion, mlir::Block *circuitBody) {
	// Generate a vector with all inputs and outputs of the module
	llvm::SmallVector<circt::firrtl::PortInfo> ports;

	// Clock and reset ports
	addClockPort(&ports);
	addResetPort(&ports);
	// Argument ports
	for (size_t i = 0; i < subRegion->narguments(); ++i) {
		addBundlePort(
			&ports,     
			circt::firrtl::Direction::In,
			get_port_name(subRegion->argument(i)),
			getIntType(&subRegion->argument(i)->type()));
	}
	// Result ports
	for (size_t i = 0; i < subRegion->nresults(); ++i) {
		addBundlePort(
			&ports,     
			circt::firrtl::Direction::Out,
			get_port_name(subRegion->result(i)),
			getIntType(&subRegion->result(i)->type()));
	}
	// Memory ports
	addMemReqPort(&ports);
	addMemResPort(&ports);

	// Create a name for the module
	auto module_name = builder.getStringAttr("subregion_mod");
	// Now when we have all the port information we can create the module
	auto module = builder.create<circt::firrtl::FModuleOp>(location, module_name, ports);
	// Get the body of the module such that we can add contents to the module
	auto body = module.getBody();

	// Initialize the signals of mem_req
	initializeMemReq(module);

	// Get the clock and reset signal of the module
	auto clock = getClockSignal(module);
	auto reset = getResetSignal(module);
	// First we create and instantiate all the modules and keep them in a dictionary
	std::unordered_map<jive::simple_node *, circt::firrtl::InstanceOp> instances;
	for (const auto node : jive::topdown_traverser(subRegion)) {
		if (auto sn = dynamic_cast<jive::simple_node *>(node)) {
			auto nodeModule = mlirGen(sn);
			// Add the module to the body of the circuit
			circuitBody->push_back(nodeModule);
			std::string instName = get_node_name(node);
			instances[sn] = builder.create<circt::firrtl::InstanceOp>(location, nodeModule, instName);
			body->push_back(instances[sn]);
			// Connect clock and reset to the instance
			connect(body, instances[sn]->getResult(0), clock);
			connect(body, instances[sn]->getResult(1), reset);
		} else if (auto oln = dynamic_cast<hls::loop_node *>(node)) {
			module = mlirGen(oln, circuitBody);
		} else {
			throw jlm::error("Unimplemented op (unexpected structural node) : " +
					 node->operation().debug_string());
		}
	}

	// Then we wire them up
	for (const auto & instance : instances) {
		// RVSDG node
		auto rvsdg_node = instance.first;
		// Corresponding InstanceOp
		auto sinkNode = instance.second;

		// Go through each of the inputs of the RVSDG node and try to connect
		// the corresponding port on the InstanceOp
		for (size_t i = 0; i < rvsdg_node->ninputs(); i++) {
			// The port of the instance is connected to another instance

			// Get the RVSDG node that's the origin of this input
			jive::simple_input *input = rvsdg_node->input(i);
			auto origin = input->origin();
			// If the origin is a jive::simple_node then we connect the source output
			// with the sink input
			if (auto o = dynamic_cast<jive::simple_output *>(origin)) {
				// Get RVSDG node of the source
				auto source = o->node();
				// Calculate the result port of the instance:
				//   2 for clock and reset +
				//   Number of inputs of the node +
				//   The index of the output of the node
				auto sourceIndex = 2 + source->ninputs() + o->index();
				// Get the corresponding InstanceOp
				auto sourceNode = instances[source];

				auto sourcePort = sourceNode->getResult(sourceIndex);
				auto sinkPort = sinkNode->getResult(i+2);
				connect(body, sinkPort, sourcePort);
			} else if (auto o = dynamic_cast<jive::argument *>(origin)) {
				// The port of the instance is connected to an argument of the region
				// Calculate the result port of the instance:
				//   2 for clock and reset +
				//   The index of the input of the region
				auto sourceIndex = 2 + o->index();
				auto sourcePort = body->getArgument(sourceIndex);
				auto sinkPort = sinkNode->getResult(i+2);
				connect(body, sinkPort, sourcePort);
			}
			// Connect the memory port to those instances that need it
			if (dynamic_cast<const jlm::load_op *>(&(rvsdg_node->operation()))) {
				// mem_res (last argument of the region and result of the instance)
				auto sourceIndex = body->getArguments().size();
				auto sinkIndex = 2 + rvsdg_node->ninputs() + rvsdg_node->noutputs() - 1;
				auto resSourcePort = body->getArgument(sourceIndex);
				auto resSinkPort = sinkNode->getResult(sinkIndex);
				connect(body, resSinkPort, resSourcePort);
				// mem_req (second to last argument of the region and result of the instance)
				// The direction is inverted compared to mem_res
				auto reqSinkPort = body->getArgument(sourceIndex-1);
				auto reqSourcePort = sinkNode->getResult(sinkIndex-1);
				connect(body, reqSinkPort, reqSourcePort);
			} else if (dynamic_cast<const jlm::store_op *>(&(rvsdg_node->operation()))) {
				// mem_res (last argument of the region and result of the instance)
				auto sourceIndex = body->getArguments().size();
				auto sinkIndex = 2 + rvsdg_node->ninputs() + rvsdg_node->noutputs() - 1;
				auto resSourcePort = body->getArgument(sourceIndex);
				auto resSinkPort = sinkNode->getResult(sinkIndex);
				connect(body, resSinkPort, resSourcePort);
				// mem_req (second to last argument of the region and result of the instance)
				// The direction is inverted compared to mem_res
				auto reqSinkPort = body->getArgument(sourceIndex-1);
				auto reqSourcePort = sinkNode->getResult(sinkIndex-1);
				connect(body, reqSinkPort, reqSourcePort);
			}
		}
	}

	// Connect the results of the region
	for (size_t i = 0; i < subRegion->nresults(); i++) {
		auto result = subRegion->result(i);
		auto origin = result->origin();		
		if (auto o = dynamic_cast<jive::simple_output *>(origin)) {
			// The RVSDG node producing the value to be connected to the output
			auto source = o->node();
			// Calculate the result port of the instance:
			//   2 for clock and reset +
			//   Number of inputs of the node +
			//   The index of the output of the node
			auto sourceIndex = 2 + source->ninputs() + o->index();
			// Get the corresponding InstanceOp
			auto sourceNode = instances[source];
			auto sourcePort = sourceNode->getResult(sourceIndex);

			// Calculate the result port of the module:
			//   2 for clock and reset +
			//   Number of inputs of the region +
			//   The index of the result of the region (== i)
			auto sinkIndex = 2 + subRegion->narguments() + i;
			auto sinkPort = body->getArgument(sinkIndex);

			// Connect the InstanceOp output to the result of the region
			connect(body, sinkPort, sourcePort);
		}
	}

        return module;
}

// Emit a circuit
circt::firrtl::CircuitOp
jlm::hls::MLIRGenImpl::mlirGen(const jlm::lambda::node *lambda_node) {
	// The same name is used for the circuit and main module
	auto module_name = builder.getStringAttr(lambda_node->name() + "_lambda_mod");
	// Create the top level FIRRTL circuit
	auto circuit = builder.create<circt::firrtl::CircuitOp>(location, module_name);
	// The body will be populated with a list of modules
	auto circuitBody = circuit.getBody();

	// Get the region of the function
	auto subRegion = lambda_node->subregion();

	// 
	//   Add ports
	//
	// Generate a vector with all inputs and outputs of the module
	llvm::SmallVector<circt::firrtl::PortInfo> ports;

	// Clock and reset ports
	addClockPort(&ports);
	addResetPort(&ports);
	
	// Input bundle
	using BundleElement = circt::firrtl::BundleType::BundleElement;
	llvm::SmallVector<BundleElement> input_elements;
	input_elements.push_back(getReadyElement());
	input_elements.push_back(getValidElement());
	for (size_t i = 0; i < subRegion->narguments(); ++i) {
		std::string port_name("data");
		port_name.append(std::to_string(i));
		input_elements.push_back(BundleElement(
						builder.getStringAttr(port_name),
						false,
						getIntType(&subRegion->argument(i)->type()))
					 );
	}
	auto input_type = circt::firrtl::BundleType::get(input_elements, builder.getContext());
	struct circt::firrtl::PortInfo i_bundle = {
		builder.getStringAttr("i"),
		input_type,
		circt::firrtl::Direction::In,
		{builder.getStringAttr("")},
		location,
	};
	ports.push_back(i_bundle);

	// Output bundle
	llvm::SmallVector<BundleElement> output_elements;
	output_elements.push_back(getReadyElement());
	output_elements.push_back(getValidElement());
	for (size_t i = 0; i < subRegion->nresults(); ++i) {
		std::string port_name("data");
		port_name.append(std::to_string(i));
		output_elements.push_back(BundleElement(
						builder.getStringAttr(port_name),
						false,
						getIntType(&subRegion->argument(i)->type()))
					 );

	}
	auto output_type = circt::firrtl::BundleType::get(output_elements, builder.getContext());
	struct circt::firrtl::PortInfo o_bundle = {
		builder.getStringAttr("o"),
		output_type,
		circt::firrtl::Direction::Out,
		{builder.getStringAttr("")},
		location,
	};
	ports.push_back(o_bundle);

	// Memory ports
	addMemReqPort(&ports);
	addMemResPort(&ports);

	// Now when we have all the port information we can create the module
	// The same name is used for the circuit and main module
	auto module = builder.create<circt::firrtl::FModuleOp>(location, module_name, ports);
	// Get the body of the module such that we can add contents to the module
	auto body = module.getBody();

	// Initialize the signals of mem_req
	initializeMemReq(module);
	
	// Create a module of the region
	auto sr_module = mlirGen(subRegion, circuitBody);
	circuitBody->push_back(sr_module);
	// Instantiate the region
	auto instance = builder.create<circt::firrtl::InstanceOp>(location, sr_module, "sr");
	body->push_back(instance);
	// Connect the Clock
	auto clock = getClockSignal(module);
	connect(body, instance->getResult(0), clock);
	// Connect the Reset
	auto reset = getResetSignal(module);
	connect(body, instance->getResult(1), reset);

	//
	// Add registers to the module
	//
	// Reset when low (0 == false) 1-bit
	auto zeroBitValue = getConstant(body, 1,0);

	// Input registers
	llvm::SmallVector<circt::firrtl::RegResetOp> inputValidRegs;
	llvm::SmallVector<circt::firrtl::RegResetOp> inputDataRegs;
	for (size_t i = 0; i < subRegion->narguments(); ++i) {
		std::string valid_name("i");
		valid_name.append(std::to_string(i));
		valid_name.append("_valid_reg");
		auto validReg = builder.create<circt::firrtl::RegResetOp>(
				location,
				getIntType(1),
				clock,
				reset,
				zeroBitValue,
				builder.getStringAttr(valid_name));
		body->push_back(validReg);
		inputValidRegs.push_back(validReg);
		
		std::string data_name("i");
		data_name.append(std::to_string(i));
		data_name.append("_data_reg");
		auto resetValue = getConstant(body, jlmSize(&subRegion->argument(i)->type()), 0);
		auto dataReg = builder.create<circt::firrtl::RegResetOp>(
				location,
				getIntType(&subRegion->argument(i)->type()),
				clock,
				reset,
				resetValue,
				builder.getStringAttr(data_name));
		body->push_back(dataReg);
		inputDataRegs.push_back(dataReg);

		auto port = instance.getResult(i+2);
		auto portValid = getSubfield(body, port, 1);
		connect(body, portValid, validReg);
		auto portData = getSubfield(body, port, 2);
		connect(body, portData, dataReg);

		// When statement
		auto portReady = builder.create<circt::firrtl::SubfieldOp>(location, port, 1);
		body->push_back(portReady);
		auto whenCondition = builder.create<circt::firrtl::AndPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				portReady,
				portValid);
		body->push_back(whenCondition);
		circt::firrtl::WhenOp whenOp = builder.create<circt::firrtl::WhenOp>(
				location,
				whenCondition,
				false);  // No else region
		body->push_back(whenOp);
		auto thenBuilder = whenOp.getThenBodyBuilder();
		thenBuilder.create<circt::firrtl::ConnectOp>(
				location,
				validReg,
				zeroBitValue);
	}

	// Output registers

	// Need to know the number of inputs so we can calculate the
	// correct index for outputs
	auto numInputs = subRegion->narguments();
	llvm::SmallVector<circt::firrtl::RegResetOp> outputValidRegs;
	llvm::SmallVector<circt::firrtl::RegResetOp> outputDataRegs;

	auto oneBitValue = getConstant(body, 1, 1);
	for (size_t i = 0; i < subRegion->nresults(); ++i) {
		std::string validName("o");
		validName.append(std::to_string(i));
		validName.append("_valid_reg");
		auto validReg = builder.create<circt::firrtl::RegResetOp>(
				location,
				getIntType(1),
				clock,
				reset,
				zeroBitValue,
				builder.getStringAttr(validName));
		body->push_back(validReg);
		outputValidRegs.push_back(validReg);
		
		std::string dataName("o");
		dataName.append(std::to_string(i));
		dataName.append("_data_reg");
		auto resetValue = getConstant(body, jlmSize(&subRegion->argument(i)->type()), 0);
		auto dataReg = builder.create<circt::firrtl::RegResetOp>(
				location,
				getIntType(&subRegion->argument(i)->type()),
				clock,
				reset,
				resetValue,
				builder.getStringAttr(dataName));
		body->push_back(dataReg);
		outputDataRegs.push_back(dataReg);

		// Get the bundle
		auto port = instance.getResult(2 + numInputs + i);

		auto portReady = getSubfield(body, port, 0);
		auto notValidReg = builder.create<circt::firrtl::NotPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				validReg);
		body->push_back(notValidReg);
		connect(body, portReady, notValidReg);

		// When statement
		auto portValid = getSubfield(body, port, 1);
		auto whenCondition = builder.create<circt::firrtl::AndPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				portReady,
				portValid);
		body->push_back(whenCondition);
		circt::firrtl::WhenOp whenOp = builder.create<circt::firrtl::WhenOp>(
				location,
				whenCondition,
				false);  // No else region
		body->push_back(whenOp);

		auto thenBuilder = whenOp.getThenBodyBuilder();
		thenBuilder.create<circt::firrtl::ConnectOp>(
				location,
				validReg,
				oneBitValue);
		auto portData = getSubfield(body, port, 2);
		thenBuilder.create<circt::firrtl::ConnectOp>(
				location,
				dataReg,
				portData);
	}

	// Create the ready signal for the input bundle
	mlir::Value prevAnd = oneBitValue;
	for (size_t i=0; i<inputValidRegs.size(); i++) {
		auto notReg = builder.create<circt::firrtl::NotPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				inputValidRegs[i]);
		body->push_back(notReg);
		auto andOp = builder.create<circt::firrtl::AndPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				notReg,
				prevAnd);
		body->push_back(andOp);
		prevAnd = andOp;
		
	}
	auto inBundle = body->getArgument(2);
	auto inReady = getSubfield(body, inBundle, 0);
	connect(body, inReady, prevAnd);

	// Create the valid signal for the output bundle
	prevAnd = oneBitValue;
	for (size_t i=0; i<outputValidRegs.size(); i++) {
		auto andOp = builder.create<circt::firrtl::AndPrimOp>(
				location,
				circt::firrtl::IntType::get(builder.getContext(), false, 1),
				outputValidRegs[i],
				prevAnd);
		body->push_back(andOp);
		prevAnd = andOp;
		
	}
	auto outBundle = body->getArgument(3);
	auto outValid = getSubfield(body, outBundle, 1);
	connect(body, outValid, prevAnd);

	// Connect output data signals
	for (size_t i=0; i<outputDataRegs.size(); i++) {
		auto outData = getSubfield(body, outBundle, 2+i);
		connect(body, outData, outputDataRegs[i]);
	}
	
	// Input when statement
	auto inValid = getSubfield(body, inBundle, 1);
	auto whenCondition = builder.create<circt::firrtl::AndPrimOp>(
			location,
			circt::firrtl::IntType::get(builder.getContext(), false, 1),
			inReady,
			inValid);
	body->push_back(whenCondition);
	circt::firrtl::WhenOp whenOp = builder.create<circt::firrtl::WhenOp>(
			location,
			whenCondition,
			false);  // No else region
	body->push_back(whenOp);
	auto thenBuilder = whenOp.getThenBodyBuilder();
	for (size_t i=0; i<inputValidRegs.size(); i++) {
		auto thenValidValue = thenBuilder.create<circt::firrtl::SpecialConstantOp>(
				location,
				circt::firrtl::ResetType::get(builder.getContext()),
				builder.getBoolAttr(true));
		thenBuilder.create<circt::firrtl::ConnectOp>(
				location,
				inputValidRegs[i],
				thenValidValue);
		auto inData = thenBuilder.create<circt::firrtl::SubfieldOp>(location, inBundle, 2+i);
		thenBuilder.create<circt::firrtl::ConnectOp>(
				location,
				inputDataRegs[i],
				inData);
	}

	// Output when statement
	auto outReady = getSubfield(body, outBundle, 0);
	whenCondition = builder.create<circt::firrtl::AndPrimOp>(
			location,
			circt::firrtl::IntType::get(builder.getContext(), false, 1),
			outReady,
			outValid);
	body->push_back(whenCondition);
	whenOp = builder.create<circt::firrtl::WhenOp>(
			location,
			whenCondition,
			false);  // No else region
	body->push_back(whenOp);
	thenBuilder = whenOp.getThenBodyBuilder();
	auto thenResetValue = thenBuilder.create<circt::firrtl::SpecialConstantOp>(
				location,
				circt::firrtl::ResetType::get(builder.getContext()),
				builder.getBoolAttr(false));
	for (size_t i=0; i<outputValidRegs.size(); i++) {
		thenBuilder.create<circt::firrtl::ConnectOp>(
				location,
				outputValidRegs[i],
				thenResetValue);
	}

	// Connect the memory ports
	auto args = body->getArguments().size();
	auto memResBundle = body->getArgument(args-1);
	auto memResValid = getSubfield(body, memResBundle, 0);
	auto memResData  = getSubfield(body, memResBundle, 1);

	auto memReqBundle = body->getArgument(args-2);
	auto memReqValid = getSubfield(body, memReqBundle, 1);
	auto memReqAddr  = getSubfield(body, memReqBundle, 2);
	auto memReqData  = getSubfield(body, memReqBundle, 3);
	auto memReqWrite = getSubfield(body, memReqBundle, 4);
	auto memReqWidth = getSubfield(body, memReqBundle, 5);
	
	auto srArgs = instance.getResults().size();
	auto srMemResBundle = instance->getResult(srArgs-1);
	auto srMemResValid = getSubfield(body, srMemResBundle, 0);
	auto srMemResData  = getSubfield(body, srMemResBundle, 1);

	auto srMemReqBundle = instance->getResult(srArgs-2);
	auto srMemReqReady = getSubfield(body, srMemReqBundle, 0);
	auto srMemReqValid = getSubfield(body, srMemReqBundle, 1);
	auto srMemReqAddr  = getSubfield(body, srMemReqBundle, 2);
	auto srMemReqData  = getSubfield(body, srMemReqBundle, 3);
	auto srMemReqWrite = getSubfield(body,srMemReqBundle, 4);
	auto srMemReqWidth = getSubfield(body, srMemReqBundle, 5);

	connect(body, srMemResValid, memResValid);
	connect(body, srMemResData,  memResData);
	connect(body, srMemReqReady, zeroBitValue);

	// When statement
	whenOp = builder.create<circt::firrtl::WhenOp>(
			location,
			srMemReqValid,
			false);  // No else region
	body->push_back(whenOp);
	thenBuilder = whenOp.getThenBodyBuilder();
	auto thenValidValue = thenBuilder.create<circt::firrtl::SpecialConstantOp>(
				location,
				circt::firrtl::ResetType::get(builder.getContext()),
				builder.getBoolAttr(true));
	thenBuilder.create<circt::firrtl::ConnectOp>(
			location,
			srMemReqReady,
			thenValidValue);
	thenBuilder.create<circt::firrtl::ConnectOp>(
			location,
			memReqValid,
			thenValidValue);
	thenBuilder.create<circt::firrtl::ConnectOp>(
			location,
			memReqAddr,
			srMemReqAddr);
	thenBuilder.create<circt::firrtl::ConnectOp>(
			location,
			memReqData,
			srMemReqData);
	thenBuilder.create<circt::firrtl::ConnectOp>(
			location,
			memReqWrite,
			srMemReqWrite);
	thenBuilder.create<circt::firrtl::ConnectOp>(
			location,
			memReqWidth,
			srMemReqWidth);

	// Add the module to the body of the circuit
	circuitBody->push_back(module);
	
	return circuit;
}

/*
  Helper functions
*/

// Returns a PortInfo of ClockType
void
jlm::hls::MLIRGenImpl::addClockPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports) {
	struct circt::firrtl::PortInfo port = {
		builder.getStringAttr("clk"),
		circt::firrtl::ClockType::get(builder.getContext()),
		circt::firrtl::Direction::In,
		{builder.getStringAttr("")},
		location,
	};
	ports->push_back(port);
}

// Returns a PortInfo of unsigned IntType with width of 1
void
jlm::hls::MLIRGenImpl::addResetPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports) {
	struct circt::firrtl::PortInfo port = {
		builder.getStringAttr("reset"),
		circt::firrtl::IntType::get(builder.getContext(), false, 1),
		circt::firrtl::Direction::In,
		{builder.getStringAttr("")},
		location,
	};
	ports->push_back(port);
}

void
jlm::hls::MLIRGenImpl::addMemReqPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports) {
	using BundleElement = circt::firrtl::BundleType::BundleElement;

	llvm::SmallVector<BundleElement> mem_req_elements;
	mem_req_elements.push_back(getReadyElement());
	mem_req_elements.push_back(getValidElement());
	mem_req_elements.push_back(BundleElement(
					builder.getStringAttr("addr"),
					false,
					circt::firrtl::IntType::get(builder.getContext(),
								    false, 64))
			       );
	mem_req_elements.push_back(BundleElement(
					builder.getStringAttr("data"),
					false,
					circt::firrtl::IntType::get(builder.getContext(),
								    false, 64))
			       );
	mem_req_elements.push_back(BundleElement(
					builder.getStringAttr("write"),
					false,
					circt::firrtl::IntType::get(builder.getContext(),
								    false, 1))
			       );
	mem_req_elements.push_back(BundleElement(
					builder.getStringAttr("width"),
					false,
					circt::firrtl::IntType::get(builder.getContext(),
								    false, 3))
			       );

	auto mem_type = circt::firrtl::BundleType::get(mem_req_elements, builder.getContext());
	struct circt::firrtl::PortInfo mem_bundle = {
		builder.getStringAttr("mem_req"),
		mem_type,
		circt::firrtl::Direction::Out,
		{builder.getStringAttr("")},
		location,
	};
	ports->push_back(mem_bundle);
}

void
jlm::hls::MLIRGenImpl::addMemResPort(llvm::SmallVector<circt::firrtl::PortInfo> *ports) {
	using BundleElement = circt::firrtl::BundleType::BundleElement;

	llvm::SmallVector<BundleElement> mem_res_elements;
	mem_res_elements.push_back(getValidElement());
	mem_res_elements.push_back(BundleElement(
					builder.getStringAttr("data"),
					false,
					circt::firrtl::IntType::get(builder.getContext(),
								    false, 64))
			       );

	auto mem_res_type = circt::firrtl::BundleType::get(mem_res_elements, builder.getContext());
	struct circt::firrtl::PortInfo mem_res_bundle = {
		builder.getStringAttr("mem_res"),
		mem_res_type,
		circt::firrtl::Direction::In,
		{builder.getStringAttr("")},
		location,
	};
	ports->push_back(mem_res_bundle);
}

void
jlm::hls::MLIRGenImpl::addBundlePort(
			llvm::SmallVector<circt::firrtl::PortInfo> *ports,
			circt::firrtl::Direction direction,
			std::string name,
			circt::firrtl::FIRRTLType type) {
	using BundleElement = circt::firrtl::BundleType::BundleElement;

	llvm::SmallVector<BundleElement> elements;
	elements.push_back(getReadyElement());
	elements.push_back(getValidElement());
	elements.push_back(BundleElement(
					builder.getStringAttr("data"),
					false,
					type));

	auto bundle_type = circt::firrtl::BundleType::get(elements, builder.getContext());
	struct circt::firrtl::PortInfo bundle = {
		builder.getStringAttr(name),
		bundle_type,
		direction,
		{builder.getStringAttr("")},
		location,
	};
	ports->push_back(bundle);
}

circt::firrtl::SubfieldOp
jlm::hls::MLIRGenImpl::getSubfield(mlir::Block *body, mlir::Value value, int index) {
	auto subfield = builder.create<circt::firrtl::SubfieldOp>(location, value, index);
	body->push_back(subfield);
	return subfield;
}

void
jlm::hls::MLIRGenImpl::connect(mlir::Block *body, mlir::Value sink, mlir::Value source) {
	body->push_back(builder.create<circt::firrtl::ConnectOp>(
				location,
				sink,
				source));
}

void
jlm::hls::MLIRGenImpl::partialConnect(mlir::Block *body, mlir::Value sink, mlir::Value source) {
	body->push_back(builder.create<circt::firrtl::PartialConnectOp>(
				location,
				sink,
				source));
}


circt::firrtl::ConstantOp
jlm::hls::MLIRGenImpl::getConstant(mlir::Block *body, int size, int value) {
	auto intType= getIntType(size);
	auto constant = builder.create<circt::firrtl::ConstantOp>(
			location,
			intType,
			llvm::APInt(size, value));
	body->push_back(constant);
	return constant;
}



// Get the clock signal in the module
mlir::BlockArgument
jlm::hls::MLIRGenImpl::getClockSignal(circt::firrtl::FModuleOp module) {
	auto clock = module.getArgument(0);
	auto ctype = clock.getType().cast<circt::firrtl::FIRRTLType>();
	if (!ctype.isa<circt::firrtl::ClockType>()) {
		assert("Not a ClockType");
	}
	return clock;
}


// Get the reset signal in the module
mlir::BlockArgument
jlm::hls::MLIRGenImpl::getResetSignal(circt::firrtl::FModuleOp module) {
	auto reset = module.getArgument(1);
	auto rtype = reset.getType().cast<circt::firrtl::FIRRTLType>();
	if (!rtype.isa<circt::firrtl::ResetType>()) {
		assert("Not a ResetType");
	}
	return reset;
}

circt::firrtl::BundleType::BundleElement
jlm::hls::MLIRGenImpl::getReadyElement() {
	using BundleElement = circt::firrtl::BundleType::BundleElement;

	return BundleElement(
		builder.getStringAttr("ready"),
		true,
		circt::firrtl::IntType::get(builder.getContext(), false, 1));
}

circt::firrtl::BundleType::BundleElement
jlm::hls::MLIRGenImpl::getValidElement() {
	using BundleElement = circt::firrtl::BundleType::BundleElement;

	return BundleElement(
		builder.getStringAttr("valid"),
		false,
		circt::firrtl::IntType::get(builder.getContext(), false, 1));
}

void
jlm::hls::MLIRGenImpl::initializeMemReq(circt::firrtl::FModuleOp module) {
	auto args = module.getArguments().size();
	mlir::BlockArgument mem = module.getArgument(args-2);
	mlir::Block *body = module.getBody();
	
	auto zeroBitValue = getConstant(body, 1, 0);
	auto invalid1 = builder.create<circt::firrtl::InvalidValueOp>(
				location,
				getIntType(1));
	body->push_back(invalid1);
	auto invalid3 = builder.create<circt::firrtl::InvalidValueOp>(
				location,
				getIntType(3));
	body->push_back(invalid3);
	auto invalid64 = builder.create<circt::firrtl::InvalidValueOp>(
				location,
				getIntType(64));
	body->push_back(invalid64);
	
	auto memValid = getSubfield(body, mem, 1);
	auto memAddr  = getSubfield(body, mem, 2);
	auto memData  = getSubfield(body, mem, 3);
	auto memWrite = getSubfield(body, mem, 4);
	auto memWidth = getSubfield(body, mem, 5);

	connect(body, memValid, zeroBitValue);
	connect(body, memAddr,  invalid64);
	connect(body, memData,  invalid64);
	connect(body, memWrite, invalid1);
	connect(body, memWidth, invalid3);
}

// Takes a jive::simple_node and creates a firrtl module with an input
// bundle for each node input and output bundle for each node output
// Returns a circt::firrtl::FModuleOp with an empty body
circt::firrtl::FModuleOp
jlm::hls::MLIRGenImpl::nodeToModule(const jive::simple_node *node) {
	// Generate a vector with all inputs and outputs of the module
	llvm::SmallVector<circt::firrtl::PortInfo> ports;

	// Clock and reset ports
	addClockPort(&ports);
	addResetPort(&ports);
	// Input bundle port
	for (size_t i = 0; i < node->ninputs(); ++i) {
		std::string name("i");
		name.append(std::to_string(i));
		addBundlePort(
			&ports,     
			circt::firrtl::Direction::In,
			name,
			getIntType(&node->input(i)->type()));
	}
	for (size_t i = 0; i < node->noutputs(); ++i) {
		std::string name("o");
		name.append(std::to_string(i));
		addBundlePort(
			&ports,     
			circt::firrtl::Direction::Out,
			name,
			getIntType(&node->output(i)->type()));
	}

	// Creat a name for the module
	mlir::StringAttr name = getModuleName(node);
	// Create the module
	return builder.create<circt::firrtl::FModuleOp>(location, name, ports);
}

//
// HLS only works with wires so all types are represented as unsigned integers
//

// Returns IntType of the specified width
circt::firrtl::IntType
jlm::hls::MLIRGenImpl::getIntType(int size) {
	return circt::firrtl::IntType::get(builder.getContext(), false, size);
}

// Return unsigned IntType with the bit width specified by the
// jive::type. The extend argument extends the width of the IntType,
// which is usefull for, e.g., additions where the result has to be 1
// larger than the operands to accomodate for the carry.
circt::firrtl::IntType
jlm::hls::MLIRGenImpl::getIntType(const jive::type *type, int extend) {
	return circt::firrtl::IntType::get(builder.getContext(), false, jlmSize(type)+extend);
}

// Return the bit width of the jive::type
int
jlm::hls::MLIRGenImpl::jlmSize(const jive::type *type) {
	if (auto bt = dynamic_cast<const jive::bittype *>(type)) {
		return bt->nbits();
	} else if (auto at = dynamic_cast<const jlm::arraytype *>(type)) {
		return jlmSize(&at->element_type()) * at->nelements();
	} else if (dynamic_cast<const jlm::ptrtype *>(type)) {
		return 64;
	} else if (auto ct = dynamic_cast<const jive::ctltype *>(type)) {
		return ceil(log2(ct->nalternatives()));
	} else if (dynamic_cast<const jive::statetype *>(type)) {
		return 1;
	} else {
		throw std::logic_error(type->debug_string() + " size of not implemented!");
	}
}

mlir::StringAttr
jlm::hls::MLIRGenImpl::getModuleName(const jive::node *node) {
	auto newName = jive::detail::strfmt("op_", node->operation().debug_string());
	// Remove characters that are not valid in firrtl module names
	std::replace_if(newName.begin(), newName.end(), isForbiddenChar, '_');
	return builder.getStringAttr(newName);
}
