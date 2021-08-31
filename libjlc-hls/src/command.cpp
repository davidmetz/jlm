/*
 * Copyright 2018 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlc-hls/command.hpp>
#include <jlc-hls/llvmpaths.hpp>
#include <jlm/util/strfmt.hpp>

#include <deque>
#include <functional>
#include <iostream>
#include <memory>

namespace jlm {

/* command generation */

std::unique_ptr<passgraph>
generate_commands(const jlm::cmdline_options & opts)
{
	std::unique_ptr<passgraph> pgraph(new passgraph());

	std::vector<passgraph_node*> leaves;
	std::vector<passgraph_node*> llir;
	std::vector<jlm::filepath> llir_files;
	// c to llvmir
	for (const auto & c : opts.compilations) {
		passgraph_node * last = pgraph->entry();

		if (c.parse()) {
			auto prsnode = prscmd::create(pgraph.get(), c.ifile(), opts.includepaths, opts.macros,
				opts.warnings, opts.flags, opts.std);
			last->add_edge(prsnode);
			last = prsnode;
			llir.push_back(last);
			llir_files.push_back(dynamic_cast<prscmd*>(&last->cmd())->ofile());
		}

	}
	// link all llir into one so inlining can be done across files for HLS
	// llvm-link
	jlm::filepath ll_merged("/tmp/merged.ll");
	auto ll_link = lllnkcmd::create(pgraph.get(), llir_files, ll_merged);
	for (const auto & ll : llir) {
		ll->add_edge(ll_link);
	}
	auto m2r = m2rcmd::create(pgraph.get(), ll_merged);
	ll_link->add_edge(m2r);
	// hls
	auto hls = hlscmd::create(pgraph.get(), dynamic_cast<m2rcmd*>(&m2r->cmd())->ofile(), opts.hls_function_regex);
	m2r->add_edge(hls);
	// firrtl
	jlm::filepath verilogfile("/tmp/jlm_hls.v");
	auto firrtl = firrtlcmd::create(pgraph.get(), dynamic_cast<hlscmd*>(&hls->cmd())->firfile(), verilogfile);
	hls->add_edge(firrtl);
	jlm::filepath asmofile("/tmp/hls.o");
	auto asmnode = cgencmd::create(pgraph.get(), dynamic_cast<hlscmd*>(&hls->cmd())->llfile(), asmofile, opts.Olvl);
	hls->add_edge(asmnode);

	std::vector<jlm::filepath> lnkifiles;
	for (const auto & c : opts.compilations) {
		if (c.link() && !c.parse())
			lnkifiles.push_back(c.ofile());
	}
	lnkifiles.push_back(asmofile);
	// TODO: remove old verilator folder first
	auto verinode = verilatorcmd::create(pgraph.get(), verilogfile, lnkifiles, dynamic_cast<hlscmd*>(&hls->cmd())->harnessfile(), opts.lnkofile, opts.libpaths, opts.libs);
	firrtl->add_edge(verinode);
	verinode->add_edge(pgraph->exit());
//	if (!lnkifiles.empty()) {
//		auto lnknode = lnkcmd::create(pgraph.get(), lnkifiles, opts.lnkofile,
//			opts.libpaths, opts.libs);
//		for (const auto & leave : leaves)
//			leave->add_edge(lnknode);
//
//		leaves.clear();
//		leaves.push_back(lnknode);
//	}
//
//	for (const auto & leave : leaves)
//		leave->add_edge(pgraph->exit());

	if (opts.only_print_commands) {
		std::unique_ptr<passgraph> pg(new passgraph());
		auto printnode = printcmd::create(pg.get(), std::move(pgraph));
		pg->entry()->add_edge(printnode);
		printnode->add_edge(pg->exit());
		pgraph = std::move(pg);
	}

	return pgraph;
}

/* parser command */

static std::string
create_prscmd_ofile(const std::string & ifile)
{
	return strfmt("tmp-", ifile, "-clang-out.ll");
}

prscmd::~prscmd()
{}

jlm::filepath
prscmd::ofile() const
{
	auto f = ifile_.base();
	return "/tmp/"+create_prscmd_ofile(f);
}

std::string
prscmd::to_str() const
{
	std::string Ipaths;
	for (const auto & Ipath : Ipaths_)
		Ipaths += "-I" + Ipath + " ";

	std::string Dmacros;
	for (const auto & Dmacro : Dmacros_)
		Dmacros += "-D" + Dmacro + " ";

	std::string Wwarnings;
	for (const auto & Wwarning : Wwarnings_)
		Wwarnings += "-W" + Wwarning + " ";

	std::string flags;
	for (const auto & flag : flags_)
		flags += "-f" + flag + " ";

	return strfmt(
	  clangpath.to_str() + " "
	, Wwarnings, " "
	, flags, " "
	, std_ != standard::none ? "-std="+jlm::to_str(std_)+" " : ""
	, Dmacros, " "
	, Ipaths, " "
	, "-S -emit-llvm "
	, "-Xclang -disable-O0-optnone "
	, "-o ", ofile().to_str(), " "
	, ifile_.to_str()
	);
}

void
prscmd::run() const
{
	if (system(to_str().c_str()))
		exit(EXIT_FAILURE);
}

/* optimization command */

static std::string
create_optcmd_ofile(const std::string & ifile)
{
	return strfmt("tmp-", ifile, "-jlm-opt-out.ll");
}

optcmd::~optcmd()
{}

std::string
optcmd::to_str() const
{
	auto f = ifile_.base();

	std::string jlmopts;
	for (const auto & jlmopt : jlmopts_)
		jlmopts += "--" + jlmopt + " ";

	/*
		If a default optimization level has been specified (-O) and no specific jlm-options 
		have been specified (-J) then use a default set of optimizations.
	 */
	if (jlmopts.empty()) {
		/*
			Only -O3 sets default optimizations
		*/
		if (ol_ == optlvl::O3) {
			jlmopts  = "--iln --inv --red --dne --ivt --inv --dne --psh --inv --dne ";
			jlmopts += "--red --cne --dne --pll --inv --dne --url --inv ";
		}
	}

	return strfmt(
	  "jlm-opt "
	, "--llvm "
	, jlmopts
	, "/tmp/", create_prscmd_ofile(f), " > /tmp/", create_optcmd_ofile(f)
	);
}

void
optcmd::run() const
{
	if (system(to_str().c_str()))
		exit(EXIT_FAILURE);
}

/* code generator command */

cgencmd::~cgencmd()
{}

std::string
cgencmd::to_str() const
{
	return strfmt(
	  llcpath.to_str() + " "
	, "-", jlm::to_str(ol_), " "
	, "--relocation-model=pic "
	, "-filetype=obj "
	, "-o ", ofile_.to_str()
	, " ", ifile_.to_str()
	);
}

void
cgencmd::run() const
{
	if (system(to_str().c_str()))
		exit(EXIT_FAILURE);
}

/* linker command */

lnkcmd::~lnkcmd()
{}

std::string
lnkcmd::to_str() const
{
	std::string ifiles;
	for (const auto & ifile : ifiles_)
		ifiles += ifile.to_str() + " ";

	std::string Lpaths;
	for (const auto & Lpath : Lpaths_)
		Lpaths += "-L" + Lpath + " ";

	std::string libs;
	for (const auto & lib : libs_)
		libs += "-l" + lib + " ";

	return strfmt(
	  clangpath.to_str() + " "
	, "-O0 "
	, ifiles
	, "-o ", ofile_.to_str(), " "
	, Lpaths
	, libs
	);
}

void
lnkcmd::run() const
{
	if (system(to_str().c_str()))
		exit(EXIT_FAILURE);
}

/* print command */

printcmd::~printcmd()
{}

std::string
printcmd::to_str() const
{
	return "PRINTCMD";
}

void
printcmd::run() const
{
	for (const auto & node : topsort(pgraph_.get())) {
		if (node != pgraph_->entry() && node != pgraph_->exit())
			std::cout << node->cmd().to_str() << "\n";
	}
}

std::string
lllnkcmd::to_str() const {

	auto llvm_link = clangpath.path() + "llvm-link";
	std::string ifiles;
	for (const auto & ifile : ifiles_)
		ifiles += ifile.to_str() + " ";
	return strfmt(
			llvm_link, " "
			, "-S -v "
			, "-o ", ofile_.to_str(), " "
			, ifiles
	);
}

void
lllnkcmd::run() const {
	if (system(to_str().c_str()))
		exit(EXIT_FAILURE);
}

	void
	hlscmd::run() const {
		if (system(to_str().c_str()))
			exit(EXIT_FAILURE);
	}

	std::string
	hlscmd::to_str() const {
		return strfmt(
				"jlm-hls"
				, " --file ", ifile().to_str()
				, " --hls-function ", function()
		);
	}

	void
	firrtlcmd::run() const {
		if (system(to_str().c_str()))
			exit(EXIT_FAILURE);
	}

	std::string
	firrtlcmd::to_str() const {
		return strfmt(
				firrtlpath.to_str()
				, " -i ", ifile().to_str()
				, " -o ", ofile().to_str()
				, " -X verilog"
				, " --no-dce"
		);
	}

	void
	verilatorcmd::run() const {
		if (system(to_str().c_str()))
			exit(EXIT_FAILURE);
	}

	std::string
	gcd() {
		char tmp[256];
		getcwd(tmp, 256);
		auto cd = std::string(tmp);
		return cd;
	}

	std::string
	verilatorcmd::to_str() const {
		std::string lfiles;
		for (const auto & ifile : lfiles_)
			lfiles += ifile.to_str() + " ";

		std::string Lpaths;
		for (const auto & Lpath : Lpaths_)
			Lpaths += "-L" + Lpath + " ";

		std::string libs;
		for (const auto & lib : libs_)
			libs += "-l" + lib + " ";

		std::string cflags;
//		if(!libs.empty()||!Lpaths.empty()){
		cflags = " -CFLAGS \"" + libs + Lpaths + " -fPIC\"";
//		}

		std::string ofile = ofile_.to_str();
		if(ofile.at(0)!='/'){
			ofile = gcd() + "/" + ofile;
		}
		return strfmt(
				"verilator"
				, " --cc"
				, " --build"
				, " --exe"
				, " --trace-fst"
				, " -Wno-WIDTH" //divisions otherwise cause errors
				, " -j"
				, " -Mdir /tmp/verilator/"
				, " -MAKEFLAGS CXX=clang"
				, cflags
				, " -o ", ofile
				, " ", vfile().to_str()
				, " ", hfile().to_str()
				, " ", lfiles
		);
	}

	jlm::filepath
	m2rcmd::ofile() const
	{
		auto f = ifile_.base();
		return "/tmp/"+f+".m2r.ll";
	}

	std::string
	m2rcmd::to_str() const
	{
		auto opt = clangpath.path() + "opt";

		return strfmt(
				opt + " "
				, "-mem2reg -S "
				, "-o ", ofile().to_str(), " "
				, ifile_.to_str()
		);
	}

	void
	m2rcmd::run() const
	{
		if (system(to_str().c_str()))
			exit(EXIT_FAILURE);
	}
}
