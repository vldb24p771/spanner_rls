// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/benchmark/benchClient.cc:
 *   Benchmarking client for a distributed transactional store.
 *
 **********************************************************************/

#include "store/common/truetime.h"
#include "store/common/frontend/client.h"
#include "store/strongstore/client.h"
#include "store/weakstore/client.h"
#include "store/tapirstore/client.h"
#include <cstdio>

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
    int nShards = 1;
    int tLen = 10;
    int wPer = 50; // Out of 100
    int closestReplica = -1; // Closest replica id.
    int skew = 0; // difference between real clock and TrueTime
    int error = 0; // error bars
    int InPer = 50;
    int clientNumber = 0;

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
    while ((opt = getopt(argc, argv, "c:d:N:l:i:w:k:f:m:e:s:z:r:n:")) != -1) {
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
                fprintf(stderr, "option -n requires a numeric arg\n");
            }
            break;
        }


        case 'n': // Number of shards.
        { 
            char *strtolPtr;
            clientNumber = strtoul(optarg, &strtolPtr, 10);
            if ((*optarg == '\0') || (*strtolPtr != '\0') ||
                (clientNumber < 0)) {
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
                fprintf(stderr, "option -n requires a numeric arg\n");
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
                    nShards, clientNumber, closestReplica, TrueTime(skew, error));
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


    struct timeval t0, t1, t2, t3, t4, t5, t6;

    int nTransactions = 0;
    int tCount = 0;
    double tLatency = 0.0;
    int getCount = 0;
    double getLatency = 0.0;
    int putCount = 0;
    double putLatency = 0.0;
    int beginCount = 0;
    double beginLatency = 0.0;
    int commitCount = 0;
    double commitLatency = 0.0;
    double testLatency = 0.0;
    int tem;

    gettimeofday(&t0, NULL);
    srand(t0.tv_sec + t0.tv_usec);

    while (1) {
        gettimeofday(&t4, NULL);
        //client->Begin();
        gettimeofday(&t1, NULL);
        Debug("START PREPARE");
        beginCount++;
        beginLatency += ((t1.tv_sec - t4.tv_sec)*1000000 + (t1.tv_usec - t4.tv_usec));
        
        int inorcross = rand() % 100;
        for (int j = 0; j < tLen; j++) {
            gettimeofday(&t5, NULL);

            if (inorcross < InPer) {
                do {
                    key = keys[rand_key()];
                } while (key_to_shard(key, nShards) != clientNumber);
            } else {
                do {
                    key = keys[rand_key()];
                } while (key_to_shard(key, nShards) == clientNumber);
            }
            tem = rand() % 100;
            gettimeofday(&t6, NULL);
            testLatency += ((t6.tv_sec - t5.tv_sec)*1000000 + (t6.tv_usec - t5.tv_usec));
            
            if (tem % 100 < wPer) {
                gettimeofday(&t3, NULL);
                Debug("%d: retwisclient put: %s", j, key.c_str());
                client->Put(key, key);
                gettimeofday(&t4, NULL);
                
                putCount++;
                putLatency += ((t4.tv_sec - t3.tv_sec)*1000000 + (t4.tv_usec - t3.tv_usec));
            } else {
                gettimeofday(&t3, NULL);
                Debug("%d: retwisclient get: %s", j, key.c_str());
                int ret = client->Get(key, value);
                Debug("ret: %d", ret);
                gettimeofday(&t4, NULL);

                getCount++;
                getLatency += ((t4.tv_sec - t3.tv_sec)*1000000 + (t4.tv_usec - t3.tv_usec));
            }
        }

        gettimeofday(&t3, NULL);
        bool status = client->Commit();
        gettimeofday(&t2, NULL);
        commitCount++;
        commitLatency += ((t2.tv_sec - t3.tv_sec)*1000000 + (t2.tv_usec - t3.tv_usec));
        Debug("commit time: %ld.%06ld, commit latency: %ld", t3.tv_sec, t3.tv_usec, (t2.tv_sec - t3.tv_sec)*1000000 + (t2.tv_usec - t3.tv_usec));

        long latency = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec);

        fprintf(stderr, "%d %ld.%06ld %ld.%06ld %ld %d\n", nTransactions+1, t1.tv_sec,
                t1.tv_usec, t2.tv_sec, t2.tv_usec, latency, status?1:0);

        if (status) {
            tCount++;
            tLatency += latency;
        }
        nTransactions++;

        gettimeofday(&t1, NULL);
        if ( ((t1.tv_sec-t0.tv_sec)*1000000 + (t1.tv_usec-t0.tv_usec)) > duration*1000000) 
            break;
    }

    fprintf(stderr, "# Commit_Ratio: %lf\n", (double)tCount/nTransactions);
    fprintf(stderr, "# Overall_Latency: %lf\n", tLatency/tCount);
    fprintf(stderr, "# Begin: %d, %lf\n", beginCount, beginLatency/beginCount);
    fprintf(stderr, "# Get: %d, %lf\n", getCount, getLatency/getCount);
    fprintf(stderr, "# Put: %d, %lf\n", putCount, putLatency/putCount);
    fprintf(stderr, "# Commit: %d, %lf\n", commitCount, commitLatency/commitCount);
    
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
