#!/bin/bash

trap '{
  echo "\nKilling all clients.. Please wait..";
  for host in ${clients[@]}
  do
    ssh $host "killall -9 $client";
    ssh $host "killall -9 $client";
  done

  echo "\nKilling all replics.. Please wait..";
  for host in ${servers[@]}
  do
    ssh $host "killall -9 server";
  done
}' INT

# Paths to source code and logfiles.
srcdir="/origin/tapir-master"
logdir="/origin/tapir-master/log"

client="retwisClient"    # Which client (benchClient, retwisClient, etc)
store="strongstore"      # Which store (strongstore, weakstore, tapirstore)
mode="span-lock"            # Mode for storage system.

nshard=30    # number of shards
nclient=40    # number of clients to run (per machine)
nregion=10
nkeys=3000000 # number of keys to use
rtime=40     # duration to run

tlen=10      # transaction length
wper=100     # writes percentage
inper=90
err=0        # error
skew=0       # skew
zalpha=0.75    # zipf alpha (-1 to disable zipf and enable uniform)
tem=170
#5ms 100ms
# Print out configuration being used.
echo "Configuration:"
echo "Shards: $nshard"
echo "Clients per host: $nclient"
echo "Threads per client: $nthread"
echo "Keys: $nkeys"
echo "Transaction Length: $tlen"
echo "Write Percentage: $wper"
echo "Error: $err"
echo "Skew: $skew"
echo "Zipf alpha: $zalpha"
echo "Skew: $skew"
echo "Client: $client"
echo "Store: $store"
echo "Mode: $mode"


# Generate keys to be used in the experiment.
#echo "Generating random keys.."
#python3 $srcdir/store/tools/key_generator.py $nkeys > $srcdir/store/tools/keys
#for ((i=0; i<10; i++))
#do
#  python3 $srcdir/store/tools/shard_generator.py $i > $srcdir/store/tools/shard$i.config
#done

#for inper in 0.99 0.98 0.97 0.95 0.92 0.9 0.85 0.8 0.75 0.7
#for zalpha in 0.5 0.55 0.6 0.65 0.7 0.75 0.8 0.85 0.9 0.95 1
#for nclient in 1 2 3 5 8 10 13 15 18 20 25 30 35 40 45 50 55 60 65
for j in {1..5}
do
for inper in 90
do
echo "$j $inper"
# Start all replicas and timestamp servers
echo "Starting TimeStampServer replicas.."
$srcdir/store/tools/start_replica.sh tss $srcdir/store/tools/shard/shard.tss.config \
  "$srcdir/timeserver/timeserver" $logdir

#for ((i=0; i<$nshard; i++)) need to --
for ((i=0; i<$nshard; i++))
do
{
  echo "Starting shard$i replicas.."
  $srcdir/store/tools/start_replica.sh shard$i $srcdir/store/tools/shard/shard$i.config \
    "$srcdir/store/$store/server -m $mode -f $srcdir/store/tools/keys -k $nkeys -e $err -s $skew" $logdir

}&
done
wait
sleep 2


# Run the clients
count=0
region=0
for ((i=160; i<$tem; i=i+1,region=region+1))
do
{
  echo "Running the client$i(s)"
  echo "$region"
  #ssh $host "$srcdir/store/tools/start_client.sh \"$srcdir/store/benchmark/$client \
  ssh -o StrictHostKeyChecking=no 10.62.0.$i "$srcdir/store/tools/start_client.sh \"$srcdir/store/benchmark/$client \
  -c $srcdir/store/tools/shard/shard -N $nshard -f $srcdir/store/tools/keys \
  -d $rtime -l $tlen -i $inper -w $wper -k $nkeys -m $mode -e $err -s $skew -z $zalpha -n $nregion -M $region\" \
  $count $nclient $logdir $i"
  region=`expr $region + 1`
}&
done
wait
for ((i=160; i<$tem; i=i+1))
do
{
echo "Waiting for client(s)$i to exit"

  #ssh $host "$srcdir/store/tools/start_client.sh \"$srcdir/store/benchmark/$client \
  ssh 10.62.0.$i "$srcdir/store/tools/wait_client.sh $client"

}&

done
wait
# Kill all replicas
echo "Cleaning up"

$srcdir/store/tools/stop_replica.sh $srcdir/store/tools/shard.tss.config > /dev/null 2>&1

for ((i=0; i<$nshard; i++))
do
{
  $srcdir/store/tools/stop_replica.sh $srcdir/store/tools/shard$i.config > /dev/null 2>&1
}
done

# Process logs

for ((i=160; i<$tem; i=i+1))
do
echo "Processing logs"
cat $logdir/client$i.*.log | sort -g -k 3 > $logdir/client$i.log
ssh 10.62.0.$i "rm -f $logdir/client$i.*.log"
done

echo "Processing total logs"
cat $logdir/client*.log | sort -g -k 3 > $logdir/client.log

python3 $srcdir/store/tools/process_logs.py $logdir/client.log $rtime $j
sleep 30
echo "clean"
for ((i=160; i<$tem; i=i+1))
do
ssh 10.62.0.$i "ps -ef | grep tapir | grep -v grep | awk '{print \$2}' | xargs kill -9"
ssh 10.62.0.$i "ps -ef | grep origin | grep -v grep | awk '{print \$2}' | xargs kill -9"
done

done
done
