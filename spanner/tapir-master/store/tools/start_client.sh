#!/bin/bash

if [ "$#" -ne 5 ]; then
  echo "Usage: $0 command begin_id ncopies" >&2
  exit 1
fi

cmd=$1
begin=$2
copies=$3
logdir=$4
number=$5

let end=$begin+$copies

for ((i=$begin; i<$end; i++))
do
  command="$cmd > $logdir/client$number.$i.log 2>&1 &"
  #echo $command
  eval $command
done
