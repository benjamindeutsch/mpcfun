// Minimal end-to-end sanity check for the emp-toolkit semi-honest 2PC stack:
// garbling, OT (IKNP), network I/O, evaluation, and output reveal.
//
// Alice and Bob each hold a private 32-bit integer. The circuit computes
// their sum and their max, and both parties learn both results publicly.
// This has nothing to do with the larger project -- it just proves the
// pipeline works before building anything real on top of it.
//
// Usage:
//   ./sh2pc_demo <party: 1=ALICE, 2=BOB> <secret-uint32>
//
// Alice listens on a fixed localhost port (default 12345, override with the
// EMP_PORT env var), Bob connects to it (override the address with
// EMP_PEER_IP).

#include "emp-sh2pc/emp-sh2pc.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace emp;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> <secret-uint32>\n";
        return 1;
    }

    int party = parse_party(argv);
    uint32_t my_secret = static_cast<uint32_t>(std::stoul(argv[2]));

    int port = peer_port();
    auto io = (party == ALICE) ? NetIO::listen(port)
                                : NetIO::connect(peer_ip(), port);

    SH2PCSession sess(io.get(), party);
    using U32 = UInt_T<SH2PCSession::ctx_t, 32>;

    U32 a = sess.input<U32>(ALICE, (uint64_t)my_secret);
    U32 b = sess.input<U32>(BOB,   (uint64_t)my_secret);

    U32 sum = a + b;
    U32 max = a.select(a < b, b);   // a<b ? b : a

    uint32_t sum_out = (uint32_t)sess.reveal(sum, PUBLIC).value();
    uint32_t max_out = (uint32_t)sess.reveal(max, PUBLIC).value();

    sess.finalize();

    std::cout << (party == ALICE ? "[alice]" : "[bob]  ")
              << " my input=" << my_secret
              << "  sum=" << sum_out
              << "  max=" << max_out << std::endl;

    return 0;
}
