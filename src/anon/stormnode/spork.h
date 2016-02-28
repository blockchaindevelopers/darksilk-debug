// Copyright (c) 2015-2016 Silk Network
// Copyright (c) 2009-2015 The Dash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef SPORK_H
#define SPORK_H

#include "bignum.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "script/script.h"
#include "base58.h"
#include "main.h"

using namespace std;
using namespace boost;

// Don't ever reuse these IDs for other sporks
static const unsigned int SPORK_START = 10001;
static const unsigned int SPORK_END = 10012;

static const unsigned int SPORK_1_STORMNODE_PAYMENTS_ENFORCEMENT = 10000;
static const unsigned int SPORK_2_INSTANTX = 10001;
static const unsigned int SPORK_3_INSTANTX_BLOCK_FILTERING = 10002;
static const unsigned int SPORK_5_MAX_VALUE = 10004;
static const unsigned int SPORK_7_STORMNODE_SCANNING = 10006;
static const unsigned int SPORK_8_STORMNODE_PAYMENT_ENFORCEMENT = 10007;
static const unsigned int SPORK_9_STORMNODE_BUDGET_ENFORCEMENT = 10008;
static const unsigned int SPORK_10_STORMNODE_PAY_UPDATED_NODES = 10009;
static const unsigned int SPORK_11_RESET_BUDGET = 10010;
static const unsigned int SPORK_12_RECONSIDER_BLOCKS = 10011;
static const unsigned int SPORK_13_ENABLE_SUPERBLOCKS = 10012;

static const int SPORK_1_STORMNODE_PAYMENTS_ENFORCEMENT_DEFAULT = 1446335999;  //Wed, 31 Oct 2015 23:59:59 GMT
static const int SPORK_2_INSTANTX_DEFAULT = 978307200;   //2001-01-01
static const int SPORK_3_INSTANTX_BLOCK_FILTERING_DEFAULT = 1424217600;  //2015-02-18
static const unsigned int SPORK_5_MAX_VALUE_DEFAULT = 1000;        //1000 DRKSLK
static const int SPORK_7_STORMNODE_SCANNING_DEFAULT = 978307200;   //2001-01-01
static const int SPORK_8_STORMNODE_PAYMENT_ENFORCEMENT_DEFAULT = 1434326400;   //2015-06-15
static const int SPORK_9_STORMNODE_BUDGET_ENFORCEMENT_DEFAULT = 1434326400;   //2015-06-15

class CSporkMessage;
class CSporkManager;

#include "bignum.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "protocol.h"
#include "anon/sandstorm/sandstorm.h"
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

extern std::map<uint256, CSporkMessage> mapSporks;
extern std::map<int, CSporkMessage> mapSporksActive;
extern CSporkManager sporkManager;

void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
int GetSporkValue(int nSporkID);
bool IsSporkActive(int nSporkID);
void ExecuteSpork(int nSporkID, int nValue);
void ReprocessBlocks(int nBlocks);

//
// Spork Class
// Keeps track of all of the network spork settings
//

class CSporkMessage
{
public:
    std::vector<unsigned char> vchSig;
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;

    uint256 GetHash(){
        uint256 n = Hash(BEGIN(nSporkID), END(nTimeSigned));
        return n;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        //unsigned int nSerSize = 0;
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
        READWRITE(vchSig);
	}
};


class CSporkManager
{
private:
    std::vector<unsigned char> vchSig;

    std::string strMasterPrivKey;
    std::string strMainPubKey;

public:

    CSporkManager() {
        strMainPubKey = "";
    }

    std::string GetSporkNameByID(int id);
    int GetSporkIDByName(std::string strName);
    bool UpdateSpork(int nSporkID, int64_t nValue);
    bool SetPrivKey(std::string strPrivKey);
    bool CheckSignature(CSporkMessage& spork);
    bool Sign(CSporkMessage& spork);
    void Relay(CSporkMessage& msg);

};

#endif
