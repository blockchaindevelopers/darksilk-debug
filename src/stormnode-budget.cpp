// Copyright (c) 2014-2016 The Dash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "stormnode-budget.h"
#include "main.h"
#include "init.h"
#include "stormnode.h"
#include "sandstorm.h"
#include "stormnodeman.h"
#include "stormnode-sync.h"
#include "util.h"
#include "addrman.h"

CBudgetManager budget;
CCriticalSection cs_budget;

std::map<uint256, int64_t> askedForSourceProposalOrBudget;
std::vector<CBudgetProposalBroadcast> vecImmatureBudgetProposals;
std::vector<CFinalizedBudgetBroadcast> vecImmatureFinalizedBudgets;

int nSubmittedFinalBudget;

int GetBudgetPaymentCycleBlocks(){
    // Amount of blocks in a months period of time 60*60*24*(365.25/12))/64
    if(Params().NetworkID() == CChainParams::MAIN) return FIXED_STORMNODE_BUDGET_FREQUENCY;
    //for testing purposes

    return 50; //ten times per day
}

bool IsBudgetCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf)
{
    CTransaction txCollateral;
    uint256 nBlockHash;
    if(!GetTransaction(nTxCollateralHash, txCollateral, nBlockHash)){
        strError = strprintf("Can't find collateral tx %s", txCollateral.ToString());
        LogPrintf ("CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    if(txCollateral.vout.size() < 1) return false;
    if(txCollateral.nLockTime != 0) return false;

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    bool foundOpReturn = false;
    BOOST_FOREACH(const CTxOut o, txCollateral.vout){
        if(!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable()){
            strError = strprintf("Invalid Script %s", txCollateral.ToString());
            LogPrintf ("CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
            return false;
        }
        if(o.scriptPubKey == findScript && o.nValue >= BUDGET_FEE_TX) foundOpReturn = true;

    }
    if(!foundOpReturn){
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrintf ("CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    int conf = GetIXConfirmations(nTxCollateralHash);
   
    if (nBlockHash != uint256(0)) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (pindex->IsInMainChain()) {
                conf += nBestHeight - pindex->nHeight + 1;
                nTime = pindex->nTime;
            }
        }
    }

    nConf = conf;

    //if we're syncing we won't have instantX information, so accept 1 confirmation
    if(conf >= BUDGET_FEE_CONFIRMATIONS){
        return true;
    } else {
        strError = strprintf("Collateral requires at least %d confirmations - %d confirmations", BUDGET_FEE_CONFIRMATIONS, conf);
        LogPrintf ("CBudgetProposalBroadcast::IsBudgetCollateralValid - %s - %d confirmations\n", strError, conf);
        return false;
    }
}

void CBudgetManager::CheckOrphanVotes()
{
    LOCK(cs);


    std::string strError = "";
    std::map<uint256, CBudgetVote>::iterator it1 = mapOrphanStormnodeBudgetVotes.begin();
    while(it1 != mapOrphanStormnodeBudgetVotes.end()){
        if(budget.UpdateProposal(((*it1).second), NULL, strError)){
            LogPrintf("CBudgetManager::CheckOrphanVotes - Proposal/Budget is known, activating and removing orphan vote\n");
            mapOrphanStormnodeBudgetVotes.erase(it1++);
        } else {
            ++it1;
        }
    }
    std::map<uint256, CFinalizedBudgetVote>::iterator it2 = mapOrphanFinalizedBudgetVotes.begin();
    while(it2 != mapOrphanFinalizedBudgetVotes.end()){
        if(budget.UpdateFinalizedBudget(((*it2).second),NULL, strError)){
            LogPrintf("CBudgetManager::CheckOrphanVotes - Proposal/Budget is known, activating and removing orphan vote\n");
            mapOrphanFinalizedBudgetVotes.erase(it2++);
        } else {
            ++it2;
        }
    }
}

void CBudgetManager::SubmitFinalBudget()
{
    CBlockIndex* pindexPrev = pindexBest;
    if(!pindexPrev) return;

    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
    if(nSubmittedFinalBudget >= nBlockStart) return;
    if(nBlockStart - pindexPrev->nHeight > 576*2) return; //submit final budget 2 days before payment

    std::vector<CBudgetProposal*> vBudgetProposals = budget.GetBudget();
    std::string strBudgetName = "main";
    std::vector<CTxBudgetPayment> vecTxBudgetPayments;

    for(unsigned int i = 0; i < vBudgetProposals.size(); i++){
        CTxBudgetPayment txBudgetPayment;
        txBudgetPayment.nProposalHash = vBudgetProposals[i]->GetHash();
        txBudgetPayment.payee = vBudgetProposals[i]->GetPayee();
        txBudgetPayment.nAmount = vBudgetProposals[i]->GetAllotted();
        vecTxBudgetPayments.push_back(txBudgetPayment);
    }

    if(vecTxBudgetPayments.size() < 1) {
        LogPrintf("CBudgetManager::SubmitFinalBudget - Found No Proposals For Period\n");
        return;
    }

    CFinalizedBudgetBroadcast tempBudget(strBudgetName, nBlockStart, vecTxBudgetPayments, 0);
    if(mapSeenFinalizedBudgets.count(tempBudget.GetHash())) {
        LogPrintf("CBudgetManager::SubmitFinalBudget - Budget already exists - %s\n", tempBudget.GetHash().ToString());
        nSubmittedFinalBudget = pindexPrev->nHeight;
        return; //already exists
    }

    //create fee tx
    CTransaction tx;

    if(!mapCollateral.count(tempBudget.GetHash())){
        CWalletTx wtx;
        if(!pwalletMain->GetBudgetSystemCollateralTX(wtx, tempBudget.GetHash(), false)){
            LogPrintf("CBudgetManager::SubmitFinalBudget - Can't make collateral transaction\n");
            return;
        }

        // make our change address
        CReserveKey reservekey(pwalletMain);
        //send the tx to the network
        pwalletMain->CommitTransaction(wtx, reservekey, "ix");

        mapCollateral.insert(make_pair(tempBudget.GetHash(), (CTransaction)wtx));
        tx = (CTransaction)wtx;
    } else {
        tx = mapCollateral[tempBudget.GetHash()];
    }

    CTxIn in(COutPoint(tx.GetHash(), 0));
    int conf = GetInputAgeIX(tx.GetHash(), in);
    /*
        Wait will we have 1 extra confirmation, otherwise some clients might reject this feeTX
        -- This function is tied to NewBlock, so we will propagate this budget while the block is also propagating
    */
    if(conf < BUDGET_FEE_CONFIRMATIONS+1){
        LogPrintf ("CBudgetManager::SubmitFinalBudget - Collateral requires at least %d confirmations - %s - %d confirmations\n", BUDGET_FEE_CONFIRMATIONS, tx.GetHash().ToString(), conf);
        return;
    }

    nSubmittedFinalBudget = nBlockStart;

    //create the proposal incase we're the first to make it
    CFinalizedBudgetBroadcast finalizedBudgetBroadcast(strBudgetName, nBlockStart, vecTxBudgetPayments, tx.GetHash());

    std::string strError = "";
    if(!finalizedBudgetBroadcast.IsValid(strError)){
        LogPrintf("CBudgetManager::SubmitFinalBudget - Invalid finalized budget - %s \n", strError);
        return;
    }

    LOCK(cs);
    mapSeenFinalizedBudgets.insert(make_pair(finalizedBudgetBroadcast.GetHash(), finalizedBudgetBroadcast));
    finalizedBudgetBroadcast.Relay();
    budget.AddFinalizedBudget(finalizedBudgetBroadcast);
}

//
// CBudgetDB
//

CBudgetDB::CBudgetDB()
{
    pathDB = GetDataDir() / "budget.dat";
    strMagicMessage = "StormnodeBudget";
}

bool CBudgetDB::Write(const CBudgetManager& objToSave)
{
    LOCK(objToSave.cs);

    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage; // stormnode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    }
    catch (std::exception &e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrintf("Written info to budget.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("Budget manager - %s\n", objToSave.ToString());

    return true;
}

CBudgetDB::ReadResult CBudgetDB::Read(CBudgetManager& objToLoad, bool fDryRun)
{
    LOCK(objToLoad.cs);

    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (std::exception &e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp)
    {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }


    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (stormnode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp)
        {
            error("%s : Invalid stormnode cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
        {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CBudgetManager object
        ssObj >> objToLoad;
    }
    catch (std::exception &e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrintf("Loaded info from budget.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", objToLoad.ToString());
    if(!fDryRun) {
        LogPrintf("Budget manager - cleaning....\n");
        objToLoad.CheckAndRemove();
        LogPrintf("Budget manager - %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpBudgets()
{
    int64_t nStart = GetTimeMillis();

    CBudgetDB budgetdb;
    CBudgetManager tempBudget;

    LogPrintf("Verifying budget.dat format...\n");
    CBudgetDB::ReadResult readResult = budgetdb.Read(tempBudget, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CBudgetDB::FileError)
        LogPrintf("Missing budgets file - budget.dat, will try to recreate\n");
    else if (readResult != CBudgetDB::Ok)
    {
        LogPrintf("Error reading budget.dat: ");
        if(readResult == CBudgetDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
        {
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrintf("Writting info to budget.dat...\n");
    budgetdb.Write(budget);

    LogPrintf("Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool CBudgetManager::AddFinalizedBudget(CFinalizedBudget& finalizedBudget)
{
    std::string strError = "";
    if(!finalizedBudget.IsValid(strError)) return false;

    if(mapFinalizedBudgets.count(finalizedBudget.GetHash())) {
        return false;
    }

    mapFinalizedBudgets.insert(make_pair(finalizedBudget.GetHash(), finalizedBudget));
    return true;
}

bool CBudgetManager::AddProposal(CBudgetProposal& budgetProposal)
{
    LOCK(cs);
    std::string strError = "";
    if(!budgetProposal.IsValid(strError)) {
        LogPrintf("CBudgetManager::AddProposal - invalid budget proposal - %s\n", strError);
        return false;
    }

    if(mapProposals.count(budgetProposal.GetHash())) {
        return false;
    }

    mapProposals.insert(make_pair(budgetProposal.GetHash(), budgetProposal));
    return true;
}

void CBudgetManager::CheckAndRemove()
{
    LogPrintf("CBudgetManager::CheckAndRemove \n");

    CBlockIndex* pindexPrev = pindexBest;
    if(!pindexPrev) return;

    std::string strError = "";
    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while(it != mapFinalizedBudgets.end())
    {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        pfinalizedBudget->fValid = pfinalizedBudget->IsValid(strError);
        if(pfinalizedBudget->fValid) {
            pfinalizedBudget->AutoCheck();
            ++it;
        // if it's too old, remove it
        } else if(pfinalizedBudget->nBlockStart != 0 && pfinalizedBudget->nBlockStart < pindexPrev->nHeight - GetBudgetPaymentCycleBlocks()) {
            mapFinalizedBudgets.erase(it++);
            LogPrintf("CBudgetManager::CheckAndRemove - removing budget %s\n", pfinalizedBudget->GetHash().ToString());
        }

        ++it;
    }

    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while(it2 != mapProposals.end())
    {
        CBudgetProposal* pbudgetProposal = &((*it2).second);
        pbudgetProposal->fValid = pbudgetProposal->IsValid(strError);
        ++it2;
    }
}

//TODO (Amir): Use CMutableTransaction here -->
void CBudgetManager::FillBlockPayee(CTransaction& txNew, CAmount nFees)
{
    LOCK(cs);

    CBlockIndex* pindexPrev = pindexBest;
    if(!pindexPrev) return;

    int nHighestCount = 0;
    CScript payee;
    CAmount nAmount = 0;

    // ------- Grab The Highest Count
    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while(it != mapFinalizedBudgets.end())
    {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if(pfinalizedBudget->GetVoteCount() > nHighestCount &&
                pindexPrev->nHeight + 1 >= pfinalizedBudget->GetBlockStart() &&
                pindexPrev->nHeight + 1 <= pfinalizedBudget->GetBlockEnd() &&
                pfinalizedBudget->GetPayeeAndAmount(pindexPrev->nHeight + 1, payee, nAmount)){
                    nHighestCount = pfinalizedBudget->GetVoteCount();
        }
        ++it;
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nBits, pindexPrev->nHeight, nFees);

    if(nHighestCount > 0){

        txNew.vout[1].scriptPubKey = payee;

        //these are super blocks, PoS and budget payments only.  No stormnode payments. (TODO (Amir): add SN payment here)
        if(txNew.vout.size() == 4) // 2 stake outputs, stake was split, plus 3x budget payments
        {
            txNew.vout[1].nValue = nAmount;
            txNew.vout[2].nValue = (blockValue / 2 / CENT) * CENT;
            txNew.vout[3].nValue = blockValue;
        }
        else if(txNew.vout.size() == 3) // only 1 stake output, was not split, plus 3x budget payments
        {
            txNew.vout[1].nValue = nAmount;
            txNew.vout[2].nValue = blockValue;
        }
        CTxDestination ctxdAddress1;
        ExtractDestination(payee, ctxdAddress1);
        CDarkSilkAddress address1(ctxdAddress1);
        LogPrintf("Budget payment 1 to %s for %lld\n", address1.ToString().c_str(), nAmount);
    }
    else
    {
        //No budget finalized and approved, so we only pay PoS minters. (add SN payment here)
        if(txNew.vout.size() == 3) // 2 stake outputs, stake was split, plus 3x budget payments
        {
            txNew.vout[1].nValue = (blockValue / 2 / CENT) * CENT;
            txNew.vout[2].nValue = blockValue;
        }
        else if(txNew.vout.size() == 2) // only 1 stake output, was not split, plus 3x budget payments
        {
            txNew.vout[1].nValue = blockValue;
        }
    }
}

CFinalizedBudget *CBudgetManager::FindFinalizedBudget(uint256 nHash)
{
    if(mapFinalizedBudgets.count(nHash))
        return &mapFinalizedBudgets[nHash];

    return NULL;
}

CBudgetProposal *CBudgetManager::FindProposal(const std::string &strProposalName)
{
    //find the prop with the highest yes count

    int nYesCount = -99999;
    CBudgetProposal* pbudgetProposal = NULL;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while(it != mapProposals.end()){
        if((*it).second.strProposalName == strProposalName && (*it).second.GetYesCount() > nYesCount){
            pbudgetProposal = &((*it).second);
            nYesCount = pbudgetProposal->GetYesCount();
        }
        ++it;
    }

    if(nYesCount == -99999) return NULL;

    return pbudgetProposal;
}

CBudgetProposal *CBudgetManager::FindProposal(uint256 nHash)
{
    LOCK(cs);

    if(mapProposals.count(nHash))
        return &mapProposals[nHash];

    return NULL;
}

bool CBudgetManager::IsBudgetPaymentBlock(int nBlockHeight)
{
    int nHighestCount = -1;

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while(it != mapFinalizedBudgets.end())
    {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if(pfinalizedBudget->GetVoteCount() > nHighestCount &&
            nBlockHeight >= pfinalizedBudget->GetBlockStart() &&
            nBlockHeight <= pfinalizedBudget->GetBlockEnd()){
            nHighestCount = pfinalizedBudget->GetVoteCount();
        }

        ++it;
    }

    /*
        If budget doesn't have 5% of the network votes, then we should pay a stormnode instead
    */
    if(nHighestCount > snodeman.CountEnabled(MIN_BUDGET_PEER_PROTO_VERSION)/20) return true;

    return false;
}

bool CBudgetManager::HasNextFinalizedBudget()
{
    CBlockIndex* pindexPrev = pindexBest;
    if(!pindexPrev) return false;

    if(stormnodeSync.IsBudgetFinEmpty()) return true;

    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
    if(nBlockStart - pindexPrev->nHeight > 576*2) return true; //we wouldn't have the budget yet

    if(budget.IsBudgetPaymentBlock(nBlockStart)) return true;

    LogPrintf("CBudgetManager::HasNextFinalizedBudget() - Client is missing budget - %lli\n", nBlockStart);

    return false;
}

bool CBudgetManager::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs);

    int nHighestCount = 0;
    std::vector<CFinalizedBudget*> ret;

    // ------- Grab The Highest Count

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while(it != mapFinalizedBudgets.end())
    {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if(pfinalizedBudget->GetVoteCount() > nHighestCount &&
                nBlockHeight >= pfinalizedBudget->GetBlockStart() &&
                nBlockHeight <= pfinalizedBudget->GetBlockEnd()){
                    nHighestCount = pfinalizedBudget->GetVoteCount();
        }

        ++it;
    }

    /*
        If budget doesn't have 5% of the network votes, then we should pay a stormnode instead
    */
    if(nHighestCount < snodeman.CountEnabled(MIN_BUDGET_PEER_PROTO_VERSION)/20) return false;

    // check the highest finalized budgets (+/- 10% to assist in consensus)

    it = mapFinalizedBudgets.begin();
    while(it != mapFinalizedBudgets.end())
    {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        if(pfinalizedBudget->GetVoteCount() > nHighestCount - snodeman.CountEnabled(MIN_BUDGET_PEER_PROTO_VERSION)/10){
            if(nBlockHeight >= pfinalizedBudget->GetBlockStart() && nBlockHeight <= pfinalizedBudget->GetBlockEnd()){
                if(pfinalizedBudget->IsTransactionValid(txNew, nBlockHeight)){
                    return true;
                }
            }
        }

        ++it;
    }

    //we looked through all of the known budgets
    return false;
}

std::vector<CBudgetProposal*> CBudgetManager::GetAllProposals()
{
    LOCK(cs);

    std::vector<CBudgetProposal*> vBudgetProposalRet;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while(it != mapProposals.end())
    {
        (*it).second.CleanAndRemove(false);

        CBudgetProposal* pbudgetProposal = &((*it).second);
        vBudgetProposalRet.push_back(pbudgetProposal);

        ++it;
    }

    return vBudgetProposalRet;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CBudgetProposal*, int> &left, const std::pair<CBudgetProposal*, int> &right) {
      if( left.second != right.second)
        return (left.second > right.second);
      return (left.first->nFeeTXHash > right.first->nFeeTXHash);
    }
};

//Need to review this function
std::vector<CBudgetProposal*> CBudgetManager::GetBudget()
{
    LOCK(cs);

    // ------- Sort budgets by Yes Count

    std::vector<std::pair<CBudgetProposal*, int> > vBudgetPorposalsSort;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while(it != mapProposals.end()){
        (*it).second.CleanAndRemove(false);
        vBudgetPorposalsSort.push_back(make_pair(&((*it).second), (*it).second.GetYesCount()-(*it).second.GetNoCount()));
        ++it;
    }

    std::sort(vBudgetPorposalsSort.begin(), vBudgetPorposalsSort.end(), sortProposalsByVotes());

    // ------- Grab The Budgets In Order

    std::vector<CBudgetProposal*> vBudgetProposalsRet;

    CAmount nBudgetAllocated = 0;
    CBlockIndex* pindexPrev = pindexBest;
    if(pindexPrev == NULL) return vBudgetProposalsRet;

    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
    int nBlockEnd  =  nBlockStart + 100;
    CAmount nTotalBudget = GetTotalBudget(nBlockStart);


    std::vector<std::pair<CBudgetProposal*, int> >::iterator it2 = vBudgetPorposalsSort.begin();
    while(it2 != vBudgetPorposalsSort.end())
    {
        CBudgetProposal* pbudgetProposal = (*it2).first;

        printf("-> Budget Name : %s\n", pbudgetProposal->strProposalName.c_str());
        printf("------- nBlockStart : %d\n", pbudgetProposal->nBlockStart);
        printf("------- nBlockEnd : %d\n", pbudgetProposal->nBlockEnd);
        printf("------- nBlockStart2 : %d\n", nBlockStart);
        printf("------- nBlockEnd2 : %d\n", nBlockEnd);

        printf("------- 1 : %d\n", pbudgetProposal->fValid && pbudgetProposal->nBlockStart <= nBlockStart);
        printf("------- 2 : %d\n", pbudgetProposal->nBlockEnd >= nBlockEnd);
        printf("------- 3 : %d\n", pbudgetProposal->GetYesCount() - pbudgetProposal->GetNoCount() > snodeman.CountEnabled(MIN_BUDGET_PEER_PROTO_VERSION)/10);
        printf("------- 4 : %d\n", pbudgetProposal->IsEstablished());

        //prop start/end should be inside this period
        if(pbudgetProposal->fValid && pbudgetProposal->nBlockStart <= nBlockStart &&
                pbudgetProposal->nBlockEnd >= nBlockEnd &&
                pbudgetProposal->GetYesCount() - pbudgetProposal->GetNoCount() > snodeman.CountEnabled(MIN_BUDGET_PEER_PROTO_VERSION)/10 &&
                pbudgetProposal->IsEstablished())
        {
            printf("------- In range \n");

            if(pbudgetProposal->GetAmount() + nBudgetAllocated <= nTotalBudget) {
                pbudgetProposal->SetAllotted(pbudgetProposal->GetAmount());
                nBudgetAllocated += pbudgetProposal->GetAmount();
                vBudgetProposalsRet.push_back(pbudgetProposal);
                printf("------- YES \n");
            } else {
                pbudgetProposal->SetAllotted(0);
            }
        }

        ++it2;
    }

    return vBudgetProposalsRet;
}

struct sortFinalizedBudgetsByVotes {
    bool operator()(const std::pair<CFinalizedBudget*, int> &left, const std::pair<CFinalizedBudget*, int> &right) {
        return left.second > right.second;
    }
};

std::vector<CFinalizedBudget*> CBudgetManager::GetFinalizedBudgets()
{
    LOCK(cs);

    std::vector<CFinalizedBudget*> vFinalizedBudgetsRet;
    std::vector<std::pair<CFinalizedBudget*, int> > vFinalizedBudgetsSort;

    // ------- Grab The Budgets In Order

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while(it != mapFinalizedBudgets.end())
    {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        vFinalizedBudgetsSort.push_back(make_pair(pfinalizedBudget, pfinalizedBudget->GetVoteCount()));
        ++it;
    }
    std::sort(vFinalizedBudgetsSort.begin(), vFinalizedBudgetsSort.end(), sortFinalizedBudgetsByVotes());

    std::vector<std::pair<CFinalizedBudget*, int> >::iterator it2 = vFinalizedBudgetsSort.begin();
    while(it2 != vFinalizedBudgetsSort.end())
    {
        vFinalizedBudgetsRet.push_back((*it2).first);
        ++it2;
    }

    return vFinalizedBudgetsRet;
}

std::string CBudgetManager::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs);

    std::string ret = "unknown-budget";

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while(it != mapFinalizedBudgets.end())
    {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if(nBlockHeight >= pfinalizedBudget->GetBlockStart() && nBlockHeight <= pfinalizedBudget->GetBlockEnd()){
            CTxBudgetPayment payment;
            if(pfinalizedBudget->GetBudgetPaymentByBlock(nBlockHeight, payment)){
                if(ret == "unknown-budget"){
                    ret = payment.nProposalHash.ToString();
                } else {
                    ret += ",";
                    ret += payment.nProposalHash.ToString();
                }
            } else {
                LogPrintf("CBudgetManager::GetRequiredPaymentsString - Couldn't find budget payment for block %d\n", nBlockHeight);
            }
        }

        ++it;
    }

    return ret;
}

CAmount CBudgetManager::GetTotalBudget(int nHeight)
{
    if(pindexBest == NULL) return 0;

    if(nHeight == 0) return 0;

    return (FIXED_STORMNODE_BUDGET_AMOUNT);
}

void CBudgetManager::NewBlock()
{
    TRY_LOCK(cs, fBudgetNewBlock);
    if(!fBudgetNewBlock) return;

    if (stormnodeSync.RequestedStormnodeAssets <= STORMNODE_SYNC_BUDGET) return;

    if (strBudgetMode == "suggest") { //suggest the budget we see
        SubmitFinalBudget();
    }

    //this function should be called 1/6 blocks, allowing up to 100 votes per day on all proposals
    if(nBestHeight % 6 != 0) return;

    // incremental sync with our peers
    if(stormnodeSync.IsSynced()){
        LogPrintf("CBudgetManager::NewBlock - incremental sync started\n");
        if(nBestHeight % 600 == rand() % 600) {
            ClearSeen();
            ResetSync();
        }

        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            if(pnode->nVersion >= MIN_BUDGET_PEER_PROTO_VERSION)
                Sync(pnode, 0, true);

        MarkSynced();
    }


    CheckAndRemove();

    //remove invalid votes once in a while (we have to check the signatures and validity of every vote, somewhat CPU intensive)

    std::map<uint256, int64_t>::iterator it = askedForSourceProposalOrBudget.begin();
    while(it != askedForSourceProposalOrBudget.end()){
        if((*it).second > GetTime() - (60*60*24)){
            ++it;
        } else {
            askedForSourceProposalOrBudget.erase(it++);
        }
    }

    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while(it2 != mapProposals.end()){
        (*it2).second.CleanAndRemove(false);
        ++it2;
    }

    std::map<uint256, CFinalizedBudget>::iterator it3 = mapFinalizedBudgets.begin();
    while(it3 != mapFinalizedBudgets.end()){
        (*it3).second.CleanAndRemove(false);
        ++it3;
    }

    std::vector<CBudgetProposalBroadcast>::iterator it4 = vecImmatureBudgetProposals.begin();
    while(it4 != vecImmatureBudgetProposals.end())
    {
        std::string strError = "";
        int nConf = 0;
        if(!IsBudgetCollateralValid((*it4).nFeeTXHash, (*it4).GetHash(), strError, (*it4).nTime, nConf)){
            ++it4;
            continue;
        }

        if(!(*it4).IsValid(strError)) {
            LogPrintf("sprop (immature) - invalid budget proposal - %s\n", strError);
            it4 = vecImmatureBudgetProposals.erase(it4);
            continue;
        }

        CBudgetProposal budgetProposal((*it4));
        if(AddProposal(budgetProposal)) {(*it4).Relay();}

        LogPrintf("sprop (immature) - new budget - %s\n", (*it4).GetHash().ToString());
        it4 = vecImmatureBudgetProposals.erase(it4);
    }

    std::vector<CFinalizedBudgetBroadcast>::iterator it5 = vecImmatureFinalizedBudgets.begin();
    while(it5 != vecImmatureFinalizedBudgets.end())
    {
        std::string strError = "";
        int nConf = 0;
        if(!IsBudgetCollateralValid((*it5).nFeeTXHash, (*it5).GetHash(), strError, (*it5).nTime, nConf)){
            ++it5;
            continue;
        }

        if(!(*it5).IsValid(strError)) {
            LogPrintf("fbs (immature) - invalid finalized budget - %s\n", strError);
            it5 = vecImmatureFinalizedBudgets.erase(it5);
            continue;
        }

        LogPrintf("fbs (immature) - new finalized budget - %s\n", (*it5).GetHash().ToString());

        CFinalizedBudget finalizedBudget((*it5));
        if(AddFinalizedBudget(finalizedBudget)) {(*it5).Relay();}

        it5 = vecImmatureFinalizedBudgets.erase(it5);
    }

}

void CBudgetManager::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // lite mode is not supported
    if(fLiteMode) return;
    if(!stormnodeSync.IsBlockchainSynced()) return;

    LOCK(cs_budget);

    if (strCommand == "snvs") { //Stormnode vote sync
        uint256 nProp;
        vRecv >> nProp;

        if(Params().NetworkID() == CChainParams::MAIN){
            if(nProp == 0) {
                if(pfrom->HasFulfilledRequest("snvs")) {
                    LogPrintf("snvs - peer already asked me for the list\n");
                    Misbehaving(pfrom->GetId(), 20);
                    return;
                }
                pfrom->FulfilledRequest("snvs");
            }
        }

        Sync(pfrom, nProp);
        LogPrintf("snvs - Sent Stormnode votes to %s\n", pfrom->addr.ToString());
    }

    if (strCommand == "sprop") { //Stormnode Proposal
        CBudgetProposalBroadcast budgetProposalBroadcast;
        vRecv >> budgetProposalBroadcast;

        if(mapSeenStormnodeBudgetProposals.count(budgetProposalBroadcast.GetHash())){
            stormnodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        if(!IsBudgetCollateralValid(budgetProposalBroadcast.nFeeTXHash, budgetProposalBroadcast.GetHash(), strError, budgetProposalBroadcast.nTime, nConf)){
            LogPrintf("Proposal FeeTX is not valid - %s - %s\n", budgetProposalBroadcast.nFeeTXHash.ToString(), strError);
            if(nConf >= 1) vecImmatureBudgetProposals.push_back(budgetProposalBroadcast);
            return;
        }

        mapSeenStormnodeBudgetProposals.insert(make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));

        if(!budgetProposalBroadcast.IsValid(strError)) {
            LogPrintf("sprop - invalid budget proposal - %s\n", strError);
            return;
        }

        CBudgetProposal budgetProposal(budgetProposalBroadcast);
        if(AddProposal(budgetProposal)) {budgetProposalBroadcast.Relay();}
        stormnodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());

        LogPrintf("sprop - new budget - %s\n", budgetProposalBroadcast.GetHash().ToString());

        //We might have active votes for this proposal that are valid now
        CheckOrphanVotes();
    }

    if (strCommand == "svote") { //Stormnode Vote
        CBudgetVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if(mapSeenStormnodeBudgetVotes.count(vote.GetHash())){
            stormnodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        CStormnode* psn = snodeman.Find(vote.vin);
        if(psn == NULL) {
            LogPrint("snbudget", "svote - unknown stormnode - vin: %s\n", vote.vin.ToString());
            snodeman.AskForSN(pfrom, vote.vin);
            return;
        }


        mapSeenStormnodeBudgetVotes.insert(make_pair(vote.GetHash(), vote));
        if(!vote.SignatureValid(true)){
            LogPrintf("svote - signature invalid\n");
            if(stormnodeSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced stormnode
            snodeman.AskForSN(pfrom, vote.vin);
            return;
        }

        std::string strError = "";
        if(UpdateProposal(vote, pfrom, strError)) {
            vote.Relay();
            stormnodeSync.AddedBudgetItem(vote.GetHash());
        }

        LogPrintf("svote - new budget vote - %s\n", vote.GetHash().ToString());
    }

    if (strCommand == "fbs") { //Finalized Budget Suggestion
        CFinalizedBudgetBroadcast finalizedBudgetBroadcast;
        vRecv >> finalizedBudgetBroadcast;

        if(mapSeenFinalizedBudgets.count(finalizedBudgetBroadcast.GetHash())){
            stormnodeSync.AddedBudgetItem(finalizedBudgetBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        if(!IsBudgetCollateralValid(finalizedBudgetBroadcast.nFeeTXHash, finalizedBudgetBroadcast.GetHash(), strError, finalizedBudgetBroadcast.nTime, nConf)){
            LogPrintf("Finalized Budget FeeTX is not valid - %s - %s\n", finalizedBudgetBroadcast.nFeeTXHash.ToString(), strError);

            if(nConf >= 1) vecImmatureFinalizedBudgets.push_back(finalizedBudgetBroadcast);
            return;
        }

        mapSeenFinalizedBudgets.insert(make_pair(finalizedBudgetBroadcast.GetHash(), finalizedBudgetBroadcast));

        if(!finalizedBudgetBroadcast.IsValid(strError)) {
            LogPrintf("fbs - invalid finalized budget - %s\n", strError);
            return;
        }

        LogPrintf("fbs - new finalized budget - %s\n", finalizedBudgetBroadcast.GetHash().ToString());

        CFinalizedBudget finalizedBudget(finalizedBudgetBroadcast);
        if(AddFinalizedBudget(finalizedBudget)) {finalizedBudgetBroadcast.Relay();}
        stormnodeSync.AddedBudgetItem(finalizedBudgetBroadcast.GetHash());

        //we might have active votes for this budget that are now valid
        CheckOrphanVotes();
    }

    if (strCommand == "fbvote") { //Finalized Budget Vote
        CFinalizedBudgetVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if(mapSeenFinalizedBudgetVotes.count(vote.GetHash())){
            stormnodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        CStormnode* psn = snodeman.Find(vote.vin);
        if(psn == NULL) {
            LogPrint("snbudget", "fbvote - unknown stormnode - vin: %s\n", vote.vin.ToString());
            snodeman.AskForSN(pfrom, vote.vin);
            return;
        }

        mapSeenFinalizedBudgetVotes.insert(make_pair(vote.GetHash(), vote));
        if(!vote.SignatureValid(true)){
            LogPrintf("fbvote - signature invalid\n");
            if(stormnodeSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced stormnode
            snodeman.AskForSN(pfrom, vote.vin);
            return;
        }

        std::string strError = "";
        if(UpdateFinalizedBudget(vote, pfrom, strError)) {
            vote.Relay();
            stormnodeSync.AddedBudgetItem(vote.GetHash());

            LogPrintf("fbvote - new finalized budget vote - %s\n", vote.GetHash().ToString());
        } else {
            LogPrintf("fbvote - rejected finalized budget vote - %s - %s\n", vote.GetHash().ToString(), strError);
        }
    }
}

bool CBudgetManager::PropExists(uint256 nHash)
{
    if(mapProposals.count(nHash)) return true;
    return false;
}

//mark that a full sync is needed
void CBudgetManager::ResetSync()
{
    LOCK(cs);


    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenStormnodeBudgetProposals.begin();
    while(it1 != mapSeenStormnodeBudgetProposals.end()){
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if(pbudgetProposal && pbudgetProposal->fValid){

            //mark votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while(it2 != pbudgetProposal->mapVotes.end()){
                (*it2).second.fSynced = false;
                ++it2;
            }
        }
        ++it1;
    }

    std::map<uint256, CFinalizedBudgetBroadcast>::iterator it3 = mapSeenFinalizedBudgets.begin();
    while(it3 != mapSeenFinalizedBudgets.end()){
        CFinalizedBudget* pfinalizedBudget = FindFinalizedBudget((*it3).first);
        if(pfinalizedBudget && pfinalizedBudget->fValid){

            //send votes
            std::map<uint256, CFinalizedBudgetVote>::iterator it4 = pfinalizedBudget->mapVotes.begin();
            while(it4 != pfinalizedBudget->mapVotes.end()){
                (*it4).second.fSynced = false;
                ++it4;
            }
        }
        ++it3;
    }
}

void CBudgetManager::MarkSynced()
{
    LOCK(cs);

    /*
        Mark that we've sent all valid items
    */

    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenStormnodeBudgetProposals.begin();
    while(it1 != mapSeenStormnodeBudgetProposals.end()){
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if(pbudgetProposal && pbudgetProposal->fValid){

            //mark votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while(it2 != pbudgetProposal->mapVotes.end()){
                if((*it2).second.fValid)
                    (*it2).second.fSynced = true;
                ++it2;
            }
        }
        ++it1;
    }

    std::map<uint256, CFinalizedBudgetBroadcast>::iterator it3 = mapSeenFinalizedBudgets.begin();
    while(it3 != mapSeenFinalizedBudgets.end()){
        CFinalizedBudget* pfinalizedBudget = FindFinalizedBudget((*it3).first);
        if(pfinalizedBudget && pfinalizedBudget->fValid){

            //mark votes
            std::map<uint256, CFinalizedBudgetVote>::iterator it4 = pfinalizedBudget->mapVotes.begin();
            while(it4 != pfinalizedBudget->mapVotes.end()){
                if((*it4).second.fValid)
                    (*it4).second.fSynced = true;
                ++it4;
            }
        }
        ++it3;
    }

}


void CBudgetManager::Sync(CNode* pfrom, uint256 nProp, bool fPartial)
{
    LOCK(cs);

    /*
        Sync with a client on the network

        --

        This code checks each of the hash maps for all known budget proposals and finalized budget proposals, then checks them against the
        budget object to see if they're OK. If all checks pass, we'll send it to the peer.

    */

    int nInvCount = 0;

    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenStormnodeBudgetProposals.begin();
    while(it1 != mapSeenStormnodeBudgetProposals.end()){
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if(pbudgetProposal && pbudgetProposal->fValid && (nProp == 0 || (*it1).first == nProp)){
            pfrom->PushInventory(CInv(MSG_BUDGET_PROPOSAL, (*it1).second.GetHash()));
            nInvCount++;

            //send votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while(it2 != pbudgetProposal->mapVotes.end()){
                if((*it2).second.fValid){
                    if((fPartial && !(*it2).second.fSynced) || !fPartial) {
                        pfrom->PushInventory(CInv(MSG_BUDGET_VOTE, (*it2).second.GetHash()));
                        nInvCount++;
                    }
                }
                ++it2;
            }
        }
        ++it1;
    }

    pfrom->PushMessage("ssc", STORMNODE_SYNC_BUDGET_PROP, nInvCount);

    LogPrintf("CBudgetManager::Sync - sent %d items\n", nInvCount);

    nInvCount = 0;

    std::map<uint256, CFinalizedBudgetBroadcast>::iterator it3 = mapSeenFinalizedBudgets.begin();
    while(it3 != mapSeenFinalizedBudgets.end()){
        CFinalizedBudget* pfinalizedBudget = FindFinalizedBudget((*it3).first);
        if(pfinalizedBudget && pfinalizedBudget->fValid && (nProp == 0 || (*it3).first == nProp)){
            pfrom->PushInventory(CInv(MSG_BUDGET_FINALIZED, (*it3).second.GetHash()));
            nInvCount++;

            //send votes
            std::map<uint256, CFinalizedBudgetVote>::iterator it4 = pfinalizedBudget->mapVotes.begin();
            while(it4 != pfinalizedBudget->mapVotes.end()){
                if((*it4).second.fValid) {
                    if((fPartial && !(*it4).second.fSynced) || !fPartial) {
                        pfrom->PushInventory(CInv(MSG_BUDGET_FINALIZED_VOTE, (*it4).second.GetHash()));
                        nInvCount++;
                    }
                }
                ++it4;
            }
        }
        ++it3;
    }

    pfrom->PushMessage("ssc", STORMNODE_SYNC_BUDGET_FIN, nInvCount);
    LogPrintf("CBudgetManager::Sync - sent %d items\n", nInvCount);

}

bool CBudgetManager::UpdateProposal(CBudgetVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    if(!mapProposals.count(vote.nProposalHash)){
        if(pfrom){
            // only ask for missing items after our syncing process is complete --
            //   otherwise we'll think a full sync succeeded when they return a result
            if(!stormnodeSync.IsSynced()) return false;

            LogPrintf("CBudgetManager::UpdateProposal - Unknown proposal %d, asking for source proposal\n", vote.nProposalHash.ToString());
            mapOrphanStormnodeBudgetVotes[vote.nProposalHash] = vote;

            if(!askedForSourceProposalOrBudget.count(vote.nProposalHash)){
                pfrom->PushMessage("snvs", vote.nProposalHash);
                askedForSourceProposalOrBudget[vote.nProposalHash] = GetTime();
            }
        }

        strError = "Proposal not found!";
        return false;
    }


    return mapProposals[vote.nProposalHash].AddOrUpdateVote(vote, strError);
}

bool CBudgetManager::UpdateFinalizedBudget(CFinalizedBudgetVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    if(!mapFinalizedBudgets.count(vote.nBudgetHash)){
        if(pfrom){
            // only ask for missing items after our syncing process is complete --
            //   otherwise we'll think a full sync succeeded when they return a result
            if(!stormnodeSync.IsSynced()) return false;

            LogPrintf("CBudgetManager::UpdateFinalizedBudget - Unknown Finalized Proposal %s, asking for source budget\n", vote.nBudgetHash.ToString());
            mapOrphanFinalizedBudgetVotes[vote.nBudgetHash] = vote;

            if(!askedForSourceProposalOrBudget.count(vote.nBudgetHash)){
                pfrom->PushMessage("snvs", vote.nBudgetHash);
                askedForSourceProposalOrBudget[vote.nBudgetHash] = GetTime();
            }

        }

        strError = "Finalized Budget not found!";
        return false;
    }

    return mapFinalizedBudgets[vote.nBudgetHash].AddOrUpdateVote(vote, strError);
}

CBudgetProposal::CBudgetProposal()
{
    strProposalName = "unknown";
    nBlockStart = 0;
    nBlockEnd = 0;
    nAmount = 0;
    nTime = 0;
    fValid = true;
}

CBudgetProposal::CBudgetProposal(const CBudgetProposal& other)
{
    strProposalName = other.strProposalName;
    strURL = other.strURL;
    nBlockStart = other.nBlockStart;
    nBlockEnd = other.nBlockEnd;
    address = other.address;
    nAmount = other.nAmount;
    nTime = other.nTime;
    nFeeTXHash = other.nFeeTXHash;
    mapVotes = other.mapVotes;
    fValid = true;
}

CBudgetProposal::CBudgetProposal(std::string strProposalNameIn, std::string strURLIn, int nPaymentCount, CScript addressIn, CAmount nAmountIn, int nBlockStartIn, uint256 nFeeTXHashIn)
{
    strProposalName = strProposalNameIn;
    strURL = strURLIn;

    nBlockStart = nBlockStartIn;

    int nPaymentsStart = nBlockStart - nBlockStart % GetBudgetPaymentCycleBlocks();
    //calculate the end of the cycle for this vote, add half a cycle (vote will be deleted after that block)
    nBlockEnd = nPaymentsStart + GetBudgetPaymentCycleBlocks() * nPaymentCount;

    address = addressIn;
    nAmount = nAmountIn;

    nFeeTXHash = nFeeTXHashIn;
}

bool CBudgetProposal::IsValid(std::string& strError, bool fCheckCollateral)
{
    if(GetNoCount() - GetYesCount() > snodeman.CountEnabled(MIN_BUDGET_PEER_PROTO_VERSION)/10){
         strError = "Active removal";
         return false;
    }

    if(nBlockStart < 0) {
        strError = "Invalid Proposal";
        return false;
    }

    CBlockIndex* pindexPrev = pindexBest;
    if(pindexPrev == NULL) {strError = "Tip is NULL"; return true;}

    if(nBlockStart % GetBudgetPaymentCycleBlocks() != 0){
        int nNext = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
        strError = strprintf("Invalid block start - must be a budget cycle block. Next valid block: %d", nNext);
        return false;
    }

    if(nBlockEnd % GetBudgetPaymentCycleBlocks() != GetBudgetPaymentCycleBlocks()/2){
        strError = "Invalid block end";
        return false;
    }

    if(nBlockEnd < nBlockStart) {
        strError = "Invalid block end - must be greater then block start.";
        return false;
    }

    if(nAmount < 1*COIN) {
        strError = "Invalid proposal amount";
        return false;
    }

    if(strProposalName.size() > 20) {
        strError = "Invalid proposal name, limit of 20 characters.";
        return false;
    }

    if(strProposalName != SanitizeString(strProposalName)) {
        strError = "Invalid proposal name, unsafe characters found.";
        return false;
    }

    if(strURL.size() > 64) {
        strError = "Invalid proposal url, limit of 64 characters.";
        return false;
    }

    if(strURL != SanitizeString(strURL)) {
        strError = "Invalid proposal url, unsafe characters found.";
        return false;
    }

    if(address == CScript()) {
        strError = "Invalid proposal Payment Address";
        return false;
    }

    if(fCheckCollateral){
        int nConf = 0;
        if(!IsBudgetCollateralValid(nFeeTXHash, GetHash(), strError, nTime, nConf)){
            return false;
        }
    }

    /*
        TODO: There might be an issue with multisig in the coinbase on mainnet, we will add support for it in a future release.
    */
    if(address.IsPayToScriptHash()) {
        strError = "Multisig is not currently supported.";
        return false;
    }

    // -- If GetAbsoluteYesCount is more than -10% of the network, flag as invalid
    if(GetAbsoluteYesCount() < -(snodeman.CountEnabled(MIN_BUDGET_PEER_PROTO_VERSION)/10)) {
        strError = "Voted Down";
        return false;
    }

    //can only pay out 10% of the possible coins (min value of coins)
    if(nAmount > budget.GetTotalBudget(nBlockStart)) {
        strError = "Payment more than max";
        return false;
    }

    if(pindexPrev == NULL) {strError = "Tip is NULL"; return true;}

    if(GetBlockEnd()+100 < pindexPrev->nHeight) return false;


    return true;
}

bool CBudgetProposal::AddOrUpdateVote(CBudgetVote& vote, std::string& strError)
{
    LOCK(cs);

    uint256 hash = vote.vin.prevout.GetHash();

    if(mapVotes.count(hash)){
        if(mapVotes[hash].nTime > vote.nTime){
            strError = strprintf("new vote older than existing vote - %s", vote.GetHash().ToString());
            LogPrint("snbudget", "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if(vote.nTime - mapVotes[hash].nTime < BUDGET_VOTE_UPDATE_MIN){
            strError = strprintf("time between votes is too soon - %s - %lli", vote.GetHash().ToString(), vote.nTime - mapVotes[hash].nTime);
            LogPrint("snbudget", "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
    }

    if(vote.nTime > GetTime() + (60*60)){
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli\n", vote.GetHash().ToString(), vote.nTime, GetTime() + (60*60));
        LogPrint("snbudget", "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
        return false;
    }

    mapVotes[hash] = vote;
    return true;
}

// If stormnode voted for a proposal, but is now invalid -- remove the vote
void CBudgetProposal::CleanAndRemove(bool fSignatureCheck)
{
    std::map<uint256, CBudgetVote>::iterator it = mapVotes.begin();

    while(it != mapVotes.end()) {
        (*it).second.fValid = (*it).second.SignatureValid(fSignatureCheck);
        ++it;
    }
}

double CBudgetProposal::GetRatio()
{
    int yeas = 0;
    int nays = 0;

    std::map<uint256, CBudgetVote>::iterator it = mapVotes.begin();

    while(it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_YES) yeas++;
        if ((*it).second.nVote == VOTE_NO) nays++;
        ++it;
    }

    if(yeas+nays == 0) return 0.0f;

    return ((double)(yeas) / (double)(yeas+nays));
}

int CBudgetProposal::GetAbsoluteYesCount()
{
    return GetYesCount() - GetNoCount();
}

int CBudgetProposal::GetYesCount()
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::iterator it = mapVotes.begin();
    while(it != mapVotes.end()){
        if ((*it).second.nVote == VOTE_YES && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CBudgetProposal::GetNoCount()
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::iterator it = mapVotes.begin();
    while(it != mapVotes.end()){
        if ((*it).second.nVote == VOTE_NO && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CBudgetProposal::GetAbstainCount()
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::iterator it = mapVotes.begin();
    while(it != mapVotes.end()){
        if ((*it).second.nVote == VOTE_ABSTAIN && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CBudgetProposal::GetBlockStartCycle()
{
    //end block is half way through the next cycle (so the proposal will be removed much after the payment is sent)

    return nBlockStart - nBlockStart % GetBudgetPaymentCycleBlocks();
}

int CBudgetProposal::GetBlockCurrentCycle()
{
    CBlockIndex* pindexPrev = pindexBest;
    if(pindexPrev == NULL) return -1;

    if(pindexPrev->nHeight >= GetBlockEndCycle()) return -1;

    return pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks();
}

int CBudgetProposal::GetBlockEndCycle()
{
    //end block is half way through the next cycle (so the proposal will be removed much after the payment is sent)

    return nBlockEnd - GetBudgetPaymentCycleBlocks()/2;
}

int CBudgetProposal::GetTotalPaymentCount()
{
    return (GetBlockEndCycle() - GetBlockStartCycle()) / GetBudgetPaymentCycleBlocks();
}

int CBudgetProposal::GetRemainingPaymentCount()
{
    int nBlockHeight = nBlockStart;
    int nPayments = 0;
    // printf("-> Budget Name : %s\n", strProposalName.c_str());
    // printf("------- nBlockStart : %d\n", nBlockStart);
    // printf("------- nBlockEnd : %d\n", nBlockEnd);
    while(nBlockHeight + GetBudgetPaymentCycleBlocks() < nBlockEnd)
    {
        // printf("------- P : %d %d - %d < %d - %d\n", nBlockHeight, nPayments, nBlockHeight + GetBudgetPaymentCycleBlocks(), nBlockEnd, nBlockHeight + GetBudgetPaymentCycleBlocks() < nBlockEnd);
        nBlockHeight += GetBudgetPaymentCycleBlocks();
        nPayments++;
    }
    return nPayments;
}

void CBudgetProposalBroadcast::Relay()
{
    CInv inv(MSG_BUDGET_PROPOSAL, GetHash());
    RelayInv(inv, MIN_BUDGET_PEER_PROTO_VERSION);
}

CBudgetVote::CBudgetVote()
{
    vin = CTxIn();
    nProposalHash = 0;
    nVote = VOTE_ABSTAIN;
    nTime = 0;
    fValid = true;
    fSynced = false;
}

CBudgetVote::CBudgetVote(CTxIn vinIn, uint256 nProposalHashIn, int nVoteIn)
{
    vin = vinIn;
    nProposalHash = nProposalHashIn;
    nVote = nVoteIn;
    nTime = GetAdjustedTime();
    fValid = true;
    fSynced = false;
}

void CBudgetVote::Relay()
{
    CInv inv(MSG_BUDGET_VOTE, GetHash());
    RelayInv(inv, MIN_BUDGET_PEER_PROTO_VERSION);
}

bool CBudgetVote::Sign(CKey& keyStormnode, CPubKey& pubKeyStormnode)
{
    // Choose coins to use
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;

    std::string errorMessage;
    std::string strMessage = vin.prevout.ToStringShort() + nProposalHash.ToString() + boost::lexical_cast<std::string>(nVote) + boost::lexical_cast<std::string>(nTime);

    if(!sandStormSigner.SignMessage(strMessage, errorMessage, vchSig, keyStormnode)) {
        LogPrintf("CBudgetVote::Sign - Error upon calling SignMessage");
        return false;
    }

    if(!sandStormSigner.VerifyMessage(pubKeyStormnode, vchSig, strMessage, errorMessage)) {
        LogPrintf("CBudgetVote::Sign - Error upon calling VerifyMessage");
        return false;
    }

    return true;
}

bool CBudgetVote::SignatureValid(bool fSignatureCheck)
{
    std::string errorMessage;
    std::string strMessage = vin.prevout.ToStringShort() + nProposalHash.ToString() + boost::lexical_cast<std::string>(nVote) + boost::lexical_cast<std::string>(nTime);

    CStormnode* psn = snodeman.Find(vin);

    if(psn == NULL)
    {
        LogPrintf("CBudgetVote::SignatureValid() - Unknown Stormnode - %s\n", vin.ToString());
        return false;
    }

    if(!fSignatureCheck) return true;

    if(!sandStormSigner.VerifyMessage(psn->pubkey2, vchSig, strMessage, errorMessage)) {
        LogPrintf("CBudgetVote::SignatureValid() - Verify message failed\n");
        return false;
    }

    return true;
}

CFinalizedBudget::CFinalizedBudget()
{
    strBudgetName = "";
    nBlockStart = 0;
    vecBudgetPayments.clear();
    mapVotes.clear();
    nFeeTXHash = 0;
    nTime = 0;
    fValid = true;
    fAutoChecked = false;
}

CFinalizedBudget::CFinalizedBudget(const CFinalizedBudget& other)
{
    strBudgetName = other.strBudgetName;
    nBlockStart = other.nBlockStart;
    vecBudgetPayments = other.vecBudgetPayments;
    mapVotes = other.mapVotes;
    nFeeTXHash = other.nFeeTXHash;
    nTime = other.nTime;
    fValid = true;
    fAutoChecked = false;
}

bool CFinalizedBudget::AddOrUpdateVote(CFinalizedBudgetVote& vote, std::string& strError)
{
    LOCK(cs);

    uint256 hash = vote.vin.prevout.GetHash();
    if(mapVotes.count(hash)){
        if(mapVotes[hash].nTime > vote.nTime){
            strError = strprintf("new vote older than existing vote - %s", vote.GetHash().ToString());
            LogPrint("snbudget", "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if(vote.nTime - mapVotes[hash].nTime < BUDGET_VOTE_UPDATE_MIN){
            strError = strprintf("time between votes is too soon - %s - %lli", vote.GetHash().ToString(), vote.nTime - mapVotes[hash].nTime);
            LogPrint("snbudget", "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
            return false;
        }
    }

    if(vote.nTime > GetTime() + (60*60)){
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli", vote.GetHash().ToString(), vote.nTime, GetTime() + (60*60));
        LogPrint("snbudget", "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
        return false;
    }

    mapVotes[hash] = vote;
    return true;
}

//evaluate if we should vote for this. Stormnode only
void CFinalizedBudget::AutoCheck()
{
    LOCK(cs);

    CBlockIndex* pindexPrev = pindexBest;
    if(!pindexPrev) return;

    LogPrintf("CFinalizedBudget::AutoCheck - %lli - %d\n", pindexPrev->nHeight, fAutoChecked);

    if(!fStormNode || fAutoChecked) return;

    //do this 1 in 4 blocks -- spread out the voting activity on mainnet
    // -- this function is only called every sixth block, so this is really 1 in 24 blocks
    if(Params().NetworkID() == CChainParams::MAIN && rand() % 4 != 0) {
        LogPrintf("CFinalizedBudget::AutoCheck - waiting\n");
        return;
    }

    fAutoChecked = true; //we only need to check this once


    if(strBudgetMode == "auto") //only vote for exact matches
    {
        std::vector<CBudgetProposal*> vBudgetProposals = budget.GetBudget();


        for(unsigned int i = 0; i < vecBudgetPayments.size(); i++){
            LogPrintf("CFinalizedBudget::AutoCheck - nProp %d %s\n", i, vecBudgetPayments[i].nProposalHash.ToString());
            LogPrintf("CFinalizedBudget::AutoCheck - Payee %d %s\n", i, vecBudgetPayments[i].payee.ToString());
            LogPrintf("CFinalizedBudget::AutoCheck - nAmount %d %lli\n", i, vecBudgetPayments[i].nAmount);
        }

        for(unsigned int i = 0; i < vBudgetProposals.size(); i++){
            LogPrintf("CFinalizedBudget::AutoCheck - nProp %d %s\n", i, vBudgetProposals[i]->GetHash().ToString());
            LogPrintf("CFinalizedBudget::AutoCheck - Payee %d %s\n", i, vBudgetProposals[i]->GetPayee().ToString());
            LogPrintf("CFinalizedBudget::AutoCheck - nAmount %d %lli\n", i, vBudgetProposals[i]->GetAmount());
        }

        if(vBudgetProposals.size() == 0) {
            LogPrintf("CFinalizedBudget::AutoCheck - Can't get Budget, aborting\n");
            return;
        }

        if(vBudgetProposals.size() != vecBudgetPayments.size()) {
            LogPrintf("CFinalizedBudget::AutoCheck - Budget length doesn't match\n");
            return;
        }


        for(unsigned int i = 0; i < vecBudgetPayments.size(); i++){
            if(i > vBudgetProposals.size() - 1) {
                LogPrintf("CFinalizedBudget::AutoCheck - Vector size mismatch, aborting\n");
                return;
            }

            if(vecBudgetPayments[i].nProposalHash != vBudgetProposals[i]->GetHash()){
                LogPrintf("CFinalizedBudget::AutoCheck - item #%d doesn't match %s %s\n", i, vecBudgetPayments[i].nProposalHash.ToString(), vBudgetProposals[i]->GetHash().ToString());
                return;
            }

            // if(vecBudgetPayments[i].payee != vBudgetProposals[i]->GetPayee()){ -- triggered with false positive
            if(vecBudgetPayments[i].payee.ToString() != vBudgetProposals[i]->GetPayee().ToString()){
                LogPrintf("CFinalizedBudget::AutoCheck - item #%d payee doesn't match %s %s\n", i, vecBudgetPayments[i].payee.ToString(), vBudgetProposals[i]->GetPayee().ToString());
                return;
            }

            if(vecBudgetPayments[i].nAmount != vBudgetProposals[i]->GetAmount()){
                LogPrintf("CFinalizedBudget::AutoCheck - item #%d payee doesn't match %lli %lli\n", i, vecBudgetPayments[i].nAmount, vBudgetProposals[i]->GetAmount());
                return;
            }
        }

        LogPrintf("CFinalizedBudget::AutoCheck - Finalized Budget Matches! Submitting Vote.\n");
        SubmitVote();

    }
}
// If stormnode voted for a proposal, but is now invalid -- remove the vote
void CFinalizedBudget::CleanAndRemove(bool fSignatureCheck)
{
    std::map<uint256, CFinalizedBudgetVote>::iterator it = mapVotes.begin();

    while(it != mapVotes.end()) {
        (*it).second.fValid = (*it).second.SignatureValid(fSignatureCheck);
        ++it;
    }
}


CAmount CFinalizedBudget::GetTotalPayout()
{
    CAmount ret = 0;

    for(unsigned int i = 0; i < vecBudgetPayments.size(); i++){
        ret += vecBudgetPayments[i].nAmount;
    }

    return ret;
}

std::string CFinalizedBudget::GetProposals()
{
    LOCK(cs);
    std::string ret = "";

    BOOST_FOREACH(CTxBudgetPayment& budgetPayment, vecBudgetPayments){
        CBudgetProposal* pbudgetProposal = budget.FindProposal(budgetPayment.nProposalHash);

        std::string token = budgetPayment.nProposalHash.ToString();

        if(pbudgetProposal) token = pbudgetProposal->GetName();
        if(ret == "") {ret = token;}
        else {ret += "," + token;}
    }
    return ret;
}

std::string CFinalizedBudget::GetStatus()
{
    std::string retBadHashes = "";
    std::string retBadPayeeOrAmount = "";

    for(int nBlockHeight = GetBlockStart(); nBlockHeight <= GetBlockEnd(); nBlockHeight++)
    {
        CTxBudgetPayment budgetPayment;
        if(!GetBudgetPaymentByBlock(nBlockHeight, budgetPayment)){
            LogPrintf("CFinalizedBudget::GetStatus - Couldn't find budget payment for block %lld\n", nBlockHeight);
            continue;
        }

        CBudgetProposal* pbudgetProposal =  budget.FindProposal(budgetPayment.nProposalHash);
        if(!pbudgetProposal){
            if(retBadHashes == ""){
                retBadHashes = "Unknown proposal hash! Check this proposal before voting" + budgetPayment.nProposalHash.ToString();
            } else {
                retBadHashes += "," + budgetPayment.nProposalHash.ToString();
            }
        } else {
            if(pbudgetProposal->GetPayee() != budgetPayment.payee || pbudgetProposal->GetAmount() != budgetPayment.nAmount)
            {
                if(retBadPayeeOrAmount == ""){
                    retBadPayeeOrAmount = "Budget payee/nAmount doesn't match our proposal! " + budgetPayment.nProposalHash.ToString();
                } else {
                    retBadPayeeOrAmount += "," + budgetPayment.nProposalHash.ToString();
                }
            }
        }
    }

    if(retBadHashes == "" && retBadPayeeOrAmount == "") return "OK";

    return retBadHashes + retBadPayeeOrAmount;
}

bool CFinalizedBudget::IsValid(std::string& strError, bool fCheckCollateral)
{
    //must be the correct block for payment to happen (once a month)
    if(nBlockStart % GetBudgetPaymentCycleBlocks() != 0) {strError = "Invalid BlockStart"; return false;}
    if(GetBlockEnd() - nBlockStart > 100) {strError = "Invalid BlockEnd"; return false;}
    if((int)vecBudgetPayments.size() > 100) {strError = "Invalid budget payments count (too many)"; return false;}
    if(strBudgetName == "") {strError = "Invalid Budget Name"; return false;}
    if(nBlockStart == 0) {strError = "Invalid BlockStart == 0"; return false;}
    if(nFeeTXHash == 0) {strError = "Invalid FeeTx == 0"; return false;}

    //can only pay out 10% of the possible coins (min value of coins)
    if(GetTotalPayout() > budget.GetTotalBudget(nBlockStart)) {strError = "Invalid Payout (more than max)"; return false;}

    std::string strError2 = "";
    if(fCheckCollateral){
        int nConf = 0;
        if(!IsBudgetCollateralValid(nFeeTXHash, GetHash(), strError2, nTime, nConf)){
            {strError = "Invalid Collateral : " + strError2; return false;}
        }
    }

    //TODO: if N cycles old, invalid, invalid

    CBlockIndex* pindexPrev = pindexBest;
    if(pindexPrev == NULL) return true;

    if(nBlockStart < pindexPrev->nHeight-100) {strError = "Older than current blockHeight"; return false;}

    return true;
}

bool CFinalizedBudget::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    int nCurrentBudgetPayment = nBlockHeight - GetBlockStart();
    if(nCurrentBudgetPayment < 0) {
        LogPrintf("CFinalizedBudget::IsTransactionValid - Invalid block - height: %d start: %d\n", nBlockHeight, GetBlockStart());
        return false;
    }

    if(nCurrentBudgetPayment > (int)vecBudgetPayments.size() - 1) {
        LogPrintf("CFinalizedBudget::IsTransactionValid - Invalid block - current budget payment: %d of %d\n", nCurrentBudgetPayment + 1, (int)vecBudgetPayments.size());
        return false;
    }

    bool found = false;
    BOOST_FOREACH(CTxOut out, txNew.vout)
    {
        if(vecBudgetPayments[nCurrentBudgetPayment].payee == out.scriptPubKey && vecBudgetPayments[nCurrentBudgetPayment].nAmount == out.nValue)
            found = true;
    }
    if(!found) {
        CTxDestination address1;
        ExtractDestination(vecBudgetPayments[nCurrentBudgetPayment].payee, address1);
        CDarkSilkAddress address2(address1);

        LogPrintf("CFinalizedBudget::IsTransactionValid - Missing required payment - %s: %d\n", address2.ToString(), vecBudgetPayments[nCurrentBudgetPayment].nAmount);
    }

    return found;
}

void CFinalizedBudget::SubmitVote()
{
    CPubKey pubKeyStormnode;
    CKey keyStormnode;
    std::string errorMessage;

    if(!sandStormSigner.SetKey(strStormNodePrivKey, errorMessage, keyStormnode, pubKeyStormnode)){
        LogPrintf("CFinalizedBudget::SubmitVote - Error upon calling SetKey\n");
        return;
    }

    CFinalizedBudgetVote vote(activeStormnode.vin, GetHash());
    if(!vote.Sign(keyStormnode, pubKeyStormnode)){
        LogPrintf("CFinalizedBudget::SubmitVote - Failure to sign.");
        return;
    }

    std::string strError = "";
    if(budget.UpdateFinalizedBudget(vote, NULL, strError)){
        LogPrintf("CFinalizedBudget::SubmitVote  - new finalized budget vote - %s\n", vote.GetHash().ToString());

        budget.mapSeenFinalizedBudgetVotes.insert(make_pair(vote.GetHash(), vote));
        vote.Relay();
    } else {
        LogPrintf("CFinalizedBudget::SubmitVote : Error submitting vote - %s\n", strError);
    }
}

CFinalizedBudgetBroadcast::CFinalizedBudgetBroadcast()
{
    strBudgetName = "";
    nBlockStart = 0;
    vecBudgetPayments.clear();
    mapVotes.clear();
    vchSig.clear();
    nFeeTXHash = 0;
}

CFinalizedBudgetBroadcast::CFinalizedBudgetBroadcast(const CFinalizedBudget& other)
{
    strBudgetName = other.strBudgetName;
    nBlockStart = other.nBlockStart;
    BOOST_FOREACH(CTxBudgetPayment out, other.vecBudgetPayments) vecBudgetPayments.push_back(out);
    mapVotes = other.mapVotes;
    nFeeTXHash = other.nFeeTXHash;
}

CFinalizedBudgetBroadcast::CFinalizedBudgetBroadcast(std::string strBudgetNameIn, int nBlockStartIn, std::vector<CTxBudgetPayment> vecBudgetPaymentsIn, uint256 nFeeTXHashIn)
{
    strBudgetName = strBudgetNameIn;
    nBlockStart = nBlockStartIn;
    BOOST_FOREACH(CTxBudgetPayment out, vecBudgetPaymentsIn) vecBudgetPayments.push_back(out);
    mapVotes.clear();
    nFeeTXHash = nFeeTXHashIn;
}

void CFinalizedBudgetBroadcast::Relay()
{
    CInv inv(MSG_BUDGET_FINALIZED, GetHash());
    RelayInv(inv, MIN_BUDGET_PEER_PROTO_VERSION);
}

CFinalizedBudgetVote::CFinalizedBudgetVote()
{
    vin = CTxIn();
    nBudgetHash = 0;
    nTime = 0;
    vchSig.clear();
    fValid = true;
    fSynced = false;
}

CFinalizedBudgetVote::CFinalizedBudgetVote(CTxIn vinIn, uint256 nBudgetHashIn)
{
    vin = vinIn;
    nBudgetHash = nBudgetHashIn;
    nTime = GetAdjustedTime();
    vchSig.clear();
    fValid = true;
    fSynced = false;
}

void CFinalizedBudgetVote::Relay()
{
    CInv inv(MSG_BUDGET_FINALIZED_VOTE, GetHash());
    RelayInv(inv, MIN_BUDGET_PEER_PROTO_VERSION);
}

bool CFinalizedBudgetVote::Sign(CKey& keyStormnode, CPubKey& pubKeyStormnode)
{
    // Choose coins to use
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;

    std::string errorMessage;
    std::string strMessage = vin.prevout.ToStringShort() + nBudgetHash.ToString() + boost::lexical_cast<std::string>(nTime);

    if(!sandStormSigner.SignMessage(strMessage, errorMessage, vchSig, keyStormnode)) {
        LogPrintf("CFinalizedBudgetVote::Sign - Error upon calling SignMessage");
        return false;
    }

    if(!sandStormSigner.VerifyMessage(pubKeyStormnode, vchSig, strMessage, errorMessage)) {
        LogPrintf("CFinalizedBudgetVote::Sign - Error upon calling VerifyMessage");
        return false;
    }

    return true;
}

bool CFinalizedBudgetVote::SignatureValid(bool fSignatureCheck)
{
    std::string errorMessage;

    std::string strMessage = vin.prevout.ToStringShort() + nBudgetHash.ToString() + boost::lexical_cast<std::string>(nTime);

    CStormnode* psn = snodeman.Find(vin);

    if(psn == NULL)
    {
        LogPrintf("CFinalizedBudgetVote::SignatureValid() - Unknown Stormnode\n");
        return false;
    }

    if(!fSignatureCheck) return true;

    if(!sandStormSigner.VerifyMessage(psn->pubkey2, vchSig, strMessage, errorMessage)) {
        LogPrintf("CFinalizedBudgetVote::SignatureValid() - Verify message failed\n");
        return false;
    }

    return true;
}

std::string CBudgetManager::ToString() const
{
    std::ostringstream info;

    info << "Proposals: " << (int)mapProposals.size() <<
            ", Budgets: " << (int)mapFinalizedBudgets.size() <<
            ", Seen Budgets: " << (int)mapSeenStormnodeBudgetProposals.size() <<
            ", Seen Budget Votes: " << (int)mapSeenStormnodeBudgetVotes.size() <<
            ", Seen Final Budgets: " << (int)mapSeenFinalizedBudgets.size() <<
            ", Seen Final Budget Votes: " << (int)mapSeenFinalizedBudgetVotes.size();

    return info.str();
}
