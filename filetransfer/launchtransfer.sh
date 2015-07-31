#!/bin/bash

#qsub -I -l nodes=2:ppn=1 -d `pwd`
#aprun -N1 -n2 hostname - to retrieve hostnames
#first arg to script is the node on which the server run;
#second arg is the node where the client needs to connect to
#i.e. the server node itself
#aprun -N1 -n2 ./launchtransfer.sh nid00008 nid00008 /tmp/zfile
port=5555
h=`hostname`
if [ $h = $1 ]; then
  ./filerecv $port /tmp/received
else
  chunksize=$4
  filename=$3
  servernode=$2
  ./filesend $servernode $port $filename $chunksize
fi
