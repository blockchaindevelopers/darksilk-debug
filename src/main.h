// Copyright (c) 2009-2016 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Developers
// Copyright (c) 2015-2016 Silk Network
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DARKSILK_MAIN_H
#define DARKSILK_MAIN_H

#include <boost/unordered_map.hpp>

#include <list>

#include "txmempool.h"
#include "net.h"
#include "txdb.h"
#include "chain.h"

class CValidationState;
class CWallet;
class CTxMemPool;

// Minimum Transaction Fee of 0.00001 DRKSLK, Fees smaller than this are considered zero fee (for transaction creation)
static const double MIN_FEE = 0.00001;
// Collateral Amount Locked for Stormnodes
static const CAmount STORMNODE_COLLATERAL = 10000;
// Main Stormnode Payments Start Block
static const int STORMNODE_PAYMENT_START = 420;
// Testnet Stormnode Payment Start Block
static const int TESTNET_STORMNODE_PAYMENT_START = 100;
// Sandstorm Collateral Payment
static const CAmount SANDSTORM_COLLATERAL = (0.01*COIN);
// Sandstorm Pool Max Amount
static const CAmount SANDSTORM_POOL_MAX = (9999.99*COIN);
// Static Proof-of-Stake Reward of 0.01 DRKSLK
static const CAmount STATIC_POS_REWARD = COIN * 0.01;
// Number of blocks that can be requested at any given time from a single peer.
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 128;
// Timeout in seconds before considering a block download peer unresponsive.
static const unsigned int BLOCK_DOWNLOAD_TIMEOUT = 60;
// The maximum size for mined blocks (50% OF MAX_BLOCK_SIZE)
static const unsigned int MAX_BLOCK_SIZE_GEN = MAX_BLOCK_SIZE/2;
// Default for -blockprioritysize, maximum space for zero/low-fee transactions
static const unsigned int DEFAULT_BLOCK_PRIORITY_SIZE = 50000;
// The maximum size for transactions we're willing to relay/mine
static const unsigned int MAX_STANDARD_TX_SIZE = MAX_BLOCK_SIZE_GEN/5;
// The maximum allowed number of signature check operations in a block (network rule)
static const unsigned int MAX_BLOCK_SIGOPS = MAX_BLOCK_SIZE/50;
// Maxiumum number of signature check operations in an IsStandard() P2SH script
static const unsigned int MAX_P2SH_SIGOPS = 15;
// The maximum number of sigops we're willing to relay/mine in a single tx
static const unsigned int MAX_TX_SIGOPS = MAX_BLOCK_SIGOPS/5;
// The maximum number of orphan transactions kept in memory
static const unsigned int MAX_ORPHAN_TRANSACTIONS = MAX_BLOCK_SIZE/100;
// Default for -maxorphanblocksmib, maximum memory used by orphan blocks
static const unsigned int DEFAULT_MAX_ORPHAN_BLOCKS = 512;
/// The maximum number of entries in an 'inv' protocol message
static const unsigned int MAX_INV_SZ = 50000;
// Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX timestamp.
static const unsigned int LOCKTIME_THRESHOLD = 500000000; // Tue Nov 5th 00:53:20 1985 UTC
// Target timing between Proof-of-Work blocks
static const unsigned int POW_TARGET_SPACING = 1 * 60; // 60 seconds
// Target timing between Proof-of-Stake blocks
static const unsigned int POS_TARGET_SPACING = 1 * 64; // 64 seconds
// Time to wait (in seconds) between writing blockchain state to disk.
static const unsigned int DATABASE_WRITE_INTERVAL = 3600;
// Maximum length of "REJECT" messages
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
// "REJECT" message codes
static const unsigned char REJECT_MALFORMED = 0x01;
static const unsigned char REJECT_INVALID = 0x10;
static const unsigned char REJECT_OBSOLETE = 0x11;
static const unsigned char REJECT_DUPLICATE = 0x12;
static const unsigned char REJECT_NONSTANDARD = 0x40;
static const unsigned char REJECT_DUST = 0x41;
static const unsigned char REJECT_INSUFFICIENTFEE = 0x42;
static const unsigned char REJECT_CHECKPOINT = 0x43;

struct BlockHasher
{
    size_t operator()(const uint256& hash) const { return hash.GetLow64(); }
};

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
typedef std::map<uint256, CBlockIndex*, BlockHasher> BlockMap; //TODO (Amir): Change to boost::unordered_map.
extern std::map<uint256, CBlockIndex*> mapBlockIndex;
extern std::set<std::pair<COutPoint, unsigned int> > setStakeSeen;
extern CBlockIndex* pindexGenesisBlock;
extern int nStakeMinConfirmations;
extern unsigned int nStakeMinAge;
extern unsigned int nNodeLifespan;
extern int nCoinbaseMaturity;
extern int nBestHeight;
extern uint256 nBestChainTrust;
extern uint256 nBestInvalidTrust;
extern uint256 hashBestChain;
extern CBlockIndex* pindexBest;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern int64_t nLastCoinStakeSearchInterval;
extern const std::string strMessageMagic;
extern int64_t nTimeBestReceived;
extern CConditionVariable cvBlockChange;
extern bool fImporting;
extern bool fReindex;
struct COrphanBlock;
extern std::map<uint256, COrphanBlock*> mapOrphanBlocks;
extern bool fTxIndex;
extern unsigned int nCoinCacheSize;
// Settings
extern bool fUseFastIndex;
extern unsigned int nDerivationMethodIndex;

extern bool fMinimizeCoinAge;

extern bool fLargeWorkForkFound;
extern bool fLargeWorkInvalidChainFound;

extern std::map<uint256, int64_t> mapRejectedBlocks;

extern CFeeRate minRelayTxFee;

// Minimum disk space required - used in CheckDiskSpace()
static const uint64_t nMinDiskSpace = 52428800;

class CTxDB;
class CTxIndex;
class CWalletInterface;

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);

/** Register a wallet to receive updates from core */
void RegisterWallet(CWalletInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterWallet(CWalletInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllWallets();
/** Push an updated transaction to all registered wallets */
void SyncWithWallets(const CTransaction& tx, const CBlock* pblock = NULL, bool fConnect = true);
/** Ask wallets to resend their transactions */
void ResendWalletTransactions(bool fForce = false);

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

void PushGetBlocks(CNode* pnode, CBlockIndex* pindexBegin, uint256 hashEnd);

bool ProcessBlock(CNode* pfrom, CBlock* pblock);
bool CheckDiskSpace(uint64_t nAdditionalBytes=0);
/// Unload database information
void UnloadBlockIndex();
/// Load the block tree and coins database from disk */
bool LoadBlockIndex(bool fAllowNew=true);
/// Initialize a new block tree database + block data on disk
bool InitBlockIndex();
void PrintBlockTree();


bool ProcessMessages(CNode* pfrom);
bool SendMessages(CNode* pto, bool fSendTrickle);
void ThreadImport(std::vector<boost::filesystem::path> vImportFiles);

bool CheckProofOfWork(uint256 hash, unsigned int nBits);
unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, bool fProofOfStake);
CAmount GetProofOfWorkReward(CAmount nFees);

bool IsConfirmedInNPrevBlocks(const CTxIndex& txindex, const CBlockIndex* pindexFrom, int nMaxDepth, int& nActualDepth);
std::string GetWarnings(std::string strFor);
bool GetTransaction(const uint256 &hash, CTransaction &tx, uint256 &hashBlock);
uint256 WantedByOrphan(const COrphanBlock* pblockOrphan);
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake);
void ThreadStakeMiner(CWallet *pwallet);

/// (try to) add transaction to memory pool
//bool AcceptToMemoryPool(CTxMemPool& pool, CTransaction &tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee=false, bool isSSTX=false);
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, CTransaction &tx, bool fLimitFree, bool* pfMissingInputs, bool ignoreFees = false);
bool AcceptableInputs(CTxMemPool& pool, CValidationState &state, CTransaction &tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee=false, bool isSSTX=false);

CAmount GetBlockValue(int nBits, int nHeight, const CAmount& nFees);

bool FindTransactionsByDestination(const CTxDestination &dest, std::vector<uint256> &vtxhash);

int GetInputAge(CTxIn& vin);
/// Abort with a message
bool AbortNode(const std::string &msg, const std::string &userMessage="");
/// Increase a node's misbehavior score.
void Misbehaving(NodeId nodeid, int howmuch);

CAmount GetStormnodePayment(int nHeight, CAmount blockValue);

int GetInputAgeIX(uint256 nTXHash, CTxIn& vin);
int GetIXConfirmations(uint256 nTXHash);

bool DisconnectBlocksAndReprocess(int blocks);
//void static FlushBlockFile(bool fFinalize = false);
void FlushStateToDisk();

boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix);
FILE* OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode);
FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly = false); //TODO (Amir): Is is okay to use without nFile?
FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly = false);
FILE* AppendBlockFile(unsigned int& nFileRet);
FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);



/// Position on disk for a particular transaction.
class CDiskTxPos
{
public:
    unsigned int nFile;
    unsigned int nBlockPos;
    unsigned int nTxPos;

    CDiskTxPos()
    {
        SetNull();
    }

    CDiskTxPos(unsigned int nFileIn, unsigned int nBlockPosIn, unsigned int nTxPosIn)
    {
        nFile = nFileIn;
        nBlockPos = nBlockPosIn;
        nTxPos = nTxPosIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(FLATDATA(*this));
    }

    void SetNull() { nFile = (unsigned int) -1; nBlockPos = 0; nTxPos = 0; }
    bool IsNull() const { return (nFile == (unsigned int) -1); }

    friend bool operator==(const CDiskTxPos& a, const CDiskTxPos& b)
    {
        return (a.nFile     == b.nFile &&
                a.nBlockPos == b.nBlockPos &&
                a.nTxPos    == b.nTxPos);
    }

    friend bool operator!=(const CDiskTxPos& a, const CDiskTxPos& b)
    {
        return !(a == b);
    }


    std::string ToString() const
    {
        if (IsNull())
            return "null";
        else
            return strprintf("(nFile=%u, nBlockPos=%u, nTxPos=%u)", nFile, nBlockPos, nTxPos);
    }
};

enum GetMinFee_mode
{
    GMF_BLOCK,
    GMF_RELAY,
    GMF_SEND,
};

typedef std::map<uint256, std::pair<CTxIndex, CTransaction> > MapPrevTx;

CAmount GetMinFee(const CTransaction& tx, unsigned int nBytes, bool fAllowFree, enum GetMinFee_mode mode);

class CTransactionPoS
{
public:
    static const int CURRENT_VERSION = 1;
    int nVersion;

    CTransactionPoS(){ nVersion = CURRENT_VERSION; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
    }

    CAmount GetValueOut(CTransaction& tx) const;

    ///  Amount of darksilks coming in to this transaction
    ///  Note that lightweight clients may not know anything besides the hash of previous transactions,
    ///  so may not be able to calculate this.
    //    @param[in] mapInputs    Map of previous transactions that have outputs we're spending
    //    @return Sum of value of all inputs (scriptSigs)
    //    @see CTransaction::FetchInputs
    CAmount GetValueIn(CTransaction& tx, const MapPrevTx& mapInputs) const;

    bool ReadFromDisk(CTransaction& tx, CDiskTxPos pos, FILE** pfileRet=NULL);
    bool ReadFromDisk(CTransaction& tx, CTxDB& txdb, const uint256& hash, CTxIndex& txindexRet);
    bool ReadFromDisk(CTransaction& tx, CTxDB& txdb, COutPoint prevout, CTxIndex& txindexRet);
    bool ReadFromDisk(CTransaction& tx, CTxDB& txdb, COutPoint prevout);
    bool ReadFromDisk(CTransaction& tx, COutPoint prevout);
    bool DisconnectInputs(CTransaction& tx, CTxDB& txdb);

    /// Fetch from memory and/or disk. inputsRet keys are transaction hashes.
    //   @param[in] txdb    Transaction database
    //   @param[in] mapTestPool List of pending changes to the transaction index database
    //   @param[in] fBlock  True if being called to add a new best-block to the chain
    //   @param[in] fMiner  True if being called by CreateNewBlock
    //   @param[out] inputsRet  Pointers to this transaction's inputs
    //   @param[out] fInvalid   returns true if transaction is invalid
    //   @return    Returns true if all inputs are in txdb or mapTestPool
    bool FetchInputs(CTransaction& tx, CTxDB& txdb, const std::map<uint256, CTxIndex>& mapTestPool,
                     bool fBlock, bool fMiner, MapPrevTx& inputsRet, bool& fInvalid);

    /// Sanity check previous transactions, then, if all checks succeed,
    /// mark them as spent by this transaction.
    //    @param[in] inputs   Previous transactions (from FetchInputs)
    //    @param[out] mapTestPool Keeps track of inputs that need to be updated on disk
    //    @param[in] posThisTx    Position of this transaction on disk
    //    @param[in] pindexBlock
    //    @param[in] fBlock   true if called from ConnectBlock
    //    @param[in] fMiner   true if called from CreateNewBlock
    //    @return Returns true if all checks succeed
    bool ConnectInputs(CTransaction& tx, CTxDB& txdb, MapPrevTx inputs,
                       std::map<uint256, CTxIndex>& mapTestPool, const CDiskTxPos& posThisTx,
                       const CBlockIndex* pindexBlock, bool fBlock, bool fMiner, unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS, bool fValidateSig = true);

    bool GetCoinAge(CTransaction& tx, CTxDB& txdb, const CBlockIndex* pindexPrev, uint64_t& nCoinAge) const;

    const CTxOut& GetOutputFor(const CTxIn& input, const MapPrevTx& inputs) const;


};

/** Check for standard transaction types
    @param[in] mapInputs    Map of previous transactions that have outputs we're spending
    @return True if all inputs (scriptSigs) use only standard transaction forms
    @see CTransaction::FetchInputs
*/
bool AreInputsStandard(const CTransaction& tx, const MapPrevTx& mapInputs);

/** Count ECDSA signature operations the old-fashioned (pre-0.6) way
    @return number of sigops this transaction's outputs will produce when spent
    @see CTransaction::FetchInputs
*/
unsigned int GetLegacySigOpCount(const CTransaction& tx);

/** Count ECDSA signature operations in pay-to-script-hash inputs.

    @param[in] mapInputs    Map of previous transactions that have outputs we're spending
    @return maximum number of sigops required to validate this transaction's inputs
    @see CTransaction::FetchInputs
 */
unsigned int GetP2SHSigOpCount(const CTransaction& tx, const MapPrevTx& mapInputs);

/** Check for standard transaction types
    @return True if all outputs (scriptPubKeys) use only standard transaction forms
*/
bool IsStandardTx(const CTransaction& tx, std::string& reason);

bool IsFinalTx(const CTransaction &tx, int nBlockHeight = 0, int64_t nBlockTime = 0);

/** A transaction with a merkle branch linking it to the block chain. */
class CMerkleTx : public CTransaction
{
private:
    int GetDepthInMainChainINTERNAL(CBlockIndex* &pindexRet) const;
public:
    uint256 hashBlock;
    std::vector<uint256> vMerkleBranch;
    int nIndex;

    // memory only
    mutable bool fMerkleVerified;


    CMerkleTx()
    {
        Init();
    }

    CMerkleTx(const CTransaction& txIn) : CTransaction(txIn)
    {
        Init();
    }

    void Init()
    {
        hashBlock = 0;
        nIndex = -1;
        fMerkleVerified = false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        //TODO (Amir): Review translation below.
        //nSerSize += SerReadWrite(s, *(CTransaction*)this, nType, nVersion, ser_action);
        READWRITE(*(CTransaction*)this);
        nVersion = this->nVersion;
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
    }

    int SetMerkleBranch(const CBlock* pblock=NULL);

    // Return depth of transaction in blockchain:
    // -1  : not in blockchain, and not in memory pool (conflicted transaction)
    //  0  : in memory pool, waiting to be included in a block
    // >=1 : this many blocks deep in the main chain
    int GetDepthInMainChain(CBlockIndex* &pindexRet, bool enableIX=true) const;
    int GetDepthInMainChain(bool enableIX=true) const { CBlockIndex *pindexRet; return GetDepthInMainChain(pindexRet, enableIX); }
    bool IsInMainChain() const { CBlockIndex *pindexRet; return GetDepthInMainChainINTERNAL(pindexRet) > 0; }
    int GetBlocksToMaturity() const;
    bool AcceptToMemoryPool(bool fLimitFree=true, bool fRejectInsaneFee=true, bool ignoreFees=false);
    int GetTransactionLockSignatures() const;
    bool IsTransactionLockTimedOut() const;
    
};

/**  A txdb record that contains the disk location of a transaction and the
 * locations of transactions that spend its outputs.  vSpent is really only
 * used as a flag, but having the location is very helpful for debugging.
 */
class CTxIndex
{
public:
    CDiskTxPos pos;
    std::vector<CDiskTxPos> vSpent;

    CTxIndex()
    {
        SetNull();
    }

    CTxIndex(const CDiskTxPos& posIn, unsigned int nOutputs)
    {
        pos = posIn;
        vSpent.resize(nOutputs);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(pos);
        READWRITE(vSpent);
    }

    void SetNull()
    {
        pos.SetNull();
        vSpent.clear();
    }

    bool IsNull()
    {
        return pos.IsNull();
    }

    friend bool operator==(const CTxIndex& a, const CTxIndex& b)
    {
        return (a.pos    == b.pos &&
                a.vSpent == b.vSpent);
    }

    friend bool operator!=(const CTxIndex& a, const CTxIndex& b)
    {
        return !(a == b);
    }
    int GetDepthInMainChain() const;

};

/** Capture information about block/transaction validation */
class CValidationState {
private:
    enum mode_state {
        MODE_VALID,   //! everything ok
        MODE_INVALID, //! network rule violation (DoS value may be set)
        MODE_ERROR,   //! run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    unsigned char chRejectCode;
    bool corruptionPossible;
public:
    CValidationState() : mode(MODE_VALID), nDoS(0), chRejectCode(0), corruptionPossible(false) {}
    bool DoS(int level, bool ret = false,
             unsigned char chRejectCodeIn=0, std::string strRejectReasonIn="",
             bool corruptionIn=false) {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        corruptionPossible = corruptionIn;
        if (mode == MODE_ERROR)
            return ret;
        nDoS += level;
        mode = MODE_INVALID;
        return ret;
    }
    bool Invalid(bool ret = false,
                 unsigned char _chRejectCode=0, std::string _strRejectReason="") {
        return DoS(0, ret, _chRejectCode, _strRejectReason);
    }
    bool Error(std::string strRejectReasonIn="") {
        if (mode == MODE_VALID)
            strRejectReason = strRejectReasonIn;
        mode = MODE_ERROR;
        return false;
    }
    bool Abort(const std::string &msg) {
        AbortNode(msg);
        return Error(msg);
    }
    bool IsValid() const {
        return mode == MODE_VALID;
    }
    bool IsInvalid() const {
        return mode == MODE_INVALID;
    }
    bool IsError() const {
        return mode == MODE_ERROR;
    }
    bool IsInvalid(int &nDoSOut) const {
        if (IsInvalid()) {
            nDoSOut = nDoS;
            return true;
        }
        return false;
    }
    bool CorruptionPossible() const {
        return corruptionPossible;
    }
    unsigned char GetRejectCode() const { return chRejectCode; }
    std::string GetRejectReason() const { return strRejectReason; }
};

class CWalletInterface {
protected:
    virtual void SyncTransaction(const CTransaction &tx, const CBlock *pblock, bool fConnect) =0;
    virtual void EraseFromWallet(const uint256 &hash) =0;
    virtual void SetBestChain(const CBlockLocator &locator) =0;
    virtual bool UpdatedTransaction(const uint256 &hash) =0;
    virtual void Inventory(const uint256 &hash) =0;
    virtual void ResendWalletTransactions(bool fForce) =0;
    friend void ::RegisterWallet(CWalletInterface*);
    friend void ::UnregisterWallet(CWalletInterface*);
    friend void ::UnregisterAllWallets();
};

class CBlockTreeDB;
class CCoinsViewCache;

/// Global variable that points to the active CCoinsView (protected by cs_main)
extern CCoinsViewCache *pcoinsTip;

/// Global variable that points to the active block tree (protected by cs_main)
extern CBlockTreeDB *pblocktree;

class CBlockFileInfo
{
public:
    unsigned int nBlocks;      //! number of blocks stored in file
    unsigned int nSize;        //! number of used bytes of block file
    unsigned int nUndoSize;    //! number of used bytes in the undo file
    unsigned int nHeightFirst; //! lowest height of block in file
    unsigned int nHeightLast;  //! highest height of block in file
    uint64_t nTimeFirst;         //! earliest time of block in file
    uint64_t nTimeLast;          //! latest time of block in file

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSize));
        READWRITE(VARINT(nUndoSize));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
    }

    void SetNull() {
         nBlocks = 0;
         nSize = 0;
         nUndoSize = 0;
         nHeightFirst = 0;
         nHeightLast = 0;
         nTimeFirst = 0;
         nTimeLast = 0;
     }

     CBlockFileInfo() {
         SetNull();
     }

     std::string ToString() const;

     /** update statistics (does not update nSize) */
     void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn) {
         if (nBlocks==0 || nHeightFirst > nHeightIn)
             nHeightFirst = nHeightIn;
         if (nBlocks==0 || nTimeFirst > nTimeIn)
             nTimeFirst = nTimeIn;
         nBlocks++;
         if (nHeightIn > nHeightLast)
             nHeightLast = nHeightIn;
         if (nTimeIn > nTimeLast)
             nTimeLast = nTimeIn;
     }
};

class CCoinsView;
/// RAII wrapper for VerifyDB: Verify consistency of the block and coin databases
class CVerifyDB {
public:
    CVerifyDB();
    ~CVerifyDB();
    bool VerifyDB(CCoinsView *coinsview, int nCheckLevel, int nCheckDepth);
};

#endif
