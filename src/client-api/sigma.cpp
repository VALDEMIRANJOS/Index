// Copyright (c) 2018 Tadhg Riordan Zcoin Developer
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "client-api/server.h"
#include "rpc/server.h"
#include "util.h"
#include "client-api/wallet.h"
#include "wallet/wallet.h"
#include "wallet/walletexcept.h"
#include "base58.h"
#include "client-api/send.h"
#include "client-api/protocol.h"
#include <zerocoin.h>
#include <sigma.h>
#include <vector>

#include "univalue.h"

using namespace std;

UniValue mintstatus(Type type, const UniValue& data, const UniValue& auth, bool fHelp){
    return data;
}

UniValue mint(Type type, const UniValue& data, const UniValue& auth, bool fHelp)
{
    // Ensure Sigma mints is already accepted by network so users will not lost their coins
    // due to other nodes will treat it as garbage data.
    if (!sigma::IsSigmaAllowed()) {
        throw JSONAPIError(API_WALLET_ERROR, "Sigma is not activated yet");
    }

    sigma::Params* sigmaParams = sigma::Params::get_default();

    vector<CRecipient> vecSend;
    vector<sigma::PrivateCoin> privCoins;
    CWalletTx wtx;
    vector<CHDMint> vHdMints;

    UniValue sendTo = data[0].get_obj();
    sigma::CoinDenomination denomination;

    vector<string> keys = sendTo.getKeys();
    BOOST_FOREACH(const string& denominationStr, keys){
        if (!StringToDenomination(denominationStr, denomination)) {
            throw runtime_error(
                "mint <amount>(0.1, 0.5, 1, 10, 25, 100) (\"zcoinaddress\")\n");
        }
        int64_t coinValue;
        DenominationToInteger(denomination, coinValue);
        int64_t numberOfCoins = sendTo[denominationStr].get_int();

        LogPrintf("mint: denomination = %s, nAmount = %s \n",
            denominationStr, numberOfCoins);

        if(numberOfCoins < 0) {
            throw runtime_error(
                    "mint {<denomination>(0.1, 0.5, 1, 10, 25, 100):\"amount\"...}\n");
        }

        for(int64_t i = 0; i < numberOfCoins; ++i) {
            // The following constructor does all the work of minting a brand
            // new sigma mint. It stores all the private values inside the
            // PrivateCoin object. This includes the coin secrets, which must be
            // stored in a secure location (wallet) at the client.
            sigma::PrivateCoin newCoin(sigmaParams, denomination, ZEROCOIN_TX_VERSION_3);
            // Get a copy of the 'public' portion of the coin. You should
            // embed this into a Sigma 'MINT' transaction along with a series
            // of currency inputs totaling the assigned value of one sigma mint.

            // Generate and store secrets deterministically in the following function.
            CHDMint fHdMint;
            zwalletMain->GenerateMint(newCoin.getPublicCoin().getDenomination(), newCoin, fHdMint);

            sigma::PublicCoin pubCoin = newCoin.getPublicCoin();

            // Create script for coin
            CScript scriptSerializedCoin;
            // opcode is inserted as 1 byte according to file script/script.h
            scriptSerializedCoin << OP_SIGMAMINT;

            // MARTUN: Commenting this for now.
            // this one will probably be written as int64_t, which means it will be written in as few bytes as necessary, and one more byte for sign. In our case our 34 will take 2 bytes, 1 for the number 34 and another one for the sign.
            // scriptSerializedCoin << pubCoin.getValue().memoryRequired();

            // and this one will write the size in different byte lengths depending on the length of vector. If vector size is <0.4c, which is 76, will write the size of vector in just 1 byte. In our case the size is always 34, so must write that 34 in 1 byte.
            std::vector<unsigned char> vch = pubCoin.getValue().getvch();
            scriptSerializedCoin.insert(scriptSerializedCoin.end(), vch.begin(), vch.end());

            CRecipient recipient = {scriptSerializedCoin, coinValue, false};

            vecSend.push_back(recipient);
            privCoins.push_back(newCoin);
            vHdMints.push_back(fHdMint);
        }
    }

    string strError = pwalletMain->MintAndStoreSigma(vecSend, privCoins, vHdMints, wtx);

    if (strError != "")
        throw runtime_error(strError);

    return wtx.GetHash().GetHex();
}

UniValue sendprivate(Type type, const UniValue& data, const UniValue& auth, bool fHelp) {

    // Ensure Sigma mints is already accepted by network so users will not lost their coins
    // due to other nodes will treat it as garbage data.
    if (!sigma::IsSigmaAllowed()) {
        throw JSONAPIError(API_WALLET_ERROR, "Sigma is not activated yet");
    }

    // Initially grab the existing transaction metadata from the filesystem.
    UniValue txMetadataUni(UniValue::VOBJ);
    UniValue txMetadataData(UniValue::VOBJ);
    getTxMetadata(txMetadataUni, txMetadataData);

    if(txMetadataUni.empty()){
        UniValue txMetadataUni(UniValue::VOBJ);
    }

    if(txMetadataData.empty()){
        UniValue txMetadataData(UniValue::VOBJ);
    }

    switch(type){
        case Create: {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            UniValue outputs(UniValue::VARR);
            outputs = find_value(data, "outputs").get_array();
            std::string label = find_value(data, "label").get_str();

            CWalletTx wtx; 

            std::set<CBitcoinAddress> setAddress;
            std::vector<CRecipient> vecSend;

            CAmount totalAmount = 0;
            if (outputs.size() <= 0) {
                throw JSONAPIError(API_INVALID_PARAMETER, "Required at least an address to send");
            }

            UniValue txMetadataEntry(UniValue::VOBJ);
            UniValue output(UniValue::VOBJ);
            for(size_t index=0; index<outputs.size(); index++){
                output = outputs[index];
                std::string strAddr = find_value(output, "address").get_str();
                // satoshi amount
                CAmount nAmount = find_value(output, "amount").get_int64();
                CBitcoinAddress address(strAddr);
                CScript scriptPubKey = GetScriptForDestination(address.Get());

                if (!address.IsValid())
                    throw JSONAPIError(API_INVALID_ADDRESS_OR_KEY, "Invalid zcoin address: " + strAddr);

                if (!setAddress.insert(address).second)
                    throw JSONAPIError(API_INVALID_PARAMETER, "Invalid parameter, duplicated address: " + strAddr);

                if (nAmount <= 0) {
                    throw JSONAPIError(API_TYPE_ERROR, "Invalid amount for send");
                }
                totalAmount += nAmount;

                bool fSubtractFeeFromAmount = false;
                vecSend.push_back({scriptPubKey, nAmount, fSubtractFeeFromAmount});

                UniValue txMetadataSubEntry(UniValue::VOBJ);

                // write label and amount to entry object
                txMetadataSubEntry.push_back(Pair("amount", nAmount));
                txMetadataSubEntry.push_back(Pair("label", label));
                txMetadataEntry.push_back(Pair(strAddr, txMetadataSubEntry));
            }

            EnsureWalletIsUnlocked();

            CAmount nFeeRequired = 0;
            std::vector<CSigmaEntry> coins;
            std::vector<CHDMint> changes;
            bool fChangeAddedToFee;
            std::string txidStr;

            try {
                // create transaction
                wtx = pwalletMain->CreateSigmaSpendTransaction(vecSend, nFeeRequired, coins, changes, fChangeAddedToFee);

                // Write tx data to filesystem
                txidStr = wtx.GetHash().GetHex();
                txMetadataData.push_back(Pair(txidStr, txMetadataEntry));
                if(!txMetadataUni.replace("data", txMetadataData)){
                    throw runtime_error("Could not replace key/value pair.");
                }
                setTxMetadata(txMetadataUni);

                // commit transaction
                pwalletMain->CommitSigmaTransaction(wtx, coins, changes);

            }
            catch (const InsufficientFunds& e) {
                throw JSONAPIError(API_WALLET_INSUFFICIENT_FUNDS, e.what());
            }
            catch (const std::exception& e) {
                throw JSONAPIError(API_WALLET_ERROR, e.what());
            }


            // publish spent mint data to API
            UniValue mintUpdates(UniValue::VOBJ);
            unsigned int index;
            string txid;

            BOOST_FOREACH(CSigmaEntry coin, coins){
                COutPoint outpoint;
                if(!sigma::GetOutPoint(outpoint, coin.value))
                    throw runtime_error("Mint tx not found!");
                txid = outpoint.hash.ToString();
                index = outpoint.n;
                string key = txid + to_string(index);
                UniValue entry(UniValue::VOBJ);
                entry.push_back(Pair("txid", txid));
                entry.push_back(Pair("index", to_string(index)));
                entry.push_back(Pair("available", false));
                mintUpdates.push_back(Pair(key, entry));
            }
            LogPrintf("mintUpdates: %s\n", mintUpdates.write());
            GetMainSignals().UpdatedMintStatus(mintUpdates.write());

            return txidStr;
        }

        default: {
           throw JSONAPIError(API_TYPE_NOT_IMPLEMENTED, "Error: type does not exist for method called, or no type passed where method requires it."); 
        }
    }
}

UniValue listmints(Type type, const UniValue& data, const UniValue& auth, bool fHelp) {

    EnsureWalletIsUnlocked();

    list <CSigmaEntry> listPubcoin = zwalletMain->GetTracker().MintsAsZerocoinEntries(true, false);
    UniValue results(UniValue::VOBJ);

    BOOST_FOREACH(const CSigmaEntry &sigmaItem, listPubcoin) {
        uint256 serialNumberHash = primitives::GetSerialHash(sigmaItem.serialNumber);

        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("id", sigmaItem.id));
        entry.push_back(Pair("IsUsed", sigmaItem.IsUsed));
        entry.push_back(Pair("denomination", sigmaItem.get_denomination_value()));
        entry.push_back(Pair("value", sigmaItem.value.GetHex()));
        entry.push_back(Pair("serialNumber", sigmaItem.serialNumber.GetHex()));
        entry.push_back(Pair("nHeight", sigmaItem.nHeight));
        entry.push_back(Pair("randomness", sigmaItem.randomness.GetHex()));
        results.push_back(Pair(serialNumberHash.ToString(), entry));
    }

    return results;
}

static const CAPICommand commands[] =
{ //  category              collection         actor (function)          authPort   authPassphrase   warmupOk
  //  --------------------- ------------       ----------------          -------- --------------   --------
    { "sigma",           "mint",            &mint,                    true,      true,            false  },
    { "sigma",           "sendPrivate",     &sendprivate,             true,      true,            false  },
    { "sigma",           "listMints",       &listmints,               true,      true,            false  },
    { "sigma",           "mintStatus",      &mintstatus,              true,      false,           false  }
};
void RegisterSigmaAPICommands(CAPITable &tableAPI)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableAPI.appendCommand(commands[vcidx].collection, &commands[vcidx]);
}