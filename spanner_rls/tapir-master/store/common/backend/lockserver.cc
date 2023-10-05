// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * spanstore/lockserver.cc:
 *   Simple multi-reader, single-writer lock server
 *
 **********************************************************************/

#include "store/common/backend/lockserver.h"
#include "algorithm"

using namespace std;

LockServer::LockServer()
{
    readers = 0;
    writers = 0;
}
    
LockServer::~LockServer() { }

bool
LockServer::Waiter::checkTimeout(const struct timeval &now)
{
    Debug("checktimeout: now:%ld.%06ld, wait:%ld.%06ld", now.tv_sec, now.tv_usec, waitTime.tv_sec, waitTime.tv_usec);
    if (now.tv_sec > waitTime.tv_sec) {
        if ((now.tv_sec - waitTime.tv_sec) * 1000000 + now.tv_usec - waitTime.tv_usec > LOCK_WAIT_TIMEOUT)
            return true;
    } else {
        ASSERT(now.tv_usec > waitTime.tv_usec && now.tv_sec == waitTime.tv_sec);
        
        if (now.tv_usec - waitTime.tv_usec > LOCK_WAIT_TIMEOUT)
            return true;
    }
    return false;
}
    
void
LockServer::Lock::waitForLock(uint64_t requester, bool write)
{
    if (waiters.find(requester) != waiters.end()) {
        // Already waiting
        return;
    }

    Debug("[%lu] Adding me to the queue ...", requester);
    // Otherwise

    waiters[requester] = Waiter(write);
    waitQ.push_back(requester);
}

void
LockServer::Lock::waitForPreLock(uint64_t requester, bool write)
{
    if (prewaiters.find(requester) != prewaiters.end()) {
        // Already waiting
        return;
    }

    Debug("[%lu] Adding me to the prequeue ...", requester);
    // Otherwise

    prewaiters[requester] = Waiter(write);
    prewaitQ.push_back(requester);
}

bool
LockServer::Lock::tryAcquireLock(uint64_t requester, bool write)
{
    if (waitQ.size() == 0) {
        return true;
    }

    Debug("[%lu] Trying to get lock for %d", requester, (int)write);
    struct timeval now;
    uint64_t w = waitQ.front();
    
    gettimeofday(&now, NULL);
    //Debug("[%lu] reday for tryaccquirelock checkTimeout", w);
    // prune old requests out of the wait queue

    if (write) {
        if (waitQ.front() == requester) {
            // this lock is being reserved for the requester
            waitQ.erase(waitQ.begin());
            ASSERT(waiters.find(requester) != waiters.end());
            ASSERT(waiters[requester].write == write);
            waiters.erase(requester);
            return true;
        } else {
            // otherwise, add me to the list
            waitForLock(requester, write);
            return false;
        }
    } else {
        return !isWriteBefore(requester);
    }
    Assert(0);
    return false;
}

bool
LockServer::Lock::tryAcquirePreLock(uint64_t requester, bool write)
{
    if (prewaitQ.size() == 0) {
        return true;
    }

    map<uint64_t, Waiter>::iterator iter3;
    for(iter3 = prewaiters.begin(); iter3 != prewaiters.end(); iter3++){
        Debug("1:prewaiters: %lu and waittime in tryAcquirePreLock:%ld.%06ld", iter3->first, iter3->second.waitTime.tv_sec, iter3->second.waitTime.tv_usec);
    }
    
    Debug("[%lu] Trying to get prelock", requester);
    struct timeval now;
    
    gettimeofday(&now, NULL);
    // prune old requests out of the wait queue

    if (write) {
        if (prewaitQ.front() == requester) {
            // this lock is being reserved for the requester
            prewaitQ.erase(prewaitQ.begin());
            prewaiters.erase(requester);
            return true;
        } else {
            // otherwise, add me to the list
            waitForPreLock(requester, write);
            return false;
        }
    } else {
        return !isPreWriteBefore(requester);
    }

    Assert(0);
    return false;
}

bool
LockServer::Lock::isWriteBefore(uint64_t requester)
{
    if (waitQ.size() == 0) return false;

    //struct timeval now;

    Debug("reday for isWriteBefore checkTimeout");

    for (int i = 0; i < waitQ.size(); i++) {
        if (waiters[waitQ[i]].write) {
            return true;
        }

        if (waitQ[i] == requester) {
            return false;
        }
    }

    //ASSERT(waiters.find(waitQ.front()) != waiters.end());
    return true;
}

bool
LockServer::Lock::isPreWriteBefore(uint64_t requester)
{
    if (prewaitQ.size() == 0) return false;

    //struct timeval now;

    Debug("reday for isWriteBefore checkTimeout");

    for (int i = 0; i < prewaitQ.size(); i++) {
        if (prewaiters[prewaitQ[i]].write) {
            return true;
        }

        if (prewaitQ[i] == requester) {
            return false;
        }
    }
    Assert(0);
    //ASSERT(waiters.find(waitQ.front()) != waiters.end());
    return true;
}

bool
LockServer::lockForPreRead(const string &lock, uint64_t requester)
{
    Lock &l = locks[lock];
    Debug("[%lu] prestate: %d, Lock for Read: %s [%lu %lu]", requester, l.prestate, lock.c_str(),
            l.preholders.size(), l.prewaiters.size());

    map<uint64_t, bool>::iterator iter;
    for(iter = l.preholders.begin(); iter != l.preholders.end(); iter++){
        Debug("preholders: %lu", iter->first);
    }

    switch (l.prestate) {
    case UNLOCKED:
        // if you are next in the queue

        if (l.tryAcquirePreLock(requester, false)) {
            Debug("[%lu] I have acquired the read lock %s!", requester, lock.c_str());

            l.prestate = LOCKED_FOR_READ;
            ASSERT(l.preholders.size() == 0);
            l.preholders.insert(std::make_pair(requester, false));

            l.prewaiters.erase(requester);
            l.prewaitQ.erase(remove(l.prewaitQ.begin(), l.prewaitQ.end(), requester), l.prewaitQ.end());

            return true;
        }
        return false;
    case LOCKED_FOR_READ:
        // if you already hold this lock
        if (l.preholders.count(requester) > 0) {
            Debug("[%lu] I already hold this read lock %s!", requester, lock.c_str());

            l.prewaiters.erase(requester);
            l.prewaitQ.erase(remove(l.prewaitQ.begin(), l.prewaitQ.end(), requester), l.prewaitQ.end());

            return true;
        }

        // There is a write waiting, let's give up the lock
        if (l.isPreWriteBefore(requester)) {
	        Debug("[%lu] Waiting on lock %s because there is a pending prewrite request", requester, lock.c_str());
            l.waitForPreLock(requester, false);
            return false;
        }

        l.preholders.insert(std::make_pair(requester, false));

        l.prewaiters.erase(requester);
        l.prewaitQ.erase(remove(l.prewaitQ.begin(), l.prewaitQ.end(), requester), l.prewaitQ.end());

        Debug("[%lu] I have acquired the read lock %s with others!", requester, lock.c_str());
        return true;
    case LOCKED_FOR_WRITE:
    case LOCKED_FOR_READ_WRITE:

        if (l.preholders.count(requester) > 0) {
            Debug("[%lu] I already hold this preread lock %s!", requester, lock.c_str());
            l.prestate = LOCKED_FOR_READ_WRITE;
            return true;
        }
        ASSERT(l.preholders.size() == 1);
        Debug("%s Locked for prewrite, held by %lu", lock.c_str(), l.preholders.begin()->first); 
        l.waitForPreLock(requester, false);
        return false;
    }
    NOT_REACHABLE();
    return false;
}


bool
LockServer::lockForRead(const string &lock, uint64_t requester)
{
    Lock &l = locks[lock];
    Debug("[%lu] state: %d, Lock for Read: %s [%lu %lu]", requester, l.state, lock.c_str(),
             l.holders.size(), l.waiters.size());

    unordered_set<uint64_t>::iterator iter;
    for(iter = l.holders.begin(); iter != l.holders.end(); iter++){
        Debug("holders: %lu", *iter);
    }

    map<uint64_t, bool>::iterator preholder;
    for(preholder = l.preholders.begin(); preholder != l.preholders.end(); preholder++){
        if (preholder->second) {
            l.waitForLock(requester, false);
            return false;
        }
    }

    switch (l.state) {
    case UNLOCKED:
        // if you are next in the queue

        if (l.tryAcquireLock(requester, false)) {
            Debug("[%lu] I have acquired the read lock %s!", requester, lock.c_str());

            l.state = LOCKED_FOR_READ;
            ASSERT(l.holders.size() == 0);
            l.holders.insert(requester);

            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());

            return true;
        }
        return false;
    case LOCKED_FOR_READ:
        // if you already hold this lock
        if (l.holders.count(requester) > 0) {
            Debug("[%lu] I already hold this read lock %s!", requester, lock.c_str());

            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());

            return true;
        }

        // There is a write waiting, let's give up the lock
        if (l.isWriteBefore(requester)) {
	        Debug("[%lu] Waiting on lock %s because there is a pending write request", requester, lock.c_str());
            l.waitForLock(requester, false);
            return false;
        }

        l.holders.insert(requester);

        l.waiters.erase(requester);
        l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());

        Debug("[%lu] I have acquired the read lock %s with others!", requester, lock.c_str());
        return true;
    case LOCKED_FOR_WRITE:
    case LOCKED_FOR_READ_WRITE:

        if (l.holders.count(requester) > 0) {
            Debug("[%lu] I already hold this read lock %s!", requester, lock.c_str());
            l.state = LOCKED_FOR_READ_WRITE;
            return true;
        }
        ASSERT(l.holders.size() == 1);
        Debug("%s Locked for write, held by %lu", lock.c_str(), *(l.holders.begin())); 
        l.waitForLock(requester, false);
        return false;
    }
    NOT_REACHABLE();
    return false;
}

bool
LockServer::lockForPreWrite(const string &lock, uint64_t requester)
{
    
    Lock &l = locks[lock];
    
    Debug("Lock for PreWrite: %s [%lu %lu]", lock.c_str(),
    l.holders.size(), l.waiters.size());

    map<uint64_t, bool>::iterator iter;
    for(iter = l.preholders.begin(); iter != l.preholders.end(); iter++){
        Debug("preholders: %lu", iter->first);
    }

    switch (l.prestate) {
    case UNLOCKED:
        // Got it!
        if (l.tryAcquirePreLock(requester, true)) {
            Debug("[%lu] I have acquired the prewrite lock in unlocked!", requester);
            /*if (l.preholders.first != 0) {
                Debug("gain!!!!!! and l.preholders: %lu", l.preholders.first);
            }*/
            l.prestate = LOCKED_FOR_WRITE;
            ASSERT(l.preholders.size() == 0);
            l.preholders.insert(std::make_pair(requester, false));

            l.prewaiters.erase(requester);
            l.prewaitQ.erase(remove(l.prewaitQ.begin(), l.prewaitQ.end(), requester), l.prewaitQ.end());
            return true;
        }
        return false;
    case LOCKED_FOR_READ:
        if (l.preholders.size() == 1 && l.preholders.count(requester) > 0) {
            Debug("[%lu] I have acquired the prewrite lock in locked for preread!", requester);
            // if there is one holder of this read lock and it is the
            // requester, then upgrade the lock
            l.prestate = LOCKED_FOR_READ_WRITE;

            l.prewaiters.erase(requester);
            l.prewaitQ.erase(remove(l.prewaitQ.begin(), l.prewaitQ.end(), requester), l.prewaitQ.end());

            return true;
        }

        Debug(" Locked for preread by %s %lu other people %s", l.preholders.count(requester) > 0 ? "you" : "", l.preholders.size(), lock.c_str());
        l.waitForPreLock(requester, true);
        return false;
    case LOCKED_FOR_WRITE:
    case LOCKED_FOR_READ_WRITE:
        ASSERT(l.preholders.size() == 1);
        if (l.preholders.count(requester) > 0) {
            Debug("[%lu] I have already acquired the prewrite lock!", requester);
            return true;
        }

        Debug("%s Held by %lu for %s", lock.c_str(), l.preholders.begin()->first, (l.prestate == LOCKED_FOR_WRITE) ? "write" : "read-write" );
        l.waitForPreLock(requester, true);
        return false;
    }
}



bool
LockServer::lockForWrite(const string &lock, uint64_t requester)
{
    Lock &l = locks[lock];

    Debug("[%lu] state: %d, Lock for Write: %s [%lu %lu]", requester, l.state, lock.c_str(),
    l.holders.size(), l.waiters.size());

    unordered_set<uint64_t>::iterator iter;
    for(iter = l.holders.begin(); iter != l.holders.end(); iter++){
        Debug("holders: %lu", *iter);
    }
    
    map<uint64_t, bool>::iterator preholder;
    for(preholder = l.preholders.begin(); preholder != l.preholders.end(); preholder++){
        if (preholder->second) {
            l.waitForLock(requester, true);
            return false;
        }
    }

    switch (l.state) {
    case UNLOCKED:
        // Got it!
        if (l.tryAcquireLock(requester, true)) {
            Debug("[%lu] I have acquired the write lock in unlocked!", requester);
            /*if (l.preholders.first != 0) {
                Debug("gain!!!!!! and l.preholders: %lu", l.preholders.first);
            }*/
            l.state = LOCKED_FOR_WRITE;
            ASSERT(l.holders.size() == 0);
            l.holders.insert(requester);

            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());
            return true;
        }
        return false;
    case LOCKED_FOR_READ:
        if (l.holders.size() == 1 && l.holders.count(requester) > 0) {
            Debug("[%lu] I have acquired the write lock in locked for read!", requester);
            // if there is one holder of this read lock and it is the
            // requester, then upgrade the lock
            l.state = LOCKED_FOR_READ_WRITE;

            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());

            return true;
        }

        Debug(" Locked for read by %s %lu other people %s", l.holders.count(requester) > 0 ? "you" : "", l.holders.size(), lock.c_str());
        l.waitForLock(requester, true);
        return false;
    case LOCKED_FOR_WRITE:
    case LOCKED_FOR_READ_WRITE:
        ASSERT(l.holders.size() == 1);
        if (l.holders.count(requester) > 0) {
            Debug("[%lu] I have already acquired the write lock!", requester);
            return true;
        }

        Debug("%s Held by %lu for %s", lock.c_str(), *(l.holders.begin()), (l.state == LOCKED_FOR_WRITE) ? "write" : "read-write" );
        l.waitForLock(requester, true);
        return false;
    }
    NOT_REACHABLE();
    return false;
}

uint64_t
LockServer::releaseForPreRead(const string &lock, uint64_t holder)
{
    if (locks.find(lock) == locks.end()) {
        Debug("[%lu] could not find lock: %s", holder, lock.c_str());
        return 0;
    }

    Lock &l = locks[lock];
    Debug("[%lu] prestate: %d, Lock for releaseForPreRead: %s [%lu %lu]", holder, l.prestate, lock.c_str(),
    l.preholders.size(), l.prewaiters.size());

    map<uint64_t, bool>::iterator iter;
    for(iter = l.preholders.begin(); iter != l.preholders.end(); iter++){
        Debug("preholders: %lu", iter->first);
    }

    for (int k = 0; k < l.prewaitQ.size(); k++) {
        Debug("prewaitQ: %lu", l.prewaitQ[k]);
    }

    map<uint64_t, Waiter>::iterator iter2;
    for(iter2 = l.prewaiters.begin(); iter2 != l.prewaiters.end(); iter2++){
        Debug("prewaiters: %lu", iter2->first);
    }

    int n = l.prewaiters.erase(holder);
    if (n) {
        Debug("erase holder %lu in releaseForPreRead", holder);
    }
    l.prewaitQ.erase(remove(l.prewaitQ.begin(), l.prewaitQ.end(), holder), l.prewaitQ.end());

    if (l.preholders.count(holder) == 0) {
        Debug("[%lu] Releasing unheld preread lock: %s", holder, lock.c_str());
        return 0;
    }

    switch (l.prestate) {
    case UNLOCKED:
    case LOCKED_FOR_WRITE:
        Warning("[%lu] prestate is wrong in lock: %s", holder, lock.c_str());
        return 0;
    case LOCKED_FOR_READ:
        if (l.preholders.erase(holder) < 1) {
            Warning("[%lu] Releasing unheld preread lock: %s", holder, lock.c_str()); 
        }
        if (l.preholders.empty()) {
            l.prestate = UNLOCKED;
        }
	    return 0;
    case LOCKED_FOR_READ_WRITE:
        l.prestate = LOCKED_FOR_WRITE;
        return 0;
    }
}

uint64_t
LockServer::releaseForRead(const string &lock, uint64_t holder)
{
    if (locks.find(lock) == locks.end()) {
        Debug("[%lu] could not find lock: %s", holder, lock.c_str());
        return 0;
    }

    Lock &l = locks[lock];
    Debug("[%lu] state: %d, Lock for releaseForRead: %s [%lu %lu]", holder, l.state, lock.c_str(),
    l.holders.size(), l.waiters.size());

    unordered_set<uint64_t>::iterator iter;
    for(iter = l.holders.begin(); iter != l.holders.end(); iter++){
        Debug("holders: %lu", *iter);
    }

    for (int k = 0; k < l.waitQ.size(); k++) {
        Debug("waitQ: %lu", l.waitQ[k]);
    }

    map<uint64_t, Waiter>::iterator iter2;
    for(iter2 = l.waiters.begin(); iter2 != l.waiters.end(); iter2++){
        Debug("waiters: %lu", iter2->first);
    }

    int n = l.waiters.erase(holder);
    if (n) {
        Debug("erase holder %lu in releaseForRead", holder);
    }
    l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), holder), l.waitQ.end());

    if (l.holders.count(holder) == 0) {
        Debug("[%lu] Releasing unheld read lock: %s", holder, lock.c_str());
        return 0;
    }

    switch (l.state) {
    case UNLOCKED:
    case LOCKED_FOR_WRITE:
        Warning("[%lu] state is wrong in lock: %s", holder, lock.c_str());
        return 0;
    case LOCKED_FOR_READ:
        if (l.holders.erase(holder) < 1) {
            Warning("[%lu] Releasing unheld read lock: %s", holder, lock.c_str()); 
        }
        if (l.holders.empty()) {
            l.state = UNLOCKED;
            map<uint64_t, bool>::iterator preholder;
            for(preholder = l.preholders.begin(); preholder != l.preholders.end(); preholder++){
                if (preholder->second) {
                    uint64_t ret = preholder->first;
                    l.preholders.erase(preholder);
                    return ret;
                }
            }
            return 0;
        }
	    return 0;
    case LOCKED_FOR_READ_WRITE:
        l.state = LOCKED_FOR_WRITE;
        return 0;
    }
}

uint64_t
LockServer::releaseForWrite(const string &lock, uint64_t holder)
{
    if (locks.find(lock) == locks.end()) {
        Debug("[%lu] could not find lock: %s", holder, lock.c_str());
        return false;
    }
    Lock &l = locks[lock];
    Debug("[%lu] state: %d, Lock for releaseForWrite: %s [%lu %lu]", holder, l.state, lock.c_str(),
    l.holders.size(), l.waiters.size());

    unordered_set<uint64_t>::iterator iter;
    for(iter = l.holders.begin(); iter != l.holders.end(); iter++){
        Debug("holders: %lu", *iter);
    }

    for (int k = 0; k < l.waitQ.size(); k++) {
        Debug("waitQ: %lu", l.waitQ[k]);
    }

    map<uint64_t, Waiter>::iterator iter2;
    for(iter2 = l.waiters.begin(); iter2 != l.waiters.end(); iter2++){
        Debug("waiters: %lu", iter2->first);
    }

    int n = l.waiters.erase(holder);
    if (n) {
        Debug("erase holder %lu in releaseForWrite", holder);
    }
    l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), holder), l.waitQ.end());

    if (l.holders.count(holder) == 0) {
        Debug("[%lu] Releasing unheld write lock: %s", holder, lock.c_str());
        return 0;
    }

    map<uint64_t, bool>::iterator iter3;
    for(iter3 = l.preholders.begin(); iter3 != l.preholders.end(); iter3++){
        Debug("preholders: %lu", iter3->first);
    }

    switch (l.state) {
    case UNLOCKED:
    case LOCKED_FOR_READ:
        return 0;
    case LOCKED_FOR_WRITE:{
        l.holders.erase(holder);
        l.state = UNLOCKED;
        
        map<uint64_t, bool>::iterator preholder;
        for(preholder = l.preholders.begin(); preholder != l.preholders.end(); preholder++){
            if (preholder->second) {
                uint64_t ret = preholder->first;
                l.preholders.erase(preholder);
                return ret;
            }
        }
        return 0;}
    case LOCKED_FOR_READ_WRITE:
        l.state = LOCKED_FOR_READ;
        ASSERT(l.holders.size() == 1);
        return 0;
    }
}

uint64_t
LockServer::releaseForPreWrite(const string &lock, uint64_t holder)
{
    if (locks.find(lock) == locks.end()) {
        Debug("[%lu] could not find prelock: %s", holder, lock.c_str());
        return false;
    }
    Lock &l = locks[lock];

    Debug("[%lu] prestate: %d, Lock for releaseForPreWrite: %s [%lu %lu]", holder, l.prestate, lock.c_str(),
    l.preholders.size(), l.waiters.size());

    for (int k = 0; k < l.prewaitQ.size(); k++) {
        Debug("prewaitQ: %lu", l.prewaitQ[k]);
    }
    
    map<uint64_t, Waiter>::iterator iter2;
    for(iter2 = l.prewaiters.begin(); iter2 != l.prewaiters.end(); iter2++){
        Debug("1:prewaiters: %lu and waittime in releaseForPreWrite:%ld.%06ld", iter2->first, iter2->second.waitTime.tv_sec, iter2->second.waitTime.tv_usec);
    }

    /*for(iter2 = l.prewaiters.begin(); iter2 != l.prewaiters.end(); iter2++){
        Debug("2:prewaiters: %lu and waittime in releaseForPreWrite:%ld.%06ld", iter2->first, iter2->second.waitTime.tv_sec, iter2->second.waitTime.tv_usec);
    }*/

    int n = l.prewaiters.erase(holder);
    if (n) {
        Debug("erase holder %lu in releaseForPreWrite", holder);
    }

    l.prewaitQ.erase(remove(l.prewaitQ.begin(), l.prewaitQ.end(), holder), l.prewaitQ.end());

    if (l.preholders.count(holder) == 0) {
        Debug("[%lu] Releasing unheld prewrite lock: %s", holder, lock.c_str());
        return 0;
    }

    switch (l.prestate) {
    case UNLOCKED:
    case LOCKED_FOR_READ:
        return 0;
    case LOCKED_FOR_WRITE:
        l.prestate = UNLOCKED;

        if (l.preholders[holder]) {
            Debug("release preholders and l.preholders.first: %lu l.preholders.second:%d", holder, l.preholders[holder]);
            uint64_t ret = holder;
            l.preholders.erase(holder);
            return ret;
        } else {
            Debug("[%lu] have aborted and releasing preholders lock: %s", holder, lock.c_str());
            l.preholders.erase(holder);
            return false;
        }
        
    case LOCKED_FOR_READ_WRITE:
        l.prestate = LOCKED_FOR_READ;
        ASSERT(l.preholders.size() == 1);
        return 0;
    }
    ASSERT(0);
    return 0;
}

void
LockServer::readyForCommit(const string &lock, uint64_t requester)
{
    Lock &l = locks[lock];
    l.preholders[requester] = true;
}