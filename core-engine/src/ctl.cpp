#include "net.hpp"
#include "mesh_control.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void usage(const char* a0) {
    std::fprintf(stderr,
                 "Usage: %s --target host:port --node-id N --skip MASK\n"
                 "       %s --target host:port --node-id N --clear\n"
                 "  Send ZCMESH mesh control datagram to edge --control port.\n",
                 a0, a0);
}

} // namespace

int main(int argc, char** argv) {
    const char* target = nullptr;
    uint16_t node_id = 1;
    int mask = -1;
    bool clear = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
        } else if (std::strcmp(argv[i], "--node-id") == 0 && i + 1 < argc) {
            node_id = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--skip") == 0 && i + 1 < argc) {
            mask = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--clear") == 0) {
            clear = true;
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (!target || (!clear && mask < 0)) {
        usage(argv[0]);
        return 1;
    }

    if (!zcmesh::net_init()) {
        return 1;
    }
    zcmesh::Endpoint ep{};
    if (!zcmesh::parse_endpoint(target, ep)) {
        std::fprintf(stderr, "bad --target\n");
        zcmesh::net_shutdown();
        return 1;
    }
    zcmesh::UdpSocket udp;
    if (!udp.open()) {
        zcmesh::net_shutdown();
        return 1;
    }

    zcmesh_ctrl_msg msg{};
    msg.magic = ZCMESH_CTRL_MAGIC;
    msg.version = ZCMESH_CTRL_VERSION;
    msg.opcode = clear ? ZCMESH_CTRL_OP_CLEAR : ZCMESH_CTRL_OP_SET_SKIP;
    msg.node_id = node_id;
    msg.mask = clear ? 0 : static_cast<uint8_t>(mask & 0xFF);
    msg.reserved = 0;

    if (!udp.send_to(ep, &msg, sizeof(msg))) {
        std::fprintf(stderr, "send failed\n");
        zcmesh::net_shutdown();
        return 1;
    }
    std::fprintf(stderr, "zcmesh_ctl %s node=%u mask=%u -> %s:%u\n",
                 clear ? "CLEAR" : "SET_SKIP", node_id, msg.mask, ep.host, ep.port);
    zcmesh::net_shutdown();
    return 0;
}
