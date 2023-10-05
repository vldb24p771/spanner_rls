// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/strongstore/lockstore.h:
 *   Key-value store with support for strong consistency using S2PL
 *
 * Copyright 2013-2015 Irene Zhang <iyzhang@cs.washington.edu>
 *                     Naveen Kr. Sharma <naveenks@cs.washington.edu>
 *                     Dan R. K. Ports  <drkp@cs.washington.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "store/strongstore/lockstore.h"

using namespace std;

namespace strongstore {

LockStore::LockStore() : TxnStore(), store() { }
LockStore::~LockStore() { }

int
LockStore::Get(uint64_t id, const Transaction &txn, pair<Timestamp, string> &value)
{
    //Debug("[%lu] GET %s", id, key.c_str());
    string val;

    int status = REPLY_OK;
    for (auto &read : txn.getReadSet()) {

        if (!store.get(read.first, val)) {
            // couldn't find the key
            Debug("[%lu] Could not find the key", id);
            return REPLY_FAIL;
        }
        Debug("[%lu] prepare with read txn %s in lockstore", id, read.first.c_str());

        if (locks.lockForRead(read.first, id)) {
            value = make_pair(Timestamp(), val);
        } else {
            Debug("[%lu] Could not acquire read lock", id);
            status = REPLY_RETRY;
        }
    }
    return status;
}

int
LockStore::Get(uint64_t id, const Transaction &txn, const Timestamp &timestamp, pair<Timestamp, string> &value)
{
    return Get(id, txn, value);
}

int
LockStore::Prepare(uint64_t id, const Transaction &txn, bool InorCross)
{    
    //Debug("[%lu] START PREPARE", id);
    //txn.addReadSet(1, 1);
    for (auto &write : txn.getWriteSet()) {
        Debug("[%lu] prepare with write txn %s in lockstore", id, write.first.c_str());
    }
    for (auto &read : txn.getReadSet()) {
        Debug("[%lu] prepare with read txn %s in lockstore", id, read.first.c_str());
    }
    if (prepared.size() > 100) {
        Warning("Lots of prepared transactions! %lu", prepared.size());
    }

    if (prepared.find(id) != prepared.end()) {
        Debug("[%lu] Already prepared", id);
        return REPLY_OK;
    }

    if (getLocks(id, txn, InorCross)) {
        prepared[id] = txn;
        Debug("[%lu] Get commit locks", id);
        return REPLY_OK;
    } else {
        Debug("[%lu] Could not acquire locks", id);
        return REPLY_RETRY;
    }
}

void
LockStore::Commit(uint64_t id, uint64_t timestamp, bool InorCross)
{
    Transaction txn = prepared[id];
    if (InorCross == true) {
        for (auto &write : txn.getWriteSet()) {
            store.put(write.first, write.second);
        } 
    } else {
        for (auto &write : txn.getWriteSet()) {
            locks.readyForCommit(write.first, id);
            precommit[write.first] = write.second;
        } 
    }
    dropLocks(id, txn, InorCross);
    prepared.erase(id);
    Debug("[%lu] Get commit locks", id);
}

void
LockStore::Abort(uint64_t id, const Transaction &txn, bool InorCross)
{
    for (auto &write : txn.getWriteSet()) {
        Debug("[%lu] abort with write txn %s in lockstore", id, write.first.c_str());
    }
    for (auto &read : txn.getReadSet()) {
        Debug("[%lu] abort with read txn %s in lockstore", id, read.first.c_str());
    }
    //Debug("[%lu] ABORT", id);
    dropLocks(id, txn, InorCross);
    prepared.erase(id);
}

void
LockStore::Load(const string &key, const string &value, const Timestamp &timestamp)
{
    store.put(key, value);
}

/* Used on commit and abort for second phase of 2PL. */
void
LockStore::dropLocks(uint64_t id, const Transaction &txn, bool InorCross)
{
    //Debug("[%lu] I'm dropping locks", id);
    uint64_t state = 0;

    if (InorCross) {
        for (auto &read : txn.getReadSet()) {
            state = locks.releaseForRead(read.first, id);
            if (state != 0) {
                store.put(read.first, precommit[read.first]);
            }
        }

        for (auto &write : txn.getWriteSet()) {
            state = locks.releaseForWrite(write.first, id);
            if (state != 0) {
                store.put(write.first, precommit[write.first]);
            }
        }
    } else {
        for (auto &read : txn.getReadSet()) {
            locks.releaseForPreRead(read.first, id);
        }

        for (auto &write : txn.getWriteSet()) {
            state = locks.releaseForPreWrite(write.first, id);
            if (state != 0) {
                store.put(write.first, precommit[write.first]);
            }
        }
    }
}

bool
LockStore::getLocks(uint64_t id, const Transaction &txn, bool InorCross)
{
    bool ret = true;
    // if we don't have read locks, get read locks
    //fprintf(stderr, "debug: getlocks");

    if(InorCross == true) {
        for (auto &read : txn.getReadSet()) {
            Debug("[%lu] I'm getting read lock", id);
            if (!locks.lockForRead(read.first, id)) {
                Debug("[%lu] I could not get read lock", id);
                ret = false;
            }
        }

        for (auto &write : txn.getWriteSet()) {
            Debug("[%lu] I'm getting inwrite lock", id);
            if (!locks.lockForWrite(write.first, id)) {
                Debug("[%lu] I could not get inwrite lock", id);
                ret = false;
            }
        }
    } else {
        for (auto &read : txn.getReadSet()) {
            Debug("[%lu] I'm getting pre crossread lock", id);
            if (!locks.lockForPreRead(read.first, id)) {
                Debug("[%lu] I could not get preread lock", id);
                ret = false;
            }
        }

        for (auto &write : txn.getWriteSet()) {
            Debug("[%lu] I'm getting pre crosswrite lock", id);
            if (!locks.lockForPreWrite(write.first, id)) {
                Debug("[%lu] I could not get prewrite lock", id);
                ret = false;
            }
        }
    }
    return ret;
}

} // namespace strongstore
