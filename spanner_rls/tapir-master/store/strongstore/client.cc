// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/strongstore/client.cc:
 *   Client to transactional storage system with strong consistency
 *
 * Copyright 2015 Irene Zhang <iyzhang@cs.washington.edu>
 *                Naveen Kr. Sharma <naveenks@cs.washington.edu>
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

#include "store/strongstore/client.h"

using namespace std;

namespace strongstore {

Client::Client(string configPath, int nShards, int clientNumber,
                int closestReplica, TrueTime timeServer)
    : transport(0.0, 0.0, 0), mode(MODE_SPAN_LOCK), timeServer(timeServer)
{
    // Initialize all state here;
    client_id = 0;
    while (client_id == 0) {
        random_device rd;
        mt19937_64 gen(rd());
        uniform_int_distribution<uint64_t> dis;
        client_id = dis(gen);
    }
    t_id = (client_id/10000)*10000;

    nshards = nShards;
    bclient.reserve(nshards);

    Debug("Initializing SpanStore client with id [%lu]", client_id);

    /* Start a client for time stamp server. */
    if (mode == MODE_OCC) {
        string tssConfigPath = configPath + ".tss.config";
        ifstream tssConfigStream(tssConfigPath);
        if (tssConfigStream.fail()) {
            fprintf(stderr, "unable to read configuration file: %s\n",
                    tssConfigPath.c_str());
        }
        transport::Configuration tssConfig(tssConfigStream);
        tss = new replication::vr::VRClient(tssConfig, &transport, 0, clientNumber);
    }

    /* Start a client for each shard. */
    for (int i = 0; i < nShards; i++) {
        string shardConfigPath = configPath + to_string(i) + ".config";
        ShardClient *shardclient = new ShardClient(mode, shardConfigPath,
            &transport, client_id, i, clientNumber, closestReplica);
        bclient[i] = new BufferClient(shardclient);
    }

    /* Run the transport in a new thread. */
    clientTransport = new thread(&Client::run_client, this);

    Debug("SpanStore client [%lu] created!", client_id);
}

Client::~Client()
{
    transport.Stop();
    delete tss;
    for (auto b : bclient) {
        delete b;
    }
    clientTransport->join();
}

/* Runs the transport event loop. */
void
Client::run_client()
{
    transport.Run();
}

/* Begins a transaction. All subsequent operations before a commit() or
 * abort() are part of this transaction.
 *
 * Return a TID for the transaction.
 */
void
Client::Begin(vector<pair<string, string>> keylist, bool inorcross)
{
    //Debug("BEGIN Transaction");
    t_id++;
    participants.clear();
    commit_sleep = -1;
    Debug("inorcross in client: %d", inorcross);
    set<int> shardmap;
    for (int i = 0; i < keylist.size(); i++) {
        int tem = key_to_shard(keylist[i].first, nshards);
        if(shardmap.count(tem) == 0) {
            shardmap.insert(tem);
            bclient[tem]->Begin(t_id, inorcross);
        } 
    }
}

string
Client::Get(const string &key)
{
    string value;
    Get(key, value);
    return value;
}

/* Returns the value corresponding to the supplied key. */
int
Client::Get(const string &key, string &value)
{
    
    // Contact the appropriate shard to get the value.
    int i = key_to_shard(key, nshards);
    int status;

    // If needed, add this shard to set of participants and send BEGIN.
    if (participants.find(i) == participants.end()) {
        participants.insert(i);
    }

    // Send the GET operation to appropriate shard.
    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    
    for (int j = 0; j < GET_RETRIES; j++) {
        gettimeofday(&t1, NULL);
        int tem = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_usec - t0.tv_usec);

        if (tem > LOCK_TIMEOUT) {
            Debug("LOCK_TIMEOUT in GET");
            break;
        }

        Promise promise(GET_TIMEOUT);
        status = REPLY_OK;

        Debug("%d: GET [%lu : %s]", j, t_id, key.c_str());
        //bclient[i]->Get(key, &promise);

        status = promise.GetReply();
        value = promise.GetValue();
        if (status == REPLY_OK || status == REPLY_FAIL) {
            break;
        }
    }

    if (status != REPLY_OK) {
        Debug("Abort %s in GET", key.c_str());
        Abort();
    }

    return status;
}

/* Sets the value corresponding to the supplied key. */
int
Client::Put(const string &key, const string &value)
{
    // Contact the appropriate shard to set the value.
    Debug("PUT [%lu : %s]", t_id, key.c_str());
    int i = key_to_shard(key, nshards);

    // If needed, add this shard to set of participants and send BEGIN.
    if (participants.find(i) == participants.end()) {
        participants.insert(i);
    }

    Promise promise(PUT_TIMEOUT);
    
    // Buffering, so no need to wait.
    //bclient[i]->Put(key, value, &promise);
    return promise.GetReply();
}

bool
Client::PreCommit(vector<pair<string, string>> keylist)
{
    
    int tem;
    for (int i = 0; i < keylist.size(); i++) {
        tem = key_to_shard(keylist[i].first, nshards);

        if (participants.find(tem) == participants.end()) {
            participants.insert(tem);
        }

        //promises.push_back(new Promise(GET_TIMEOUT + i));
        if(keylist[i].second.empty() == false) {
            bclient[tem]->Put(keylist[i].first, keylist[i].second);
            Debug("[%s] PUT in shard [%d]", keylist[i].first.c_str(), tem);
        }
    }

    for (int i = 0; i < keylist.size(); i++) {
        tem = key_to_shard(keylist[i].first, nshards);

        if (participants.find(tem) == participants.end()) {
            participants.insert(tem);
        }

        //promises.push_back(new Promise(GET_TIMEOUT + i));
        if(keylist[i].second.empty() == true) {
            bclient[tem]->Get(keylist[i].first);
            Debug("[%s] GET in shard [%d]", keylist[i].first.c_str(), tem);
        }
    }
    return false;
}

int
Client::Prepare(uint64_t &ts)
{
    int status;

    // 1. Send commit-prepare to all shards.
    //Debug("PREPARE Transaction");
    list<Promise *> promises;

    for (auto p : participants) {
        Debug("Sending prepare to shard [%d]", p);
        promises.push_back(new Promise(PREPARE_TIMEOUT));
        bclient[p]->Prepare(Timestamp(), promises.back());
    }
    
    // 2. Wait for reply from all shards. (abort on timeout)
    //Debug("Waiting for PREPARE replies");

    status = REPLY_OK;
    for (auto p : promises) {
        // If any shard returned false, abort the transaction.
        if (p->GetReply() != REPLY_OK) {
            
            if (status != REPLY_FAIL) {
                status = p->GetReply();
            }
        }
        //Debug("GetReply in client: %d", p->GetReply());
        // Also, find the max of all prepare timestamp returned.
        if (p->GetTimestamp().getTimestamp() > ts) {
            ts = p->GetTimestamp().getTimestamp();
        }
        delete p;
    }
    return status;
}

/* Attempts to commit the ongoing transaction. */
bool
Client::Commit()
{
    // Implementing 2 Phase Commit
    uint64_t ts = 0;
    int status;

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);

    for (int i = 0; i < COMMIT_RETRIES; i++) {
        gettimeofday(&t1, NULL);
        int tem = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_usec - t0.tv_usec);
        if (tem > LOCK_TIMEOUT) {
            Debug("LOCK_TIMEOUT in COMMIT");
            break;
        }

        status = Prepare(ts);
        if (status == REPLY_OK || status == REPLY_FAIL) {
            break;
        }
    }

    if (status == REPLY_OK) {
        // For Spanner like systems, calculate timestamp.
        if (mode == MODE_SPAN_OCC || mode == MODE_SPAN_LOCK) {
            uint64_t now, err;
            struct timeval t1, t2;

            gettimeofday(&t1, NULL);
            timeServer.GetTimeAndError(now, err);

            if (now > ts) {
                ts = now;
            } else {
                uint64_t diff = ((ts >> 32) - (now >> 32))*1000000 +
                        ((ts & 0xffffffff) - (now & 0xffffffff));
                err += diff;
            }

            commit_sleep = (int)err;

            // how good are we at waking up on time?
            //Debug("Commit wait sleep: %lu", err);
            if (err > 1000000)
                Warning("Sleeping for too long! %lu; now,ts: %lu,%lu", err, now, ts);
            if (err > 150) {
                usleep(err-150);
            }
            // fine grained busy-wait
            while (1) {
                gettimeofday(&t2, NULL);
                if ((t2.tv_sec-t1.tv_sec)*1000000 +
                    (t2.tv_usec-t1.tv_usec) > (int64_t)err) {
                    break;
                }
            }
        }

        // Send commits
        //Debug("COMMIT Transaction at [%lu]", ts);

        for (auto p : participants) {
            Debug("Sending commit to shard [%d]", p);
            bclient[p]->Commit(ts);
        }
        return true;
        
    }
    Debug("the abort state: %d", status);
    // 4. If not, send abort to all shards.
    Abort();
    return false;
}

/* Aborts the ongoing transaction. */
void
Client::Abort()
{
    Debug("ABORT Transaction in client");
    for (auto p : participants) {
        bclient[p]->Abort();
    }
}

/* Return statistics of most recent transaction. */
vector<int>
Client::Stats()
{
    vector<int> v;
    return v;
}

/* Callback from a tss replica upon any request. */
void
Client::tssCallback(const string &request, const string &reply)
{
    lock_guard<mutex> lock(cv_m);
    Debug("Received TSS callback [%s]", reply.c_str());

    // Copy reply to "replica_reply".
    replica_reply = reply;
    
    // Wake up thread waiting for the reply.
    cv.notify_all();
}

} // namespace strongstore
