#include "sc/sidechainrpc.h"
#include "primitives/transaction.h"
#include <boost/foreach.hpp>

#include <rpc/protocol.h>
#include "utilmoneystr.h"
#include "uint256.h"

#include <wallet/wallet.h>

#include <core_io.h>
#include <rpc/server.h>
#include <main.h>
#include <init.h>

extern UniValue ValueFromAmount(const CAmount& amount);
extern CAmount AmountFromValue(const UniValue& value);
extern CFeeRate minRelayTxFee;
extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

namespace Sidechain
{

void AddCeasedSidechainWithdrawalInputsToJSON(const CTransaction& tx, UniValue& parentObj)
{
    UniValue vcsws(UniValue::VARR);
    for (const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("value", ValueFromAmount(csw.nValue)));
        o.push_back(Pair("scId", csw.scId.GetHex()));
        o.push_back(Pair("nullifier", csw.nullifier.GetHexRepr()));

        UniValue spk(UniValue::VOBJ);
        ScriptPubKeyToJSON(csw.scriptPubKey(), spk, true);
        o.push_back(Pair("scriptPubKey", spk));

        o.push_back(Pair("scProof", csw.scProof.GetHexRepr()));

        UniValue rs(UniValue::VOBJ);
        rs.push_back(Pair("asm", csw.redeemScript.ToString()));
        rs.push_back(Pair("hex", HexStr(csw.redeemScript)));
        o.push_back(Pair("redeemScript", rs));
        o.push_back(Pair("actCertDataIdx", (uint64_t)csw.actCertDataIdx));

        vcsws.push_back(o);
    }

    UniValue vactCertData(UniValue::VARR);
    for (const CFieldElement& acd: tx.GetVActCertData())
    {
        vactCertData.push_back(acd.GetHexRepr());
    }

    parentObj.push_back(Pair("vact_cert_data", vactCertData));
    parentObj.push_back(Pair("vcsw_ccin", vcsws));
}

// TODO: naming style is different. Use CamelCase
void AddSidechainOutsToJSON(const CTransaction& tx, UniValue& parentObj)
{
    UniValue vscs(UniValue::VARR);
    // global idx
    unsigned int nIdx = 0;

    for (unsigned int i = 0; i < tx.GetVscCcOut().size(); i++) {
        const CTxScCreationOut& out = tx.GetVscCcOut()[i];
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("scid", out.GetScId().GetHex()));
        o.push_back(Pair("n", (int64_t)nIdx));
        o.push_back(Pair("withdrawal epoch length", (int)out.withdrawalEpochLength));
        o.push_back(Pair("value", ValueFromAmount(out.nValue)));
        o.push_back(Pair("address", out.address.GetHex()));
        o.push_back(Pair("certProvingSystem", Sidechain::ProvingSystemTypeToString(out.certificateProvingSystem)));
        o.push_back(Pair("wCertVk", out.wCertVk.GetHexRepr()));

        UniValue arrFieldElementConfig(UniValue::VARR);
        for(const auto& cfgEntry: out.vFieldElementCertificateFieldConfig)
        {
            arrFieldElementConfig.push_back(cfgEntry.getBitSize());
        }
        o.push_back(Pair("vFieldElementCertificateFieldConfig", arrFieldElementConfig));

        UniValue arrBitVectorConfig(UniValue::VARR);
        for(const auto& cfgEntry: out.vBitVectorCertificateFieldConfig)
        {
            UniValue singlePair(UniValue::VARR);
            singlePair.push_back(cfgEntry.getBitVectorSizeBits());
            singlePair.push_back(cfgEntry.getMaxCompressedSizeBytes());
            arrBitVectorConfig.push_back(singlePair);
        }
        o.push_back(Pair("vBitVectorCertificateFieldConfig", arrBitVectorConfig));

        o.push_back(Pair("customData", HexStr(out.customData)));
        if(out.constant.is_initialized())
            o.push_back(Pair("constant", out.constant->GetHexRepr()));
        if (out.cswProvingSystem != Sidechain::ProvingSystemType::Undefined)
            o.push_back(Pair("cswProvingSystem", Sidechain::ProvingSystemTypeToString(out.cswProvingSystem)));
        if(out.wCeasedVk.is_initialized())
            o.push_back(Pair("wCeasedVk", out.wCeasedVk.get().GetHexRepr()));
        o.push_back(Pair("ftScFee", ValueFromAmount(out.forwardTransferScFee)));
        o.push_back(Pair("mbtrScFee", ValueFromAmount(out.mainchainBackwardTransferRequestScFee)));
        o.push_back(Pair("mbtrRequestDataLength", out.mainchainBackwardTransferRequestDataLength));
        vscs.push_back(o);
        nIdx++;
    }
    parentObj.push_back(Pair("vsc_ccout", vscs));

    UniValue vfts(UniValue::VARR);
    for (unsigned int i = 0; i < tx.GetVftCcOut().size(); i++) {
        const CTxForwardTransferOut& out = tx.GetVftCcOut()[i];
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("scid", out.scId.GetHex()));
        o.push_back(Pair("n", (int64_t)nIdx));
        o.push_back(Pair("value", ValueFromAmount(out.nValue)));
        o.push_back(Pair("address", out.address.GetHex()));
        vfts.push_back(o);
        nIdx++;
    }
    parentObj.push_back(Pair("vft_ccout", vfts));

    UniValue vbts(UniValue::VARR);
    for (unsigned int i = 0; i < tx.GetVBwtRequestOut().size(); i++) {
        const CBwtRequestOut& out = tx.GetVBwtRequestOut()[i];
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("scid", out.GetScId().GetHex()));
        o.push_back(Pair("n", (int64_t)nIdx));

        std::string taddrStr = "Invalid taddress";
        uint160 pkeyValue;
        pkeyValue.SetHex(out.mcDestinationAddress.GetHex());

        CKeyID keyID(pkeyValue);
        CBitcoinAddress taddr(keyID);
        if (taddr.IsValid()) {
            taddrStr = taddr.ToString();
        }

        UniValue mcAddr(UniValue::VOBJ);
        mcAddr.push_back(Pair("pubkeyhash", out.mcDestinationAddress.GetHex()));
        mcAddr.push_back(Pair("taddr", taddrStr));
        
        o.push_back(Pair("mcDestinationAddress", mcAddr));
        o.push_back(Pair("scFee", ValueFromAmount(out.GetScValue())));

        UniValue arrRequestData(UniValue::VARR);
        for(const auto& requestData: out.vScRequestData)
        {
            arrRequestData.push_back(requestData.GetHexRepr());
        }
        o.push_back(Pair("vScRequestData", arrRequestData));
        vbts.push_back(o);
        nIdx++;
    }
    parentObj.push_back(Pair("vmbtr_out", vbts));
}

bool AddCustomFieldElement(const std::string& inputString, std::vector<unsigned char>& vBytes,
    unsigned int nBytes, std::string& errString)
{
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
    {
        errString = std::string("Invalid format: not an hex");
        return false;
    }

    unsigned int dataLen = inputString.length();

    if (dataLen%2)
    {
        errString = strprintf("Invalid length %d, must be even (byte string)", dataLen);
        return false;
    }

    unsigned int scDataLen = dataLen/2;

    if(scDataLen > nBytes)
    {
        errString = strprintf("Invalid length %d, must be %d bytes at most", scDataLen, nBytes);
        return false;
    }

    vBytes = ParseHex(inputString);
    assert(vBytes.size() == scDataLen);

    return true;
}

bool AddScData(const std::string& inputString, std::vector<unsigned char>& vBytes, unsigned int vSize, bool enforceStrictSize, std::string& error)
{ 
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
    {
        error = std::string("Invalid format: not an hex");
        return false;
    }

    unsigned int dataLen = inputString.length();

    if (dataLen%2)
    {
        error = strprintf("Invalid length %d, must be even (byte string)", dataLen);
        return false;
    }

    unsigned int scDataLen = dataLen/2;

    if(enforceStrictSize && (scDataLen != vSize))
    {
        error = strprintf("Invalid length %d, must be %d bytes", scDataLen, vSize);
        return false;
    }

    if (!enforceStrictSize && (scDataLen > vSize))
    {
        error = strprintf("Invalid length %d, must be %d bytes at most", scDataLen, vSize);
        return false;
    }

    vBytes = ParseHex(inputString);
    assert(vBytes.size() == scDataLen);

    return true;
}

template <typename T>
bool AddScData(const UniValue& intArray, std::vector<T>& vCfg)
{ 
    if (intArray.size() != 0)
    {
        for (const UniValue& o : intArray.getValues())
        {
            if (!o.isNum())
            {
                return false;
            }
            vCfg.push_back(o.get_int());
        }
    }
    return true;
}

bool AddCeasedSidechainWithdrawalInputs(UniValue &csws, CMutableTransaction &rawTx, std::string &error)
{
    rawTx.nVersion = SC_TX_VERSION;
    std::set<CFieldElement> sActCertData;

    for (size_t i = 0; i < csws.size(); i++)
    {
        const UniValue& input = csws[i];
        const UniValue& o = input.get_obj();

        // parse amount
        const UniValue& amount_v = find_value(o, "amount");
        if (amount_v.isNull())
        {
            error = "Missing mandatory parameter \"amount\" for the ceased sidechain withdrawal input";
            return false;
        }
        CAmount amount = AmountFromValue(amount_v);
        if (amount < 0)
        {
            error = "Invalid ceased sidechain withdrawal input parameter: \"amount\" must be positive";
            return false;
        }

        // parse sender address and get public key hash
        const UniValue& sender_v = find_value(o, "senderAddress");
        if (sender_v.isNull())
        {
            error = "Missing mandatory parameter \"senderAddress\" for the ceased sidechain withdrawal input";
            return false;
        }
        CBitcoinAddress senderAddress(sender_v.get_str());
        if (!senderAddress.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input \"senderAddress\" parameter";
            return false;
        }

        CKeyID pubKeyHash;
        if(!senderAddress.GetKeyID(pubKeyHash))
        {
            error = "Invalid ceased sidechain withdrawal input \"senderAddress\": Horizen pubKey address expected.";
            return false;
        }

        // parse sidechain id
        const UniValue& scid_v = find_value(o, "scId");
        if (scid_v.isNull())
        {
            error = "Missing mandatory parameter \"scId\" for the ceased sidechain withdrawal input";
            return false;
        }
        std::string scIdString = scid_v.get_str();
        if (scIdString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid ceased sidechain withdrawal input \"scId\" format: not an hex";
            return false;
        }

        uint256 scId;
        scId.SetHex(scIdString);

        // parse nullifier
        const UniValue& nullifier_v = find_value(o, "nullifier");
        if (nullifier_v.isNull())
        {
            error = "Missing mandatory parameter \"nullifier\" for the ceased sidechain withdrawal input";
            return false;
        }

        std::string nullifierError;
        std::vector<unsigned char> nullifierVec;
        if (!AddScData(nullifier_v.get_str(), nullifierVec, CFieldElement::ByteSize(), true, nullifierError))
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"nullifier\": " + nullifierError;
            return false;
        }

        CFieldElement nullifier {nullifierVec};
        if (!nullifier.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"nullifier\": invalid nullifier data";
            return false;
        }
//---------------------------------------------------------------------------------------------
        // parse active cert data (do not check it though)
        const UniValue& valActCertData = find_value(o, "activeCertData");
        if (valActCertData.isNull())
        {
            error = "Missing mandatory parameter \"activeCertData\" for the ceased sidechain withdrawal input";
            return false;
        }

        std::string errStr;
        std::vector<unsigned char> vActCertData;
        if (!AddScData(valActCertData.get_str(), vActCertData, CFieldElement::ByteSize(), true, errStr))
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"activeCertData\": " + errStr;
            return false;
        }

        CFieldElement actCertData {vActCertData};
        if (!actCertData.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"activeCertData\": invalid field element";
            return false;
        }

        if (!sActCertData.count(actCertData))
        {
            rawTx.add(actCertData);
            LogPrint("sc", "%s():%d - added actCertData[%s]\n", __func__, __LINE__, actCertData.GetHexRepr());
            sActCertData.insert(actCertData);
        }
        int idx = rawTx.GetIndexOfActCertData(actCertData);
        assert(idx >= 0);

//---------------------------------------------------------------------------------------------
        // parse snark proof
        const UniValue& proof_v = find_value(o, "scProof");
        if (proof_v.isNull())
        {
            error = "Missing mandatory parameter \"scProof\" for the ceased sidechain withdrawal input";
            return false;
        }

        std::string proofError;
        std::vector<unsigned char> scProofVec;
        if (!AddScData(proof_v.get_str(), scProofVec, CScProof::ByteSize(), true, proofError))
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"scProof\": " + proofError;
            return false;
        }

        CScProof scProof {scProofVec};
        if (!scProof.IsValid())
        {
            error = "Invalid ceased sidechain withdrawal input parameter \"scProof\": invalid snark proof data";
            return false;
        }

        CTxCeasedSidechainWithdrawalInput csw_input(amount, scId, nullifier, pubKeyHash, scProof, CScript(), idx);
        rawTx.vcsw_ccin.push_back(csw_input);
    }

    return true;
}

bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t i = 0; i < sc_crs.size(); i++)
    {
        ScFixedParameters sc;

        const UniValue& input = sc_crs[i];
        const UniValue& o = input.get_obj();

        const UniValue& sh_v = find_value(o, "epoch_length");
        if (sh_v.isNull() || !sh_v.isNum())
        {
            error = "Invalid parameter or missing epoch_length key";
            return false;
        }
        int el = sh_v.get_int();
        if (el < 0)
        {
            error = "Invalid parameter, epoch_length must be positive";
            return false;
        }

        sc.withdrawalEpochLength = el;

        const UniValue& av = find_value(o, "amount");
        if (av.isNull())
        {
            error = "Missing mandatory parameter amount";
            return false;
        }
        CAmount nAmount = AmountFromValue( av );
        if (nAmount < 0)
        {
            error = "Invalid parameter, amount must be positive";
            return false;
        }

        const UniValue& adv = find_value(o, "address");
        if (adv.isNull())
        {
            error = "Missing mandatory parameter address";
            return false;
        }

        const std::string& inputString = adv.get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid address format: not an hex";
            return false;
        }

        uint256 address;
        address.SetHex(inputString);

        const UniValue& certProvingSystemValue = find_value(o, "certProvingSystem");
        if (!certProvingSystemValue.isStr())
        {
            error = "Invalid parameter or missing certProvingSystem key";
            return false;
        }
        sc.certificateProvingSystem = Sidechain::StringToProvingSystemType(certProvingSystemValue.get_str());
        if (!Sidechain::IsValidProvingSystemType(sc.certificateProvingSystem))
        {
            // if the key is specified we accept only defined values
            error = "Invalid parameter certProvingSystem";
            return false;
        }

        const UniValue& wCertVk = find_value(o, "wCertVk");
        if (wCertVk.isNull())
        {
            error = "Missing mandatory parameter wCertVk";
            return false;
        }
        else
        {
            const std::string& inputString = wCertVk.get_str();
            std::vector<unsigned char> wCertVkVec;
            if (!AddScData(inputString, wCertVkVec, CScVKey::ByteSize(), true, error))
            {
                error = "wCertVk: " + error;
                return false;
            }

            sc.wCertVk = CScVKey(wCertVkVec);

            if (!sc.wCertVk.IsValid())
            {
                error = "invalid wCertVk";
                return false;
            }
        }
        
        const UniValue& cd = find_value(o, "customData");
        if (!cd.isNull())
        {
            const std::string& inputString = cd.get_str();
            if (!AddScData(inputString, sc.customData, MAX_SC_CUSTOM_DATA_LEN, false, error))
            {
                error = "customData: " + error;
                return false;
            }
        }

        const UniValue& constant = find_value(o, "constant");
        if (!constant.isNull())
        {
            const std::string& inputString = constant.get_str();
            std::vector<unsigned char> scConstantByteArray {};
            if (!AddScData(inputString, scConstantByteArray, CFieldElement::ByteSize(), false, error))
            {
                error = "constant: " + error;
                return false;
            }

            sc.constant = CFieldElement{scConstantByteArray};
            if (!sc.constant->IsValid())
            {
                error = "invalid constant";
                return false;
            }
        }

        const UniValue& cswProvingSystemValue = find_value(o, "cswProvingSystem");
        if (!cswProvingSystemValue.isNull())
        {
            if (!cswProvingSystemValue.isStr())
            {
                error = "Invalid parameter or missing cswProvingSystem key";
                return false;
            }
            sc.cswProvingSystem = Sidechain::StringToProvingSystemType(cswProvingSystemValue.get_str());
            if (!Sidechain::IsValidProvingSystemType(sc.cswProvingSystem))
            {
                // if the key is specified we accept only defined values
                error = "Invalid parameter cswProvingSystem";
                return false;
            }
        }

        const UniValue& wCeasedVk = find_value(o, "wCeasedVk");
        if (!wCeasedVk.isNull())
        {
            const std::string& inputString = wCeasedVk.get_str();

            if (!inputString.empty())
            {
                if (sc.cswProvingSystem == Sidechain::ProvingSystemType::Undefined)
                {
                    error = "cswProvingSystem must be defined if a wCeasedVk is provided";
                    return false;
                }

                std::vector<unsigned char> wCeasedVkVec;
                if (!AddScData(inputString, wCeasedVkVec, CScVKey::ByteSize(), true, error))
                {
                    error = "wCeasedVk: " + error;
                    return false;
                }

                sc.wCeasedVk = CScVKey(wCeasedVkVec);
                if (!sc.wCeasedVk.get().IsValid())
                {
                    error = "invalid wCeasedVk";
                    return false;
                }
            }
        }
        else
        {
            // if csw vk is null we should not have csw proving sys option set
            if (Sidechain::IsValidProvingSystemType(sc.cswProvingSystem))
            {
                error = "cswProvingSystem should not be defined if a wCeasedVk is null";
                return false;
            }
        }

        const UniValue& FeCfg = find_value(o, "vFieldElementCertificateFieldConfig");
        if (!FeCfg.isNull())
        {
            UniValue intArray = FeCfg.get_array();
            if (!Sidechain::AddScData(intArray, sc.vFieldElementCertificateFieldConfig))
            {
                error = "invalid vFieldElementCertificateFieldConfig";
                return false;
            }
        }

        const UniValue& CmtCfg = find_value(o, "vBitVectorCertificateFieldConfig");
        if (!CmtCfg.isNull())
        {
            UniValue BitVectorSizesPairArray = CmtCfg.get_array();
            for(auto& pairEntry: BitVectorSizesPairArray.getValues())
            {
                if (pairEntry.size() != 2) {
                    error = "invalid vBitVectorCertificateFieldConfig";
                    return false;
                }
                if (!pairEntry[0].isNum() || !pairEntry[1].isNum())
                {
                    error = "invalid vBitVectorCertificateFieldConfig";
                    return false;
                }

                sc.vBitVectorCertificateFieldConfig.push_back(BitVectorCertificateFieldConfig{pairEntry[0].get_int(), pairEntry[1].get_int()});
            }
        }

        CAmount ftScFee(0);
        const UniValue& uniFtScFee = find_value(o, "forwardTransferScFee");
        if (!uniFtScFee.isNull())
        {
            ftScFee = AmountFromValue(uniFtScFee);

            if (!MoneyRange(ftScFee))
            {
                error = strprintf("Invalid forwardTransferScFee: out of range [%d, %d]", 0, MAX_MONEY);
                return false;
            }
        }

        CAmount mbtrScFee(0);
        const UniValue& uniMbtrScFee = find_value(o, "mainchainBackwardTransferScFee");
        if (!uniMbtrScFee.isNull())
        {
            mbtrScFee = AmountFromValue(uniMbtrScFee);

            if (!MoneyRange(mbtrScFee))
            {
                error = strprintf("Invalid mainchainBackwardTransferScFee: out of range [%d, %d]", 0, MAX_MONEY);
                return false;
            }
        }

        int32_t mbtrDataLength = 0;
        const UniValue& uniMbtrDataLength = find_value(o, "mainchainBackwardTransferRequestDataLength");
        if (!uniMbtrDataLength.isNull())
        {
            if (!uniMbtrDataLength.isNum())
            {
                error = "Invalid mainchainBackwardTransferRequestDataLength: numeric value expected";
                return false;
            }
            
            mbtrDataLength = uniMbtrDataLength.get_int();

            if (mbtrDataLength < 0 || mbtrDataLength > MAX_SC_MBTR_DATA_LEN)
            {
                error = strprintf("Invalid mainchainBackwardTransferRequestDataLength: out of range [%d, %d]", 0, MAX_SC_MBTR_DATA_LEN);
                return false;
            }
        }
        sc.mainchainBackwardTransferRequestDataLength = mbtrDataLength;

        CTxScCreationOut txccout(nAmount, address, ftScFee, mbtrScFee, sc);

        rawTx.vsc_ccout.push_back(txccout);
    }

    return true;
}

bool AddSidechainForwardOutputs(UniValue& fwdtr, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t j = 0; j < fwdtr.size(); j++)
    {
        const UniValue& input = fwdtr[j];
        const UniValue& o = input.get_obj();

        std::string inputString = find_value(o, "scid").get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid scid format: not an hex";
            return false;
        }

        uint256 scId;
        scId.SetHex(inputString);

        const UniValue& av = find_value(o, "amount");
        CAmount nAmount = AmountFromValue( av );
        if (nAmount < 0)
        {
            error = "Invalid parameter, amount must be positive";
            return false;
        }

        inputString = find_value(o, "address").get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid address format: not an hex";
            return false;
        }

        uint256 address;
        address.SetHex(inputString);

        CTxForwardTransferOut txccout(scId, nAmount, address);
        rawTx.vft_ccout.push_back(txccout);
    }

    return true;
}

bool AddSidechainBwtRequestOutputs(UniValue& bwtreq, CMutableTransaction& rawTx, std::string& error)
{
    rawTx.nVersion = SC_TX_VERSION;

    for (size_t j = 0; j < bwtreq.size(); j++)
    {
        ScBwtRequestParameters bwtData;

        const UniValue& input = bwtreq[j];
        const UniValue& o = input.get_obj();

        //---------------------------------------------------------------------
        const UniValue& scidVal = find_value(o, "scid");
        if (scidVal.isNull())
        {
            error = "Missing mandatory parameter scid";
            return false;
        }
        std::string inputString = scidVal.get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid scid format: not an hex";
            return false;
        }

        uint256 scId;
        scId.SetHex(inputString);

        //---------------------------------------------------------------------
        const UniValue& pkhVal = find_value(o, "pubkeyhash");
        if (pkhVal.isNull())
        {
            error = "Missing mandatory parameter pubkeyhash";
            return false;
        }
        inputString = pkhVal.get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        {
            error = "Invalid pubkeyhash format: not an hex";
            return false;
        }

        uint160 pkh;
        pkh.SetHex(inputString);

        //---------------------------------------------------------------------
        const UniValue& scFeeVal = find_value(o, "scFee");
        CAmount scFee = AmountFromValue( scFeeVal );
        if (scFee < 0)
        {
            error = "Invalid parameter, amount must be positive";
            return false;
        }
        bwtData.scFee = scFee;

        //---------------------------------------------------------------------
        const UniValue& vScRequestDataVal = find_value(o, "vScRequestData");
        if (vScRequestDataVal.isNull())
        {
            error = "Missing mandatory parameter vScRequestData";
            return false;
        }


        for (UniValue inputElement : vScRequestDataVal.get_array().getValues())
        {
            std::vector<unsigned char> requestDataByteArray {};

            if (!Sidechain::AddScData(inputElement.get_str(), requestDataByteArray, CFieldElement::ByteSize(), true, error))
            {
                throw JSONRPCError(RPC_TYPE_ERROR, std::string("requestDataByte: ") + error);
            }

            bwtData.vScRequestData.push_back(CFieldElement{requestDataByteArray});
        }


        CBwtRequestOut txccout(scId, pkh, bwtData);
        rawTx.vmbtr_out.push_back(txccout);
    }

    return true;
}

void fundCcRecipients(const CTransaction& tx,
    std::vector<CRecipientScCreation >& vecScSend, std::vector<CRecipientForwardTransfer >& vecFtSend,
    std::vector<CRecipientBwtRequest>& vecBwtRequest)
{
    BOOST_FOREACH(const auto& entry, tx.GetVscCcOut())
    {
        CRecipientScCreation sc;
        sc.nValue = entry.nValue;
        sc.address = entry.address;
        sc.fixedParams.withdrawalEpochLength               = entry.withdrawalEpochLength;
        sc.fixedParams.wCertVk                             = entry.wCertVk;
        sc.fixedParams.wCeasedVk                           = entry.wCeasedVk;
        sc.fixedParams.vFieldElementCertificateFieldConfig = entry.vFieldElementCertificateFieldConfig;
        sc.fixedParams.vBitVectorCertificateFieldConfig    = entry.vBitVectorCertificateFieldConfig;
        sc.fixedParams.customData                          = entry.customData;
        sc.fixedParams.constant                            = entry.constant;

        vecScSend.push_back(sc);
    }

    BOOST_FOREACH(const auto& entry, tx.GetVftCcOut())
    {
        CRecipientForwardTransfer ft;
        ft.scId = entry.scId;
        ft.address = entry.address;
        ft.nValue = entry.nValue;

        vecFtSend.push_back(ft);
    }

    BOOST_FOREACH(const auto& entry, tx.GetVBwtRequestOut())
    {
        CRecipientBwtRequest bt;
        bt.scId = entry.scId;
        bt.mcDestinationAddress = entry.mcDestinationAddress;
        bt.bwtRequestData.scFee = entry.scFee;
        bt.bwtRequestData.vScRequestData = entry.vScRequestData;

        vecBwtRequest.push_back(bt);
    }
}

//--------------------------------------------------------------------------------------------
// Cross chain outputs

ScRpcCmd::ScRpcCmd(
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee): 
        _fromMcAddress(fromaddress), _changeMcAddress(changeaddress), _minConf(minConf), _fee(nFee)
{
    _totalOutputAmount = 0;

    _hasFromAddress   = !(_fromMcAddress   == CBitcoinAddress());
    _hasChangeAddress = !(_changeMcAddress == CBitcoinAddress());

    // Get dust threshold
    CKey secret;
    secret.MakeNewKey(true);
    CScript scriptPubKey = GetScriptForDestination(secret.GetPubKey().GetID());
    CTxOut out(CAmount(1), scriptPubKey);
    _dustThreshold = out.GetDustThreshold(minRelayTxFee);

    _totalInputAmount = 0;
}

void ScRpcCmd::addInputs()
{
    std::vector<COutput> vAvailableCoins;
    std::vector<SelectedUTXO> vInputUtxo;

    static const bool fOnlyConfirmed = false;
    static const bool fIncludeZeroValue = false;
    bool fProtectCoinbase = !Params().GetConsensus().fCoinbaseMustBeProtected;
    static const bool fIncludeCoinBase = fProtectCoinbase;
    static const bool fIncludeCommunityFund = fProtectCoinbase;

    pwalletMain->AvailableCoins(vAvailableCoins, fOnlyConfirmed, NULL, fIncludeZeroValue, fIncludeCoinBase, fIncludeCommunityFund);

    for (const auto& out: vAvailableCoins)
    {
        LogPrint("sc", "utxo %s depth: %5d, val: %12s, spendable: %s\n",
            out.tx->getTxBase()->GetHash().ToString(), out.nDepth, FormatMoney(out.tx->getTxBase()->GetVout()[out.pos].nValue), out.fSpendable?"Y":"N");

        if (!out.fSpendable || out.nDepth < _minConf) {
            continue;
        }

        if (_hasFromAddress)
        {
            CTxDestination dest;
            if (!ExtractDestination(out.tx->getTxBase()->GetVout()[out.pos].scriptPubKey, dest)) {
                continue;
            }

            if (!(CBitcoinAddress(dest) == _fromMcAddress)) {
                continue;
            }
        }

        CAmount nValue = out.tx->getTxBase()->GetVout()[out.pos].nValue;

        SelectedUTXO utxo(out.tx->getTxBase()->GetHash(), out.pos, nValue);
        vInputUtxo.push_back(utxo);
    }

    // sort in ascending order, so smaller utxos appear first
    std::sort(vInputUtxo.begin(), vInputUtxo.end(), [](SelectedUTXO i, SelectedUTXO j) -> bool {
        return ( std::get<2>(i) < std::get<2>(j));
    });

    CAmount targetAmount = _totalOutputAmount + _fee;

    CAmount dustChange = -1;

    std::vector<SelectedUTXO> vSelectedInputUTXO;

    for (const SelectedUTXO & t : vInputUtxo)
    {
        _totalInputAmount += std::get<2>(t);
        vSelectedInputUTXO.push_back(t);

        LogPrint("sc", "---> added tx %s val: %12s, vout.n: %d\n",
            std::get<0>(t).ToString(), FormatMoney(std::get<2>(t)), std::get<1>(t));

        if (_totalInputAmount >= targetAmount)
        {
            // Select another utxo if there is change less than the dust threshold.
            dustChange = _totalInputAmount - targetAmount;
            if (dustChange == 0 || dustChange >= _dustThreshold) {
                break;
            }
        }
    }

    if (_totalInputAmount < targetAmount)
    {
        std::string addrDetails;
        if (_hasFromAddress)
            addrDetails = strprintf(" for taddr[%s]", _fromMcAddress.ToString());

        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient transparent funds %s, have %s, need %s (minconf=%d)",
            addrDetails, FormatMoney(_totalInputAmount), FormatMoney(targetAmount), _minConf));
    }

    // If there is transparent change, is it valid or is it dust?
    if (dustChange < _dustThreshold && dustChange != 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient transparent funds, have %s, need %s more to avoid creating invalid change output %s (dust threshold is %s)",
            FormatMoney(_totalInputAmount), FormatMoney(_dustThreshold - dustChange), FormatMoney(dustChange), FormatMoney(_dustThreshold)));
    }

    // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
    size_t limit = (size_t)GetArg("-mempooltxinputlimit", 0);
    if (limit > 0)
    {
        size_t n = vSelectedInputUTXO.size();
        if (n > limit) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Too many transparent inputs %zu > limit %zu", n, limit));
        }
    }

    // update the transaction with these inputs
    for (const auto& t : vSelectedInputUTXO)
    {
        uint256 txid = std::get<0>(t);
        int vout = std::get<1>(t);

        CTxIn in(COutPoint(txid, vout));
        addInput(in);
    }
}

void ScRpcCmd::addChange()
{
    CAmount change = _totalInputAmount - ( _totalOutputAmount + _fee);

    if (change > 0)
    {
        // handle the address for the change
        CScript scriptPubKey;
        if (_hasChangeAddress)
        {
            scriptPubKey = GetScriptForDestination(_changeMcAddress.Get());
        }
        else
        if (_hasFromAddress)
        {
            scriptPubKey = GetScriptForDestination(_fromMcAddress.Get());
        }
        else
        {
            CReserveKey keyChange(pwalletMain);
            CPubKey vchPubKey;

            if (!keyChange.GetReservedKey(vchPubKey))
                throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Could not generate a taddr to use as a change address"); // should never fail, as we just unlocked

            scriptPubKey = GetScriptForDestination(vchPubKey.GetID());
        }

        addOutput(CTxOut(change, scriptPubKey));
    }
}

ScRpcCmdCert::ScRpcCmdCert(
        CMutableScCertificate& cert, const std::vector<sBwdParams>& bwdParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress, int minConf, const CAmount& nFee,
        const std::vector<FieldElementCertificateField>& vCfe, const std::vector<BitVectorCertificateField>& vCmt,
        const CAmount& ftScFee, const CAmount& mbtrScFee):
        ScRpcCmd(fromaddress, changeaddress, minConf, nFee),
        _cert(cert),_bwdParams(bwdParams), _vCfe(vCfe), _vCmt(vCmt),
        _ftScFee(ftScFee), _mbtrScFee(mbtrScFee)
{
}

void ScRpcCmdCert::execute()
{
    addInputs();
    addChange();
    addBackwardTransfers();
    addCustomFields();
    addScFees();
    sign();
    send();
}

void ScRpcCmdCert::sign()
{
    std::string rawcert;
    try
    {
        CScCertificate toEncode(_cert);
        rawcert = EncodeHexCert(toEncode);
        LogPrint("sc", "      toEncode[%s]\n", toEncode.GetHash().ToString());
        LogPrint("sc", "      toEncode: %s\n", toEncode.ToString());
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to encode certificate");
    }

    UniValue val = UniValue(UniValue::VARR);
    val.push_back(rawcert);

    UniValue signResultValue = signrawcertificate(val, false);

    UniValue signResultObject = signResultValue.get_obj();

    UniValue completeValue = find_value(signResultObject, "complete");
    if (!completeValue.get_bool())
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
    }

    UniValue hexValue = find_value(signResultObject, "hex");
    if (hexValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
    }
    _signedObjHex = hexValue.get_str();

    CMutableScCertificate certStreamed;
    try
    {
        // Keep the signed certificate so we can hash to the same certid
        CDataStream stream(ParseHex(_signedObjHex), SER_NETWORK, PROTOCOL_VERSION);
        stream >> certStreamed;
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to parse certificate");
    }
    _cert = certStreamed;
}

void ScRpcCmdCert::send()
{
    UniValue val = UniValue(UniValue::VARR);
    val.push_back(_signedObjHex);

    UniValue sendResultValue = sendrawcertificate(val, false);
    if (sendResultValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Send raw transaction did not return an error or a txid.");
    }
    LogPrint("sc", "cert sent[%s]\n", sendResultValue.get_str());
}

void ScRpcCmdCert::addBackwardTransfers()
{
    for (const auto& entry : _bwdParams)
    {
        CTxOut txout(entry._nAmount, entry._scriptPubKey);
        _cert.addBwt(txout);
    }
}

void ScRpcCmdCert::addCustomFields()
{
    if (!_vCfe.empty())
        _cert.vFieldElementCertificateField = _vCfe;
    if (!_vCmt.empty())
        _cert.vBitVectorCertificateField = _vCmt;
}

void ScRpcCmdCert::addScFees()
{
    _cert.forwardTransferScFee = _ftScFee;
    _cert.mainchainBackwardTransferRequestScFee = _mbtrScFee;
}

ScRpcCmdTx::ScRpcCmdTx(
        CMutableTransaction& tx,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmd(fromaddress, changeaddress, minConf, nFee), _tx(tx)
{
}

void ScRpcCmdTx::sign()
{
    std::string rawtxn;
    try
    {
        rawtxn = EncodeHexTx(_tx);
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to encode transaction");
    }

    UniValue val = UniValue(UniValue::VARR);
    val.push_back(rawtxn);

    UniValue signResultValue = signrawtransaction(val, false);

    UniValue signResultObject = signResultValue.get_obj();

    UniValue completeValue = find_value(signResultObject, "complete");
    if (!completeValue.get_bool())
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
    }

    UniValue hexValue = find_value(signResultObject, "hex");
    if (hexValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
    }
    _signedObjHex = hexValue.get_str();

    CMutableTransaction txStreamed;
    try
    {
        // Keep the signed transaction so we can hash to the same txid
        CDataStream stream(ParseHex(_signedObjHex), SER_NETWORK, PROTOCOL_VERSION);
        stream >> txStreamed;
    }
    catch(...)
    {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to parse transaction");
    }
    _tx = txStreamed;
}

void ScRpcCmdTx::send()
{
    UniValue val = UniValue(UniValue::VARR);
    val.push_back(_signedObjHex);

    UniValue sendResultValue = sendrawtransaction(val, false);
    if (sendResultValue.isNull())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Send raw transaction did not return an error or a txid.");
    }
}

void ScRpcCmdTx::execute()
{
    addInputs();
    addChange();
    addCcOutputs();
    sign();
    send();
}

ScRpcCreationCmdTx::ScRpcCreationCmdTx(
        CMutableTransaction& tx, const std::vector<sCrOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee, const CAmount& ftScFee, const CAmount& mbtrScFee,
        const ScFixedParameters& cd):
        ScRpcCmdTx(tx, fromaddress, changeaddress, minConf, nFee), _fixedParams(cd), _outParams(outParams), _ftScFee(ftScFee), _mbtrScFee(mbtrScFee)
{
    for (const auto& entry : _outParams)
    {
        _totalOutputAmount += entry._nAmount;
    }
} 

void ScRpcCreationCmdTx::addCcOutputs()
{
    if (_outParams.size() != 1)
    {
        // creation has just one output param
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("invalid number of output: %d!", _outParams.size()));
    }

    CTxScCreationOut txccout(_outParams[0]._nAmount, _outParams[0]._toScAddress, _ftScFee, _mbtrScFee, _fixedParams);
    _tx.add(txccout);
}

ScRpcSendCmdTx::ScRpcSendCmdTx(
        CMutableTransaction& tx, const std::vector<sFtOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmdTx(tx, fromaddress, changeaddress, minConf, nFee), _outParams(outParams)
{
    for (const auto& entry : _outParams)
    {
        _totalOutputAmount += entry._nAmount;
    }
} 


void ScRpcSendCmdTx::addCcOutputs()
{
    if (_outParams.size() == 0)
    {
        // send cmd can not have empty output vector
        throw JSONRPCError(RPC_WALLET_ERROR, "null number of output!");
    }

    for (const auto& entry : _outParams)
    {
        CTxForwardTransferOut txccout(entry._scid, entry._nAmount, entry._toScAddress);
        _tx.add(txccout);
    }
}

ScRpcRetrieveCmdTx::ScRpcRetrieveCmdTx(
        CMutableTransaction& tx, const std::vector<sBtOutParams>& outParams,
        const CBitcoinAddress& fromaddress, const CBitcoinAddress& changeaddress,
        int minConf, const CAmount& nFee):
        ScRpcCmdTx(tx, fromaddress, changeaddress, minConf, nFee), _outParams(outParams)
{
    for (const auto& entry : _outParams)
    {
        _totalOutputAmount += entry._params.scFee;
    }
} 


void ScRpcRetrieveCmdTx::addCcOutputs()
{
    if (_outParams.size() == 0)
    {
        // send cmd can not have empty output vector
        throw JSONRPCError(RPC_WALLET_ERROR, "null number of output!");
    }

    for (const auto& entry : _outParams)
    {
        CBwtRequestOut txccout(entry._scid, entry._pkh, entry._params);
        _tx.add(txccout);
    }
}

// explicit instantiations
template bool AddScData<FieldElementCertificateFieldConfig>(const UniValue& intArray, std::vector<FieldElementCertificateFieldConfig>& vCfg);
}  // end of namespace
