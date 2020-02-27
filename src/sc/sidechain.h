#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "chain.h"
#include "hash.h"
#include <boost/unordered_map.hpp>
#include "sync.h"

#include "sc/sidechaintypes.h"

//------------------------------------------------------------------------------------
class CTxMemPool;
class CBlockUndo;
class UniValue;
class CValidationState;
class CLevelDBWrapper;

class ScInfo {
public:
    ScInfo() : creationBlockHash(), creationBlockHeight(-1), creationTxHash(),
         lastReceivedCertificateEpoch(CScCertificate::EPOCH_NULL), balance(0) {}
    
    // reference to the block containing the tx that created the side chain
    uint256 creationBlockHash;

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // hash of the tx who created it
    uint256 creationTxHash;

    // last epoch where a certificate has been received
    int lastReceivedCertificateEpoch;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    Sidechain::ScCreationParameters creationData;

    // immature amounts
    // key   = height at which amount will be considered as mature and will be part of the sc balance
    // value = the immature amount
    std::map<int, CAmount> mImmatureAmounts;

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(creationBlockHash);
        READWRITE(creationBlockHeight);
        READWRITE(creationTxHash);
        READWRITE(lastReceivedCertificateEpoch);
        READWRITE(balance);
        READWRITE(creationData);
        READWRITE(mImmatureAmounts);
    }

    inline bool operator==(const ScInfo& rhs) const
    {
        return (this->creationBlockHash            == rhs.creationBlockHash)            &&
               (this->creationBlockHeight          == rhs.creationBlockHeight)          &&
               (this->creationTxHash               == rhs.creationTxHash)               &&
               (this->lastReceivedCertificateEpoch == rhs.lastReceivedCertificateEpoch) &&
               (this->creationData                 == rhs.creationData)                 &&
               (this->mImmatureAmounts             == rhs.mImmatureAmounts);
    }
    inline bool operator!=(const ScInfo& rhs) const { return !(*this == rhs); }
};

namespace Sidechain {
    bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
    bool anyForwardTransaction(const CTransaction& tx, const uint256& scId);
    bool hasScCreationOutput(const CTransaction& tx, const uint256& scId);

    bool checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state);
}; // end of namespace

#endif // _SIDECHAIN_CORE_H