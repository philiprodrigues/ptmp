#include "ptmp/api.h"
#include "json.hpp"
#include "czmq.h"

#include "CLI11.hpp"

#include <cstdio>

// Every message data is assumed to be prefixed by a 64 bit unsigned
// int holding the size of the message data in bytes.
zmsg_t* read_msg(FILE* fp)
{
    size_t size=0;
    int nread = fread(&size, sizeof(size_t), 1, fp);
    if (nread != 1) {
        return NULL;
    }
    zchunk_t* chunk = zchunk_read(fp, size);
    zframe_t* frame = zchunk_pack(chunk);
    zchunk_destroy(&chunk);
    zmsg_t* msg = zmsg_decode(frame);
    zframe_destroy(&frame);
    return msg;
}

int main(int argc, char** argv)
{
    CLI::App app{"Print dumped TPSets"};

    std::string input_file{"/nfs/sw/work_dirs/phrodrig/hit-dumps/run8567/FELIX_BR_508.dump"};
    app.add_option("-f", input_file, "Input file", true);

    int nsets=1000;
    app.add_option("-n", nsets, "number of TPSets to print (-1 for no limit)", true);

    CLI11_PARSE(app, argc, argv);

    const char* address="inproc://sets";
    zsock_t* sender=zsock_new(ZMQ_PUSH);
    zsock_bind(sender, address);

    nlohmann::json recv_config;
    recv_config["socket"]["type"]="SUB";
    recv_config["socket"]["connect"].push_back(address);

    ptmp::TPReceiver receiver(recv_config.dump());
    
    FILE* fp=fopen(input_file.c_str(), "r");
    zmsg_t* msg=NULL;
    int counter=0;
    while((msg = read_msg(fp))){
        zsock_send(sender, "m", msg);
        ptmp::data::TPSet tpset;
        receiver(tpset);
        printf("%d 0x%06x %ld %d\n", tpset.count(), tpset.detid(), tpset.tstart(), tpset.tps().size()); 
        ++counter;
        if(nsets>0 && counter>nsets) break;
    }

    zsock_destroy(&sender);
}
