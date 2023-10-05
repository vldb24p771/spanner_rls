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
        for (int i = 0; i < waitQ.size(); i++) {
            if (waitQ[i] == requester) {
                return true;
            }
            if (waiters[waitQ[i]].write) {
                return false;
            }
        }
    }
    return true;
}

bool
LockServer::Lock::isWriteNext(uint64_t requester)
{
    if (waitQ.size() == 0) return false;

    //struct timeval now;
    //uint64_t w = waitQ.front();
    
    //gettimeofday(&now, NULL);
    // prune old requests out of the wait queue

    Debug("reday for isWriteNext checkTimeout");

    for (int i = 0; i < waitQ.size(); i++) {
        if (waiters[waitQ[i]].write) {
            return true;
        }

        if (waitQ[i] == requester) {
            return false;
        }
    }

    /*while (waiters[w].checkTimeout(now)) {
        Debug("erase waiter %lu in iswritenext", w);
        waiters.erase(w);
        waitQ.erase(waitQ.begin());

        // if everyone else was old ...
        if (waitQ.size() == 0) {
            return false;
        }

        w = waitQ.front();
        ASSERT(waiters.find(w) != waiters.end());
    }*/

    //ASSERT(waiters.find(waitQ.front()) != waiters.end());
    return true;
}

bool
LockServer::lockForRead(const string &lock, uint64_t requester)
{
    Lock &l = locks[lock];
    Debug("Lock for Read: %s [%lu %lu %lu %lu]", lock.c_str(),
            readers, writers, l.holders.size(), l.waiters.size());

    Debug("[%lu] state: %d, Lock for Write: %s [%lu %lu %lu %lu]", requester, l.state, lock.c_str(),
    readers, writers, l.holders.size(), l.waiters.size());
    unordered_set<uint64_t>::iterator iter;
    for(iter = l.holders.begin(); iter != l.holders.end(); iter++){
        Debug("holders: %lu", *iter);
    }

    switch (l.state) {
    case UNLOCKED:
        // if you are next in the queue
        if (l.tryAcquireLock(requester, false)) {
            Debug("[%lu] I have acquired the read lock!", requester);
            l.state = LOCKED_FOR_READ;
            ASSERT(l.holders.size() == 0);
            l.holders.insert(requester);
            readers++;
            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());
            return true;
        }
        return false;
    case LOCKED_FOR_READ:
        // if you already hold this lock
        if (l.holders.find(requester) != l.holders.end()) {
            Debug("[%lu] I already hold this read lock %s!", requester, lock.c_str());
            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());
            return true;
        }

        // There is a write waiting, let's give up the lock
        if (l.isWriteNext(requester)) {
	    Debug("[%lu] Waiting on lock because there is a pending write request", requester);
            l.waitForLock(requester, false);
            return false;
        }

        l.holders.insert(requester);
        Debug("[%lu] I have acquired the read lock %s with others!", requester, lock.c_str());
        readers++;
        l.waiters.erase(requester);
        l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());
        return true;
    case LOCKED_FOR_WRITE:
    case LOCKED_FOR_READ_WRITE:
        if (l.holders.count(requester) > 0) {
            l.state = LOCKED_FOR_READ_WRITE;
            readers++;
            return true;
        }
        ASSERT(l.holders.size() == 1);
        Debug("Locked for write, held by %lu", *(l.holders.begin())); 
        l.waitForLock(requester, false);
        return false;
    }
    NOT_REACHABLE();
    return false;
}

bool
LockServer::lockForWrite(const string &lock, uint64_t requester)
{
    Lock &l = locks[lock];

    Debug("Lock for Write: %s [%lu %lu %lu %lu]", lock.c_str(),
    readers, writers, l.holders.size(), l.waiters.size());

    Debug("[%lu] state: %d, Lock for Write: %s [%lu %lu %lu %lu]", requester, l.state, lock.c_str(),
    readers, writers, l.holders.size(), l.waiters.size());
    unordered_set<uint64_t>::iterator iter;
    for(iter = l.holders.begin(); iter != l.holders.end(); iter++){
        Debug("holders: %lu", *iter);
    }

    switch (l.state) {
    case UNLOCKED:
        // Got it!
        if (l.tryAcquireLock(requester, true)) {
            Debug("[%lu] I have acquired the write lock!", requester);
            l.state = LOCKED_FOR_WRITE;
            ASSERT(l.holders.size() == 0);
            l.holders.insert(requester);
            writers++;

            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());
            return true;
        }
        return false;
    case LOCKED_FOR_READ:
        if (l.holders.size() == 1 && l.holders.count(requester) > 0) {
            // if there is one holder of this read lock and it is the
            // requester, then upgrade the lock
            l.state = LOCKED_FOR_READ_WRITE;
            writers++;

            l.waiters.erase(requester);
            l.waitQ.erase(remove(l.waitQ.begin(), l.waitQ.end(), requester), l.waitQ.end());
            return true;
        }

        Debug("Locked for read by%s%lu other people", l.holders.count(requester) > 0 ? "you" : "", l.holders.size());
        l.waitForLock(requester, true);
        return false;
    case LOCKED_FOR_WRITE:
    case LOCKED_FOR_READ_WRITE:
        ASSERT(l.holders.size() == 1);
        if (l.holders.count(requester) > 0) {
            return true;
        }

        Debug("Held by %lu for %s", *(l.holders.begin()), (l.state == LOCKED_FOR_WRITE) ? "write" : "read-write" );
        l.waitForLock(requester, true);
        return false;
    }
    NOT_REACHABLE();
    return false;
}

void
LockServer::releaseForRead(const string &lock, uint64_t holder)
{
    if (locks.find(lock) == locks.end()) {
        return;
    }
    
    Lock &l = locks[lock];

    Debug("[%lu] state: %d, Lock for releaseForWrite: %s [%lu %lu %lu %lu]", holder, l.state, lock.c_str(),
    readers, writers, l.holders.size(), l.waiters.size());
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
        Warning("[%ld] Releasing unheld read lock: %s", holder, lock.c_str());
        return;
    }

    switch (l.state) {
    case UNLOCKED:
    case LOCKED_FOR_WRITE:
        return;
    case LOCKED_FOR_READ:
        Debug("read release read lock %s", lock.c_str());
        readers--;
        if (l.holders.erase(holder) < 1) {
            Warning("[%ld] Releasing unheld read lock: %s", holder, lock.c_str()); 
        }
        if (l.holders.empty()) {
            l.state = UNLOCKED;
        }
	return;
    case LOCKED_FOR_READ_WRITE:
        Debug("read release read write lock %s", lock.c_str());
        readers--;
        l.state = LOCKED_FOR_WRITE;
        return;
    }
}

void
LockServer::releaseForWrite(const string &lock, uint64_t holder)
{
    if (locks.find(lock) == locks.end()) {
        return;
    }

    Lock &l = locks[lock];

    Debug("[%lu] state: %d, Lock for releaseForWrite: %s [%lu %lu %lu %lu]", holder, l.state, lock.c_str(),
    readers, writers, l.holders.size(), l.waiters.size());
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
        Warning("[%ld] Releasing unheld write lock: %s", holder, lock.c_str());
        return;
    }

    switch (l.state) {
    case UNLOCKED:
    case LOCKED_FOR_READ:
        return;
    case LOCKED_FOR_WRITE:
        writers--;
        l.holders.erase(holder);
        ASSERT(l.holders.size() == 0);
        l.state = UNLOCKED;
        return;
    case LOCKED_FOR_READ_WRITE:
        writers--;
        l.state = LOCKED_FOR_READ;
        ASSERT(l.holders.size() == 1);
        return;
    }
}

