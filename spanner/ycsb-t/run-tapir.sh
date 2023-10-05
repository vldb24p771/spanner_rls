#! /usr/bin/env bash

# Copy the shared library to libs folder.
mkdir -p libs
cp /mnt/n/code/origin/tapir-master/libtapir/libtapir.so ./libs/

# Make the tapir binding using maven
mvn clean package

srcdir="/mnt/n/code/origin/tapir-master"
logdir="/mnt/n/code/origin/tapir-master"

# Machines on which replicas are running.
replicas=("127.0.0.1" "127.0.0.2" "127.0.0.3")

# Machines on which clients are running.
clients=("127.0.0.1")

client="benchClient"    # Which client (benchClient, retwisClient, etc)
store="strongstore"      # Which store (strongstore, weakstore, tapirstore)
mode="span-lock"            # Mode for storage system.

nshard=1     # number of shards
nclient=1    # number of clients to run (per machine)
nkeys=100000 # number of keys to use
rtime=120     # duration to run

tlen=2       # transaction length
wper=10       # writes percentage
inper=10
err=0        # error
skew=0       # skew
zalpha=1    # zipf alpha (-1 to disable zipf and enable uniform)

echo "Starting TimeStampServer replicas.."
$srcdir/store/tools/start_replica.sh tss $srcdir/store/tools/shard.tss.config \
  "$srcdir/timeserver/timeserver" $logdir
  
echo "Starting shard0 replicas.."
$srcdir/store/tools/start_replica.sh shard0 $srcdir/store/tools/shard0.config \
"$srcdir/store/$store/server -m $mode -f $srcdir/store/tools/keys -k $nkeys -e $err -s $skew" $logdir

# Load the records in Tapir
java -cp tapir-interface/target/tapir-interface-0.1.4.jar:core/target/core-0.1.4.jar:tapir/target/tapir-binding-0.1.4.jar:javacpp/target/javacpp.jar \
-Djava.library.path=libs/ com.yahoo.ycsb.Client -P workloads/workloada \
-load -db com.yahoo.ycsb.db.TapirClient \
-p tapir.configpath=../store/tools/shard -p tapir.nshards=1 -p tapir.clientNumber=1 -p tapir.closestreplica=0 > load.log 2>&1

# Run the YCSB workload
java -cp tapir-interface/target/tapir-interface-0.1.4.jar:core/target/core-0.1.4.jar:tapir/target/tapir-binding-0.1.4.jar:javacpp/target/javacpp.jar \
-Djava.library.path=libs/ com.yahoo.ycsb.Client -P workloads/workloada \
-t -db com.yahoo.ycsb.db.TapirClient \
-p tapir.configpath=../store/tools/shard -p tapir.nshards=1 -p tapir.clientNumber=1 -p tapir.closestreplica=0 > run.log 2>&1
