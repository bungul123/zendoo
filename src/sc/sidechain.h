#ifndef _SIDECHAIN_CORE_H
#define _SIDECHAIN_CORE_H

#include "amount.h"
#include "sc/sidechaintypes.h"
#include <primitives/certificate.h>

class CValidationState;
class CTransaction;

namespace Sidechain
{
    static const boost::filesystem::path GetSidechainDataDir();
    bool InitDLogKeys();
    bool InitSidechainsFolder();
    void ClearSidechainsFolder();
    void LoadCumulativeProofsParameters();
};

class CSidechainEvents {
public:
    CSidechainEvents(): sidechainEventsVersion(0), ceasingScs(), maturingScs() {};
    ~CSidechainEvents() = default;

    int32_t sidechainEventsVersion;
    std::set<uint256> ceasingScs;
    std::set<uint256> maturingScs;

    bool IsNull() const {return ceasingScs.empty() && maturingScs.empty();}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(sidechainEventsVersion);
        READWRITE(ceasingScs);
        READWRITE(maturingScs);
    }

    inline bool operator==(const CSidechainEvents& rhs) const {
        return (this->sidechainEventsVersion  == rhs.sidechainEventsVersion) &&
               (this->ceasingScs              == rhs.ceasingScs)             &&
               (this->maturingScs             == rhs.maturingScs);
    }

    inline bool operator!=(const CSidechainEvents& rhs) const { return !(*this == rhs); }

    // Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;
};

class CSidechain {
public:
    CSidechain():
        sidechainVersion(0), creationBlockHeight(-1), creationTxHash(),
        pastEpochTopQualityCertView(), lastTopQualityCertView(), lastTopQualityCertHash(),
        lastTopQualityCertReferencedEpoch(CScCertificate::EPOCH_NULL),
        lastTopQualityCertQuality(CScCertificate::QUALITY_NULL), lastTopQualityCertBwtAmount(0),
        balance(0), maxSizeOfScFeesContainers(-1) {}

    bool IsNull() const
    {
        return (
             creationBlockHeight == -1                                        &&
             creationTxHash.IsNull()                                          &&
             pastEpochTopQualityCertView.IsNull()                             &&
             lastTopQualityCertView.IsNull()                                  &&
             lastTopQualityCertHash.IsNull()                                  &&
             lastTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL  &&
             lastTopQualityCertQuality == CScCertificate::QUALITY_NULL        &&
             lastTopQualityCertBwtAmount == 0                                 &&
             balance == 0                                                     &&
             fixedParams.IsNull()                                             &&
             mImmatureAmounts.empty())                                        &&
             scFees.empty();
    }

    int32_t sidechainVersion;

    // We can not serialize a pointer value to block index, but can retrieve it from chainActive if we have height
    int creationBlockHeight;

    // hash of the tx who created it
    uint256 creationTxHash;

    // Certificate view section
    CScCertificateView pastEpochTopQualityCertView;
    CScCertificateView lastTopQualityCertView;

    // Data for latest top quality cert confirmed in blockchain
    uint256 lastTopQualityCertHash;
    int32_t lastTopQualityCertReferencedEpoch;
    int64_t lastTopQualityCertQuality;
    CAmount lastTopQualityCertBwtAmount;

    // total amount given by sum(fw transfer)-sum(bkw transfer)
    CAmount balance;

    // creation data
    Sidechain::ScFixedParameters fixedParams;

    // immature amounts
    // key   = height at which amount will be considered as mature and will be part of the sc balance
    // value = the immature amount
    std::map<int, CAmount> mImmatureAmounts;

    // memory only
    int maxSizeOfScFeesContainers;
    // the last ftScFee and mbtrScFee values, as set by the active certificates
    // it behaves like a circular buffer once the max size is reached
    std::list<Sidechain::ScFeeData> scFees;

    // compute the max size of the sc fee list
    int getMaxSizeOfScFeesContainers();

    // returns the chain param value with the number of blocks to consider for sc fee check logic
    int getNumBlocksForScFeeCheck();

    enum class State : uint8_t {
        NOT_APPLICABLE = 0,
        UNCONFIRMED,
        ALIVE,
        CEASED
    };

    static std::string stateToString(State s);

    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(sidechainVersion);
        READWRITE(VARINT(creationBlockHeight));
        READWRITE(creationTxHash);
        READWRITE(pastEpochTopQualityCertView);
        READWRITE(lastTopQualityCertView);
        READWRITE(lastTopQualityCertHash);
        READWRITE(lastTopQualityCertReferencedEpoch);
        READWRITE(lastTopQualityCertQuality);
        READWRITE(lastTopQualityCertBwtAmount);
        READWRITE(balance);
        READWRITE(fixedParams);
        READWRITE(mImmatureAmounts);
        READWRITE(scFees);
        if (ser_action.ForRead())
        {
            if (!scFees.empty())
            {
                maxSizeOfScFeesContainers = getMaxSizeOfScFeesContainers();
            }
        }
    }

    inline bool operator==(const CSidechain& rhs) const
    {
        return (this->sidechainVersion                           == rhs.sidechainVersion)                  &&
               (this->creationBlockHeight                        == rhs.creationBlockHeight)               &&
               (this->creationTxHash                             == rhs.creationTxHash)                    &&
               (this->pastEpochTopQualityCertView                == rhs.pastEpochTopQualityCertView)       &&
               (this->lastTopQualityCertView                     == rhs.lastTopQualityCertView)            &&
               (this->lastTopQualityCertHash                     == rhs.lastTopQualityCertHash)            &&
               (this->lastTopQualityCertReferencedEpoch          == rhs.lastTopQualityCertReferencedEpoch) &&
               (this->lastTopQualityCertQuality                  == rhs.lastTopQualityCertQuality)         &&
               (this->lastTopQualityCertBwtAmount                == rhs.lastTopQualityCertBwtAmount)       &&
               (this->balance                                    == rhs.balance)                           &&
               (this->fixedParams                                == rhs.fixedParams)                       &&
               (this->mImmatureAmounts                           == rhs.mImmatureAmounts)                  &&
               (this->scFees                                     == rhs.scFees);
    }
    inline bool operator!=(const CSidechain& rhs) const { return !(*this == rhs); }

    int EpochFor(int targetHeight) const;
    int GetStartHeightForEpoch(int targetEpoch) const;
    int GetEndHeightForEpoch(int targetEpoch) const;
    int GetCertSubmissionWindowStart(int certEpoch) const;
    int GetCertSubmissionWindowEnd(int certEpoch) const;
    int GetCertSubmissionWindowLength() const;
    int GetCertMaturityHeight(int certEpoch) const;
    int GetScheduledCeasingHeight() const;
    bool GetCeasingCumTreeHash(CFieldElement& ceasedBlockCum) const;

    bool isCreationConfirmed() const {
        return this->creationBlockHeight != -1;
    }

    void InitScFees();
    void UpdateScFees(const CScCertificateView& certView);
    void DumpScFees() const;

    CAmount GetMinFtScFee() const;
    CAmount GetMinMbtrScFee() const;

    // Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;
};

namespace Sidechain {
    bool checkCertCustomFields(const CSidechain& sidechain, const CScCertificate& cert);
    bool checkCertSemanticValidity(const CScCertificate& cert, CValidationState& state);
    bool checkTxSemanticValidity(const CTransaction& tx, CValidationState& state);
}; // end of namespace

#endif // _SIDECHAIN_CORE_H

