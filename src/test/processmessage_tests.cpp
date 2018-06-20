#include "test/dummyconnman.h"
#include "test/thinblockutil.h"
#include "blockheaderprocessor.h"
#include "bloom.h"
#include "chain.h"
#include "chainparams.h"
#include "inflightindex.h"
#include "merkleblock.h"
#include "net.h"
#include "process_xthinblock.h"
#include "testutil.h"
#include "util.h" // for fPrintToDebugLog
#include "xthin.h"
#include "compactthin.h"
#include "compactblockprocessor.h"
#include <boost/test/unit_test_suite.hpp>
#include <boost/test/test_tools.hpp>

template <class WORKER_TYPE>
struct DummyWorker : public WORKER_TYPE {

    DummyWorker(ThinBlockManager& m, NodeId i) :
        WORKER_TYPE(m, i), isBuilt(false), buildStubCalled(false)
    { }

    void buildStub(const StubData& s, const TxFinder& f, CConnman& c, CNode& n) override {
        buildStubCalled = true;
        WORKER_TYPE::buildStub(s, f, c, n);
    }
    bool isStubBuilt(const uint256& block) const override {
        return isBuilt;
    }
    bool isBuilt;
    bool buildStubCalled;
};

struct DummyHeaderProcessor : public BlockHeaderProcessor {

    DummyHeaderProcessor() : headerOK(true), called(false) { }

    CBlockIndex* operator()(const std::vector<CBlockHeader>&, bool, bool) override {
        called = true;
        if (!headerOK)
            throw BlockHeaderError("header not OK");

        static CBlockIndex index;
        index.nHeight = 2;
        return &index;
    }
    bool requestConnectHeaders(const CBlockHeader& h, CConnman&, CNode& from, bool) override {
        return false;
    }
    bool headerOK;
    bool called;
};

struct XThinBlockSetup {

    XThinBlockSetup() :
        tmgr(std::unique_ptr<ThinBlockFinishedCallb>(new DummyFinishedCallb),
             std::unique_ptr<InFlightEraser>(new DummyInFlightEraser))
    {
        SelectParams(CBaseChainParams::MAIN);
        fPrintToDebugLog = false;
    }

    CDataStream stream(const XThinBlock& block) {
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);

        stream << block;
        return stream;;
    }

    XThinBlock xblock;
    DummyConnman connman;
    DummyNode pfrom;
    ThinBlockManager tmgr;
    DummyHeaderProcessor headerprocessor;
};

struct DummyXThinProcessor : public XThinBlockProcessor {

    DummyXThinProcessor(CConnman& c, CNode& f, ThinBlockWorker& w,
                        BlockHeaderProcessor& h) :
        XThinBlockProcessor(c, f, w, h), misbehaved(0)
    { }

    void misbehave(int howmuch, const std::string&) override {
        misbehaved += howmuch;
    }
    int misbehaved;
};

BOOST_FIXTURE_TEST_SUITE(process_xthinblock_tests, XThinBlockSetup);

BOOST_AUTO_TEST_CASE(xthinblock_ignore_invalid) {
    XThinBlock xblock(TestBlock1(), CBloomFilter());;
    xblock.txHashes.clear(); // <- makes block invalid

    DummyWorker<XThinWorker> worker(tmgr, 42);
    worker.addWork(xblock.header.GetHash());
    CDataStream s = stream(xblock);
    DummyXThinProcessor process(connman, pfrom, worker, headerprocessor);
    process(s, NullFinder(), MAX_BLOCK_SIZE, 1);

    // Should reset the worker.
    BOOST_CHECK(!worker.isWorking());

    // ...and misbehave the client.
    BOOST_CHECK_EQUAL(20, process.misbehaved);
    BOOST_CHECK(!worker.buildStubCalled);
    BOOST_CHECK(connman.MsgWasSent(pfrom, "reject", 0));
}

BOOST_AUTO_TEST_CASE(xthinblock_ignore_if_has_block_data) {
    // Add block to mapBlockIndex (so we already have it)
    XThinBlock xblock(TestBlock1(), CBloomFilter()); // <- has only coinbase
    DummyBlockIndexEntry dummyEntry(xblock.header.GetHash());

    DummyWorker<XThinWorker> worker(tmgr, 42);
    worker.addWork(xblock.header.GetHash());
    CDataStream s = stream(xblock);
    DummyXThinProcessor process(connman, pfrom, worker, headerprocessor);
    process(s, NullFinder(), MAX_BLOCK_SIZE, 1);

    // peer should not be working on anything
    BOOST_CHECK(!worker.isWorking());
    BOOST_CHECK(connman.NumMessagesSent(pfrom) == 0); //<- no re-requesting
}

BOOST_AUTO_TEST_CASE(xthinblock_header_is_processed) {
    XThinWorker worker(tmgr, 42);
    XThinBlock xblock(TestBlock1(), CBloomFilter());
    worker.addWork(xblock.header.GetHash());
    CDataStream s = stream(xblock);
    DummyXThinProcessor process(connman, pfrom, worker, headerprocessor);
    process(s, NullFinder(), MAX_BLOCK_SIZE, 1);

    BOOST_CHECK(headerprocessor.called);
    BOOST_CHECK(worker.isStubBuilt(xblock.header.GetHash()));
    BOOST_CHECK(worker.isWorkingOn(xblock.header.GetHash()));
}

BOOST_AUTO_TEST_CASE(xthinblock_stop_if_header_fails) {
    XThinWorker worker(tmgr, 42);
    XThinBlock xblock(TestBlock1(), CBloomFilter());
    worker.addWork(xblock.header.GetHash());
    headerprocessor.headerOK = false;
    CDataStream s = stream(xblock);
    DummyXThinProcessor process(connman, pfrom, worker, headerprocessor);
    process(s, NullFinder(), MAX_BLOCK_SIZE, 1);

    BOOST_CHECK(!worker.isWorkingOn(xblock.header.GetHash()));
    BOOST_CHECK(!worker.isStubBuilt(xblock.header.GetHash()));
}

BOOST_AUTO_TEST_CASE(xthinblock_rerequest_missing) {
    CBlock testblock = TestBlock1();

    // bloom filter will match all, so non will be included.
    XThinBlock xblock(testblock, CBloomFilter());
    XThinWorker worker(tmgr, 42);

    worker.addWork(xblock.header.GetHash());
    CDataStream s = stream(xblock);
    DummyXThinProcessor process(connman, pfrom, worker, headerprocessor);
    process(s, NullFinder(), MAX_BLOCK_SIZE, 1);

    BOOST_CHECK(headerprocessor.called);
    BOOST_CHECK(worker.isStubBuilt(xblock.header.GetHash()));
    BOOST_CHECK(worker.isWorkingOn(xblock.header.GetHash()));

    // should have re-requested missing transaction
    BOOST_CHECK(connman.MsgWasSent(pfrom, "get_xblocktx", 0));

    // create a bloom filter that matches non, so we
    // don't have to re-request.
    CBloomFilter f;
    f.clear();
    connman.ClearMessages(pfrom);
    xblock = XThinBlock(TestBlock1(), f);
    s = stream(xblock);
    process(s, NullFinder(), MAX_BLOCK_SIZE, 1);
    BOOST_CHECK(connman.NumMessagesSent(pfrom) == 0);
}

// We want to call build stub even if we have one.
// This thinblock may contain transactions we're missing.
BOOST_AUTO_TEST_CASE(rebuild_stub) {
    DummyWorker<XThinWorker> worker(tmgr, 42);
    XThinBlock xblock(TestBlock1(), CBloomFilter());
    worker.addWork(xblock.header.GetHash());
    worker.isBuilt = true;
    CDataStream s = stream(xblock);
    DummyXThinProcessor process(connman, pfrom, worker, headerprocessor);
    process(s, NullFinder(), MAX_BLOCK_SIZE, 1);

    BOOST_CHECK(worker.buildStubCalled);
}

BOOST_AUTO_TEST_SUITE_END();
BOOST_AUTO_TEST_SUITE(compactblockprocessor_tests);

// TODO: Move to compactblockprocessor_tests.cpp after thin block
// announcements are merged.
BOOST_AUTO_TEST_CASE(compactblockprocessor_fetch_full) {
    CBlock testblock = TestBlock1();

    DummyConnman connman;
    DummyNode node;
    std::unique_ptr<ThinBlockManager> tmgr = GetDummyThinBlockMg();
    CompactWorker worker(*tmgr, node.id);
    DummyHeaderProcessor hprocessor;
    CompactBlockProcessor p(connman, node, worker, hprocessor);

    // create a invalid compact block (obviously too big)
    CompactBlock block(TestBlock1(), CoinbaseOnlyPrefiller{});

    uint64_t currMaxBlockSize = 1000 * 100; // 100kb (for faster unittest)
    size_t tooManyTx = std::ceil(currMaxBlockSize * 1.05 / minTxSize()) + 1;
    block.prefilledtxn.resize(tooManyTx, block.prefilledtxn.at(0));

    CTxMemPool mpool(CFeeRate(0));
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << block;
    p(stream, mpool, currMaxBlockSize, 1);

    // should have rejected the block
    BOOST_CHECK_EQUAL(size_t(1), connman.NumMessagesSent(node));
    BOOST_CHECK(connman.MsgWasSent(node, "reject", 0));
}

BOOST_AUTO_TEST_SUITE_END();
