import pygraphviz as pgv
import networkx as nx
import Verilog_VCD.Verilog_VCD as vvcd
import os
import typing

G = pgv.AGraph("test6.dot")
# remove back edges
for edge in G.edges():
    if edge.attr.get("dir") == "back":
        attr = dict(edge.attr.items())
        attr.pop("dir")
        headport = attr.pop("headport")
        tailport = attr.pop("tailport")
        attr["headport"] = tailport
        attr["tailport"] = headport
        src, snk = edge
        G.remove_edge(edge)
        G.add_edge(snk, src, **attr)
nG = nx.nx_agraph.from_agraph(G)

os.makedirs("loops", exist_ok=True)
known_cycles = set()


def color_edges(G, net_dict, mark_changed=False):
    def snk_signal_name(e):
        snk_port = e.attr.get("headport")
        src, snk = e
        name = snk
        if snk_port:
            name = name + "_inst_" + snk_port
        return name

    def valid(e, ts):
        hier = ('TOP', 'test_lambda_mod', 'sr', snk_signal_name(e) + "_valid")
        return net_dict[hier][ts] == "1"

    def ready(e, ts):
        hier = ('TOP', 'test_lambda_mod', 'sr', snk_signal_name(e) + "_ready")
        return net_dict[hier][ts] == "1"

    changed = False
    for e in G.edges():
        prev = e.attr["color"], e.attr["style"]
        e.attr["color"] = "black"
        e.attr["style"] = "solid"
        e.attr["penwidth"] = 2
        if not valid(e, timestep):
            e.attr["style"] = "dashed"
        if ready(e, timestep):
            e.attr["color"] = "green"
        new = e.attr["color"], e.attr["style"]
        if new != prev:
            changed = True
            # make changed edges thick
            if mark_changed:
                e.attr["penwidth"] = 4
    return changed


def to_timeseries(l, last_time):
    ts = []
    previous = l[0]
    for current in l:
        ts.extend([previous[1]] * (current[0] - previous[0]))
        previous = current
    ts.extend([previous[1]] * ((last_time + 1) - previous[0]))
    return ts


def create_net_dict(path):
    data = vvcd.parse_vcd(path)
    net_dict = {}
    last_time = max(r['tv'][-1][0] for r in data.values())
    for record in data.values():
        ts = to_timeseries(record["tv"], last_time)
        for net in record["nets"]:
            net_dict[tuple(net["hier"].split(".")) + (net["name"],)] = ts
    return net_dict, last_time


net_dict, last_time = create_net_dict("test6.vcd")
timestep = 80


def get_node_state(node, port, rv):
    name = node
    if port:
        name = name + "_inst_" + port
    hier = ('TOP', 'test_lambda_mod', 'sr', name + "_" + rv)
    return net_dict[hier][timestep] == "1"


def get_in_ports(node):
    result = []
    sink = node
    for src in nG.predecessors(node):
        result.append(nG[src][sink][0].get("headport"))
    return result


def get_out_ports(node):
    result = []
    src = node
    for sink in nG.successors(node):
        result.append(nG[src][sink][0].get("tailport"))
    return result


def get_in_src(node, port):
    sink = node
    for src in nG.predecessors(node):
        if nG[src][sink][0]["headport"] == port:
            src_port = nG[src][sink][0].get("tailport")
            return src, src_port


def get_out_sink(node, port):
    src = node
    for sink in nG.successors(node):
        if nG[src][sink][0]["tailport"] == port:
            sink_port = nG[src][sink][0].get("headport")
            return sink, sink_port


def plot_cycle(cycle):
    # copy is broken for fancy labels
    # g = pgv.AGraph().from_string(G.to_string())
    color_edges(G, net_dict)
    readys = {(node, port) for node, port, direction in cycle if direction == "ready"}
    valids = {(node, port) for node, port, direction in cycle if direction == "valid"}
    for edge in G.edges():
        src, sink = edge
        src_port = edge.attr.get("tailport")
        if (src, src_port) in valids:
            edge.attr["penwidth"] = 4
            edge.attr["color"] = "magenta"
        if (src, src_port) in readys:
            edge.attr["penwidth"] = 4
            edge.attr["color"] = "cyan"
    G.write(f"loops/loop_{len(known_cycles)}.dot")


def isolate_cycle(new_path: typing.List[typing.Tuple[str, str, str]]) -> typing.Tuple[typing.Tuple[str, str, str]]:
    # isolate loop
    loop_path = new_path[new_path.index(new_path[-1])+1:]
    # get canonical form (beginning from alphabetically smallest node)
    start = loop_path.index(min(loop_path))
    cycle = tuple(loop_path[start:]+loop_path[:start])
    assert len(cycle) == len(loop_path)
    if cycle not in known_cycles:
        known_cycles.add(cycle)
        return cycle


def find_cycle(node, port, direction, path=None):
    if path is None:
        path = []
    if port is None:
        return  # ignore arguments and results
    new_path = path + [(node, port, direction)]

    if (node, port, direction) in path:
        cycle = isolate_cycle(new_path)
        if cycle:
            plot_cycle(cycle)
    elif direction == "ready" and "o" in port:
        assert not get_node_state(node, port, "ready")
        sink, sink_port = get_out_sink(node, port)
        find_cycle(sink, sink_port, "ready", new_path)
    elif direction == "valid" and "i" in port:
        assert not get_node_state(node, port, "valid")
        src, src_port = get_in_src(node, port)
        find_cycle(src, src_port, "valid", new_path)
    elif direction == "ready" and "i" in port:
        # in ports
        if "HLS_MERGE" in node:
            pass  # in ready only depends on output
        elif "HLS_NDMUX" in node and port != "i0":
            # pred in needs to be valid
            if not get_node_state(node, "i0", "valid"):
                find_cycle(node, "i0", "valid", new_path)
        else:
            # in ready depends on all ins being valid
            for i_port in get_in_ports(node):  # TODO: all other?
                if not get_node_state(node, i_port, "valid"):
                    find_cycle(node, i_port, "valid", new_path)
        # out ports
        if "HLS_BUF" in node:  # TODO: pred buf?
            pass  # in and out are decoupled
        else:
            for o_port in get_out_ports(node):
                if get_node_state(node, o_port, "valid") and not get_node_state(node, o_port, "ready"):
                    find_cycle(node, o_port, "ready", new_path)
    elif direction == "valid" and "o" in port:
        # in ports
        if "HLS_BUF" in node:  # TODO: pred buf?
            pass  # in and out are decoupled
        else:
            for i_port in get_in_ports(node):
                if not get_node_state(node, i_port, "valid"):
                    find_cycle(node, i_port, "valid", new_path)
        # out ports
        if "HLS_FORK" in node:
            if get_node_state(node, "i0", "valid"):
                for o_port in get_out_ports(node):
                    if get_node_state(node, o_port, "valid") and not get_node_state(node, o_port, "ready"):
                        find_cycle(node, o_port, "ready", new_path)

color_edges(G, net_dict)
G.write(f"loops/reference.dot")
G.layout(prog="dot")
for node in nG.nodes():
    for port in get_out_ports(node):
        if not get_node_state(node, port, "ready"):
            find_cycle(node, port, "ready")
