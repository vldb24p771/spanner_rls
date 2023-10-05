// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/benchmark/retwisClient.cc:
 *   Retwis benchmarking client for a distributed transactional store.
 *
 **********************************************************************/

#include "store/common/truetime.h"
#include "store/common/frontend/client.h"
#include "store/strongstore/client.h"
#include "store/weakstore/client.h"
#include "store/tapirstore/client.h"
#include <algorithm>
#include <utility>

using namespace std;

// Function to pick a random key according to some distribution.
int rand_key();
uint64_t key_to_shard(const std::string &key, uint64_t nshards) {
        uint64_t hash = 5381;
        const char* str = key.c_str();
        for (unsigned int i = 0; i < key.length(); i++) {
            hash = ((hash << 5) + hash) + (uint64_t)str[i];
        }

        return (hash % nshards);
};

bool ready = false;
double alpha = -1;
double *zipf;

vector<string> keys;
int nKeys = 100;

int
main(int argc, char **argv)
{
    const char *configPath = NULL;
    const char *keysPath = NULL;
    int duration = 10;
    int wPer = 50; // Out of 100
    int nShards = 1;
    int tLen = 10;
    int closestReplica = -1; // Closest replica id.
    int skew = 0; // difference between real clock and TrueTime
    int error = 0; // error bars
    int regionNumber = 0;
    int InPer = 50;
    int myRegion;

    Client *client;
    enum {
        MODE_UNKNOWN,
        MODE_TAPIR,
        MODE_WEAK,
        MODE_STRONG
    } mode = MODE_UNKNOWN;
    
    // Mode for strongstore.
    strongstore::Mode strongmode;

    int opt;
    while ((opt = getopt(argc, argv, "c:d:N:l:i:w:k:f:m:e:s:z:r:n:M:")) != -1) {
        switch (opt) {
        case 'c': // Configuration path
        { 
            configPath = optarg;
            break;
        }

        case 'f': // Generated keys path
        { 
            keysPath = optarg;
            break;
        }

        case 'N': // Number of shards.
        { 
            char *strtolPtr;
            nShards = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (nShards <= 0)) {
                fprintf(stderr, "option -N requires a numeric arg\n");
            }
            break;
        }

        case 'n': // Number of shards.
        { 
            char *strtolPtr;
            regionNumber = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (regionNumber < 0)) {
                fprintf(stderr, "option -n requires a numeric arg\n");
            }
            break;
        }

        case 'd': // Duration in seconds to run.
        { 
            char *strtolPtr;
            duration = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (duration <= 0)) {
                fprintf(stderr, "option -d requires a numeric arg\n");
            }
            break;
        }

        case 'l': // Length of each transaction (deterministic!)
        {
            char *strtolPtr;
            tLen = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (tLen <= 0)) {
                fprintf(stderr, "option -l requires a numeric arg\n");
            }
            break;
        }

        case 'i': // Percentage of in-region (out of 100)
        {
            char *strtolPtr;
            InPer = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (InPer < 0 || InPer > 100)) {
                fprintf(stderr, "option -i requires a arg b/w 0-100\n");
            }
            break;
        }

        case 'w': // Percentage of writes (out of 100)
        {
            char *strtolPtr;
            wPer = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (wPer < 0 || wPer > 100)) {
                fprintf(stderr, "option -w requires a arg b/w 0-100\n");
            }
            break;
        }

        case 'k': // Number of keys to operate on.
        {
            char *strtolPtr;
            nKeys = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (nKeys <= 0)) {
                fprintf(stderr, "option -k requires a numeric arg\n");
            }
            break;
        }

        case 's': // Simulated clock skew.
        {
            char *strtolPtr;
            skew = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') || (skew < 0))
            {
                fprintf(stderr,
                        "option -s requires a numeric arg\n");
            }
            break;
        }

        case 'e': // Simulated clock error.
        {
            char *strtolPtr;
            error = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') || (error < 0))
            {
                fprintf(stderr,
                        "option -e requires a numeric arg\n");
            }
            break;
        }

        case 'z': // Zipf coefficient for key selection.
        {
            char *strtolPtr;
            alpha = strtod(optarg, &strtolPtr);
            if ((*optarg == '\0') || (*strtolPtr != '\0'))
            {
                fprintf(stderr,
                        "option -z requires a numeric arg\n");
            }
            break;
        }

        case 'r': // Preferred closest replica.
        {
            char *strtolPtr;
            closestReplica = strtod(optarg, &strtolPtr);
            if ((*optarg == '\0') || (*strtolPtr != '\0'))
            {
                fprintf(stderr,
                        "option -r requires a numeric arg\n");
            }
            break;
        }

        case 'm': // Mode to run in [occ/lock/...]
        {
            if (strcasecmp(optarg, "txn-l") == 0) {
                mode = MODE_TAPIR;
            } else if (strcasecmp(optarg, "txn-s") == 0) {
                mode = MODE_TAPIR;
            } else if (strcasecmp(optarg, "qw") == 0) {
                mode = MODE_WEAK;
            } else if (strcasecmp(optarg, "occ") == 0) {
                mode = MODE_STRONG;
                strongmode = strongstore::MODE_OCC;
            } else if (strcasecmp(optarg, "lock") == 0) {
                mode = MODE_STRONG;
                strongmode = strongstore::MODE_LOCK;
            } else if (strcasecmp(optarg, "span-occ") == 0) {
                mode = MODE_STRONG;
                strongmode = strongstore::MODE_SPAN_OCC;
            } else if (strcasecmp(optarg, "span-lock") == 0) {
                mode = MODE_STRONG;
                strongmode = strongstore::MODE_SPAN_LOCK;
            } else {
                fprintf(stderr, "unknown mode '%s'\n", optarg);
                exit(0);
            }
            break;
        }

        case 'M': // Zipf coefficient for key selection.
        {
            char *strtolPtr;
            myRegion = strtod(optarg, &strtolPtr);
            if ((*optarg == '\0') || (*strtolPtr != '\0'))
            {
                fprintf(stderr,
                        "option -M requires a numeric arg\n");
            }
            break;
        }

        default:
            fprintf(stderr, "Unknown argument %s\n", argv[optind]);
            break;
        }
    }

    if (mode == MODE_TAPIR) {
        //client = new tapirstore::Client(configPath, nShards,
        //            closestReplica, TrueTime(skew, error));
    } else if (mode == MODE_WEAK) {
        //client = new weakstore::Client(configPath, nShards,
        //            closestReplica);
    } else if (mode == MODE_STRONG) {
        client = new strongstore::Client(configPath,
                    nShards, regionNumber, closestReplica, TrueTime(skew, error));
    } else {
        fprintf(stderr, "option -m is required\n");
        exit(0);
    }

    // Read in the keys from a file.
    string key, value;
    ifstream in;
    in.open(keysPath);
    if (!in) {
        fprintf(stderr, "Could not read keys from: %s\n", keysPath);
        exit(0);
    }
    for (int i = 0; i < nKeys; i++) {
        getline(in, key);
        keys.push_back(key);
    }
    in.close();

    struct timeval t0, t1, t2;
    int nTransactions = 0; // Number of transactions attempted.
    int ttype = 1; // Transaction type.
    int ret;
    bool status;
    vector<int> keyIdx;
    vector<pair<string, string>> keylist;
    struct timeval t5, t6;
    int tem;

    gettimeofday(&t0, NULL);
    srand(t0.tv_sec + t0.tv_usec);

    while (1) {
        keylist.clear();
        
        // Begin a transaction.

        // Decide which type of retwis transaction it is going to be.

        bool inorcross = (rand() % 100) < InPer;
        Debug("ttype: %d, clientnumber : %d, inorcross: %d", ttype, regionNumber, inorcross?1:0);
        if (1) {
            if (inorcross) {
                for (int i = 0; i < tLen; i++) {
                    //inregion
                    do {
                        tem = rand_key();
                        key = keys[tem];
                    } while (key_to_shard(key, nShards) % regionNumber != myRegion);
                    
                    keylist.push_back(std::make_pair(keys[tem], value));
                }

            }else {
                do {
                    tem = rand_key();
                    key = keys[tem];
                } while (key_to_shard(key, nShards) % regionNumber == myRegion);
                keylist.push_back(std::make_pair(keys[tem], value));

                do {
                    tem = rand_key();
                    key = keys[tem];
                } while (key_to_shard(key, nShards) % regionNumber != myRegion);
                keylist.push_back(std::make_pair(keys[tem], value));
                
                for (int i = 0; i < tLen - 2; i++) {
                    tem = rand_key();
                    key = keys[tem];    
                    keylist.push_back(std::make_pair(keys[tem], value));
                }
            }

            if(rand() % 100 < wPer) { 
                for (int i = 0; i < tLen; i++) {
                    if(rand() % 100 < 50) {
                        Debug("%d: retwisclient put: %s in region %lu", i, keylist[i].first.c_str(), key_to_shard(keylist[i].first, nShards));
                        keylist[i].second = keylist[i].first;
                    } else {
                        Debug("%d: retwisclient get: %s in region %lu", i, keylist[i].first.c_str(), key_to_shard(keylist[i].first, nShards));
                    }
                }
            } else {
                for (int i = 0; i < tLen; i++) {
                    Debug("%d: retwisclient get: %s in region %lu", i, keylist[i].first.c_str(), key_to_shard(keylist[i].first, nShards));
                }
            }
            client->Begin(keylist, inorcross);

            gettimeofday(&t1, NULL);

            status = client->PreCommit(keylist);
        }

        struct timeval t7, t8;
        gettimeofday(&t7, NULL);
        status = client->Commit();
        gettimeofday(&t8, NULL);
        long tem = (t8.tv_sec - t7.tv_sec) * 1000000 + (t8.tv_usec - t7.tv_usec);
        Debug("commit time: %ld.%06ld, commit latency: %ld", t8.tv_sec, t8.tv_usec, tem);


        gettimeofday(&t2, NULL);
        
        long latency = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);


        int statistics;

        if (inorcross) {
            statistics = 0;
        } else {
            statistics = 2;
        }

        if (status) {
            statistics += 1;
        }

        fprintf(stderr, "%d %ld.%06ld %ld.%06ld %ld %d %d %d \n", ++nTransactions, t1.tv_sec,
            t1.tv_usec, t2.tv_sec, t2.tv_usec, latency, statistics, ttype, 0);

        if (((t2.tv_sec-t0.tv_sec)*1000000 + (t2.tv_usec-t0.tv_usec)) > duration*1000000) {
            break;
        }

    }
    fprintf(stderr, "# Client exiting..\n");
    return 0;
}

int rand_key()
{
    if (alpha < 0) {
        // Uniform selection of keys.
        return (rand() % nKeys);
    } else {
        // Zipf-like selection of keys.
        if (!ready) {
            zipf = new double[nKeys];

            double c = 0.0;
            for (int i = 1; i <= nKeys; i++) {
                c = c + (1.0 / pow((double) i, alpha));
            }
            c = 1.0 / c;

            double sum = 0.0;
            for (int i = 1; i <= nKeys; i++) {
                sum += (c / pow((double) i, alpha));
                zipf[i-1] = sum;
            }
            ready = true;
        }

        double random = 0.0;
        while (random == 0.0 || random == 1.0) {
            random = (1.0 + rand())/RAND_MAX;
        }

        // binary search to find key;
        int l = 0, r = nKeys, mid;
        while (l < r) {
            mid = (l + r) / 2;
            if (random > zipf[mid]) {
                l = mid + 1;
            } else if (random < zipf[mid]) {
                r = mid - 1;
            } else {
                break;
            }
        }
        return mid;
    } 
}