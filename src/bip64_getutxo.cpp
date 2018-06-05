#include "bip64_getutxo.h"
#include "txmempool.h"
#include "coins.h"

namespace bip64 {

UTXORetriever::UTXORetriever(
        const std::vector<COutPoint>& o,
        CCoinsView& viewChain,
        CTxMemPool* mempool) : outpoints(o)
{
    if (mempool != nullptr) {
        // also check mempool if provided
        CCoinsViewMemPool viewMempool(&viewChain, *mempool);
        Process(&viewMempool, mempool);
    }
    else {
        Process(&viewChain, nullptr);
    }
}

void UTXORetriever::Process(CCoinsView* view, CTxMemPool* mempool) {
    for (const COutPoint& vOutPoint : outpoints) {
        Coin coin;
        bool hit = view->GetCoin(vOutPoint, coin);
        if (mempool)
            hit = hit && !mempool->isSpent(vOutPoint);
        hits.push_back(hit);
        if (hit) outs.emplace_back(std::move(coin));
    }
}

std::vector<uint8_t> UTXORetriever::GetBitmap() const {
    std::vector<uint8_t> bitmap;
    bitmap.resize((outpoints.size() + 7) / 8);
    for (size_t i = 0; i < hits.size(); ++i) {
        const bool hit = hits[i];
        bitmap[i / 8] |= ((uint8_t)hit) << (i % 8);
    }
    return bitmap;
}

std::string UTXORetriever::GetBitmapStr() const {
    std::string bitmap;
    for (bool hit : hits) {
        bitmap.append(hit ? "1" : "0");
    }
    return bitmap;
}

} // ns bip64
