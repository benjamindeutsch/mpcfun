// Minimal end-to-end sanity check for the emp-toolkit semi-honest 2PC stack:
// garbling, OT (IKNP), network I/O, evaluation, and output reveal.
//
// Alice and Bob each hold a private 32-bit bitstring. The circuit computes
// bitwise AND, OR, XOR, NOT (of Alice's bits), and equality over BitVec_T --
// plain logical wires, no arithmetic anywhere -- and both parties learn all
// results publicly. This has nothing to do with the larger project -- it
// just proves the pipeline works before building anything real on top of it.
//
// Usage:
//   ./sh2pc_demo <party: 1=ALICE, 2=BOB> <secret-32-bit-string>
//
// Alice listens on a fixed localhost port (default 12345, override with the
// EMP_PORT env var), Bob connects to it (override the address with
// EMP_PEER_IP).

#include "emp-sh2pc/emp-sh2pc.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace emp;

constexpr int VARS = 32;
constexpr int CUBES = 32;

using BitVec = BitVec_T<SH2PCSession::ctx_t, VARS>;

// "10110001010101110000111100001111" (exactly VARS chars of '0'/'1') -> the
// array<bool,N> clear_t that BitVec_T's input/reveal expect. Left-to-right
// string order = index 0..N-1; this is a plain bit vector, not a number, so
// there's no endianness to get "right", only self-consistency with
// bits_to_string below.
std::array<bool, VARS> parse_bits(const std::string& s) {
    if ((int)s.size() != VARS)
        throw std::invalid_argument(
            "bitstring must be exactly " + std::to_string(VARS) + " chars of '0'/'1'");
    std::array<bool, VARS> bits{};
    for (int i = 0; i < VARS; ++i) {
        if (s[i] != '0' && s[i] != '1')
            throw std::invalid_argument("bitstring must contain only '0'/'1'");
        bits[i] = (s[i] == '1');
    }
    return bits;
}

std::string bits_to_string(const std::array<bool, VARS>& bits) {
    std::string s(VARS, '0');
    for (int i = 0; i < VARS; ++i) s[i] = bits[i] ? '1' : '0';
    return s;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <party: 1=ALICE, 2=BOB> <secret-" << VARS << "-bit-string>\n";
        return 1;
    }

    int party = parse_party(argv);
    std::array<bool, VARS> my_bits = parse_bits(argv[2]);

    int port = peer_port();
    auto io = (party == ALICE) ? NetIO::listen(port)
                                : NetIO::connect(peer_ip(), port);

    SH2PCSession sess(io.get(), party);

    BitVec a = sess.input<BitVec>(ALICE, my_bits);
    BitVec b = sess.input<BitVec>(BOB,   my_bits);

    BitVec bits_and = a & b;
    BitVec bits_or  = a | b;
    BitVec bits_xor = a ^ b;
    BitVec bits_not = ~a;
    Bit_T<SH2PCSession::ctx_t> bits_eq = (a == b);

    std::array<bool, VARS> and_out = sess.reveal(bits_and, PUBLIC).value();
    std::array<bool, VARS> or_out  = sess.reveal(bits_or,  PUBLIC).value();
    std::array<bool, VARS> xor_out = sess.reveal(bits_xor, PUBLIC).value();
    std::array<bool, VARS> not_out = sess.reveal(bits_not, PUBLIC).value();
    bool eq_out = sess.reveal(bits_eq, PUBLIC).value();

    sess.finalize();

    std::cout << (party == ALICE ? "[alice]" : "[bob]  ")
              << " my bits=" << argv[2]
              << "  AND="    << bits_to_string(and_out)
              << "  OR="     << bits_to_string(or_out)
              << "  XOR="    << bits_to_string(xor_out)
              << "  NOT(a)=" << bits_to_string(not_out)
              << "  a==b="   << (eq_out ? "true" : "false") << std::endl;

    return 0;
}
