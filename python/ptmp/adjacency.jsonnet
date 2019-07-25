/**
Configure "ptmper" instances to run "adjacency" triggering.  

The word "adjacency" refers to the use of the triggering algorithms
from ProtoDuneTrigger packge which is based on finding adjacent
trigger primitives to form trigger candidates and then stitch these
into trigger decisions in order to attempt to trigger on horizontal
muons.

The overall topology is a fan-in and the the configuration is
abstracted into numbered "layers" receiving or sending data and that
data is fragmented over a number of sockets in that layer.  Each layer
connects to its neighbors either via network message transmission or
internally through an application.

The layers are:

0. PUB sockets provided by the FELIX_BR hit finder and producing
trigger primitives.

1. SUB sockets provided by ptmper hosting message handling and trigger
candidate algorithm.

2. PUB sockets provided by the above applications producing trigger
candidates.

3. SUB sockets provided by ptmper hosting message handling and module
level trigger algorithm.

4. PUB socket providing final trigger decision.

Note, components in both ptmp and ptmp-tcs plugins are needed and thus
their libraries must be found via the user's LD_LIBRARY_PATH.  

*/

// A number of meta configurations are defined and switched based on a
// single input 'context' (eg "test", "prod").
local context = std.extVar('context');

local all_params = import "params.jsonnet";
local params = all_params[context];

local ptmp = import "ptmp.jsonnet";


local make_tc = function(apa, inputs, output, params) {
    local iota = std.range(0, std.length(inputs)-1),
    local inproc_wz = ["inproc://tc-window-zipper-apa%d-link%02d"%[apa,link] for link in iota],
    local windows = [ptmp.window('tc-window-apa%d-link%02d'%[apa, link],
                                 isocket = ptmp.socket('connect', 'sub', inputs[link],
                                                       hwm = params.pubsub_hwm),
                                 osocket = ptmp.socket('bind', 'push', inproc_wz[link]),
                                 cfg = {name:"tp-window-apa%d-link%02d"%[apa,link]}+params.cfg)
                     for link in iota],
    local inproc_ztc = "inproc://tc-zipper-tcfinder-apa%d" % apa,
    local zipper = ptmp.zipper("tc-zipper-apa%d"%apa,
                               isocket = ptmp.socket('connect','pull', inproc_wz),
                               osocket = ptmp.socket('bind','push', inproc_ztc),
                               cfg = {name:"apa%d-zipper"%apa}+params.cfg),

    local tcfinder = ptmp.nodeconfig("filter", "tc-finder-apa%d"%apa,
                                     isocket = ptmp.socket('connect','pull', inproc_ztc),
                                     osocket = ptmp.socket('bind','pub', output,
                                                           hwm = params.pubsub_hwm),
                                     cfg = {name:"apa%d-adj-tcs"%apa, method: "pdune-adjacency-tc"}),
    components: windows + [zipper, tcfinder],
}.components;

local make_td = function(inputs, output, params) {
    local inproc_ztd = "inproc://td-zipper-tdfinder",
    local zipper = ptmp.zipper("td-zipper",
                               isocket = ptmp.socket('connect', 'sub', inputs,
                                                     hwm = params.pubsub_hwm),
                               osocket = ptmp.socket('bind','push', inproc_ztd),
                               cfg = {name:"td-zipper"}+params.cfg),

    local tdfinder = ptmp.nodeconfig("filter", "td-finder",
                                     isocket = ptmp.socket('connect','pull', inproc_ztd),
                                     osocket = ptmp.socket('bind','pub', output,
                                                           hwm = params.pubsub_hwm),
                                     cfg = {method: "pdune-adjacency-td"}),
    components: [zipper, tdfinder],
}.components;


{
    ["%s-tc-apa%d.json"%[context, apa]]: {
        name: "apa%d-tcs"%apa,
        plugins: ["ptmp-tcs"],
        proxies: make_tc(apa, params.addresses.tps[apa], params.addresses.tc[apa], params)
    } for apa in params.apas
} + {
    ["%s-td.json"%context]: {
        name: "tds",
        plugins: ["ptmp-tcs"],
        proxies: make_td([params.addresses.tc[apa] for apa in params.apas],
                         params.addresses.td, params)
    }
}