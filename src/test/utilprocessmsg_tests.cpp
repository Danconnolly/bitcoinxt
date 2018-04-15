// Copyright (c) 2016 The Bitcoin XT developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "utilprocessmsg.h"
#include "options.h"
#include "test/thinblockutil.h"

#include <limits.h>

BOOST_AUTO_TEST_SUITE(utilprocessmsg_tests);

BOOST_AUTO_TEST_CASE(keep_outgoing_peer) {

    auto arg = new DummyArgGetter;
    auto argraii = SetDummyArgGetter(std::unique_ptr<ArgGetter>(arg));

    // Node that does not support thin blocks.
    DummyNode node;
    node.nServices = 0;
    node.nVersion = SENDHEADERS_VERSION; //< version before compact blocks

    // Thin blocks disabled. Still a keeper.
    arg->Set("-use-thin-blocks", 0);
    BOOST_CHECK(KeepOutgoingPeer(node));

    // Thin block enabled. Node does not support it.
    arg->Set("-use-thin-blocks", 1);
    BOOST_CHECK(!KeepOutgoingPeer(node));

    // Node supports xthin, keep.
    node.nServices = NODE_THIN;
    BOOST_CHECK(KeepOutgoingPeer(node));

    // Node supports xthin, keep.
    node.nServices = NODE_THIN;
    BOOST_CHECK(KeepOutgoingPeer(node));

    // Node supports compact blocks, keep.
    node.nServices = 0;
    node.nVersion = SHORT_IDS_BLOCKS_VERSION;
    BOOST_CHECK(KeepOutgoingPeer(node));
}

BOOST_AUTO_TEST_SUITE_END()
