// a variant on TPMonitor where we use zmq_proxy_steerable instead of DIY

#include "ptmp/api.h"
#include "ptmp/data.h"
#include "ptmp/internals.h"
#include "ptmp/factory.h"
#include "ptmp/actors.h"

#include <cstdio>

#include "json.hpp"
using json = nlohmann::json;

PTMP_AGENT(ptmp::TPMonitorz, monitor)


// An actor function that starts a steerable proxy.  
void ptmp::actor::tap(zsock_t* pipe, void* vargs)
{
    auto config = json::parse((const char*) vargs);
    const uint32_t tapid = config["id"];

    if (config["name"].is_string()) {
        ptmp::internals::set_thread_name(config["name"].get<std::string>());
    }
    else {
        char* name = zsys_sprintf("montap%05d", tapid);
        ptmp::internals::set_thread_name(name);
        freen (name);
    }
    

    zsock_t* fe_sock = ptmp::internals::endpoint(config["input"].dump());
    zsock_t* be_sock = ptmp::internals::endpoint(config["output"].dump());
    std::string attach = "pushpull";
    if (config["attach"].is_string()) {
        attach = config["attach"];
    }

    zsock_t* cap_sock = NULL;
    char* cap_addr = zsys_sprintf("inproc://capture%05d", tapid);
    if (attach == "pubsub") {
        cap_sock = zsock_new_pub(cap_addr);
    }
    else if (attach == "pushpull") {
        cap_sock = zsock_new_push(cap_addr);
    }
    else {
        zsys_error("unknown attachement type: %s", attach.c_str());
        throw std::runtime_error("unknown attachement type");
    }
    freen (cap_addr);

    char* con_addr = zsys_sprintf("inproc://control%05d", tapid);
    zsock_t* con_sock = zsock_new_pull(con_addr);
    freen (con_addr);

    zsock_signal(pipe, 0); // signal ready    

    // runs forever until someone sends TERMINATE up its caboose
    zmq_proxy_steerable(zsock_resolve(fe_sock),
                        zsock_resolve(be_sock),
                        zsock_resolve(cap_sock),
                        zsock_resolve(con_sock));

    zsock_destroy(&con_sock);
    zsock_destroy(&cap_sock);
    zsock_destroy(&be_sock);
    zsock_destroy(&fe_sock);
}


void ptmp::actor::monitor(zsock_t* pipe, void* vargs)
{
    auto config = json::parse((const char*) vargs);

    std::string name = "monitor";
    if (config["name"].is_string()) {
        name = config["name"];
    }
    ptmp::internals::set_thread_name(name);

    std::string filename = config["filename"];
    std::string attach = "pushpull";
    if (config["attach"].is_string()) {
        attach = config["attach"];
    }

    struct tapinfo_t {
        zactor_t* proxy;
        zsock_t* cap;           // capture
        zsock_t* con;           // control
        int id;
        int nrecv;
    };

    std::vector<zactor_t*> taps;

    // map tap socket pointer to info about the tap
    std::unordered_map<void*, tapinfo_t> tapinfos;
    zpoller_t* poller = zpoller_new(pipe, NULL);

    for (auto& jtap : config["taps"]) {
        const int tapid = jtap["id"];
        char* cap_addr = zsys_sprintf("inproc://capture%05d", tapid);
        char* con_addr = zsys_sprintf("inproc://control%05d", tapid);

        zsys_debug("monitor: %s CAPTURE %s CONTROL %s", attach.c_str(), cap_addr, con_addr);

        jtap["attach"] = attach;
        std::string tapcfg = jtap.dump();
        zsock_t* cap_sock = NULL;
        if (attach == "pubsub") {
            cap_sock = zsock_new_sub(cap_addr, "");
        }
        else if (attach == "pushpull") {
            cap_sock = zsock_new_pull(cap_addr);
        }
        else {
            zsys_error("unknown attachement type: %s", attach.c_str());
            throw std::runtime_error("unknown attachement type");
        }
        tapinfo_t ti {
            zactor_new(ptmp::actor::tap, (void*)tapcfg.c_str()),
            cap_sock,
            zsock_new_push(con_addr),
            tapid,
            0
        };
        freen (cap_addr);
        freen (con_addr);

        assert (ti.proxy and ti.cap and ti.con);

        zpoller_add(poller, ti.cap);

        tapinfos[(void*)ti.cap] = ti;
        zsys_debug("monitor: made tap %d", tapid);
    }
    
    zsock_signal(pipe, 0); // signal ready    

    // open file...
    FILE* fp = fopen(filename.c_str(), "w");
    if (!fp) {
        zsys_error("monitor: failed to open file %s", filename.c_str());
        return;
    }
    fprintf(fp, "tapid now nrecv nrecvtot count detid created tstart tspan ntps\n");

    int nrecv_tot=0;
    bool got_quit = false;
    while (!zsys_interrupted) {

        void* which = zpoller_wait(poller, -1);
        if (!which) {
            zsys_info("monitor: interrupted");
            break;
        }
        if (which == pipe) {
            zsys_info("monitor: got quit");
            got_quit = true;
            break;
        }

        auto tiit = tapinfos.find(which);
        if (tiit == tapinfos.end()) {
            zsys_error("monitor: unknown tap");
            continue;
        }
        auto& ti = tiit->second;

        ptmp::data::TPSet tpset;
        zmsg_t* msg = zmsg_recv(ti.cap);
        ptmp::internals::recv(&msg, tpset);
        int64_t now = ptmp::data::now();
        ++nrecv_tot;
        ++ti.nrecv;

        fprintf(fp, "%d %ld %d %d %d %d %ld %ld %d %d\n",
                ti.id, now, ti.nrecv, nrecv_tot,
                tpset.count(), tpset.detid(), tpset.created(),
                tpset.tstart(), tpset.tspan(), tpset.tps_size());
    }


    // close file ...
    fclose(fp); fp=NULL;

    zsys_debug("monitor: signalling taps to shutdown");
    zpoller_destroy(&poller);
    for (auto tiit : tapinfos) {
        auto& ti = tiit.second;

        zsys_debug("monitor: cap ID %d", ti.id);
        zsock_destroy(&ti.cap);

        int rc = zstr_sendx(ti.con, "TERMINATE", NULL);
        assert(rc == 0);

        zsys_debug("monitor: con ID %d", ti.id);
        zsock_destroy(&ti.con);
    }
    for (auto tiit : tapinfos) {
        auto& ti = tiit.second;

        zsys_debug("monitor: proxy for tap ID %d", ti.id);
        zactor_destroy(&ti.proxy);
    }

    if (got_quit) {
        return;
    }
    zsys_debug("monitor: waiting for quit");
    zsock_wait(pipe);
}


ptmp::TPMonitorz::TPMonitorz(const std::string& config)
    : m_actor(zactor_new(ptmp::actor::monitor, (void*)config.c_str()))
{
}

ptmp::TPMonitorz::~TPMonitorz()
{
    zsock_signal(zactor_sock(m_actor), 0); // signal quit
    zactor_destroy(&m_actor);
}
