
echo $test
for ((i=160; i<170; i=i+1))
do
    echo "Stop the progress in client $i(s)"

    cmd="ps -ef | grep tapir | grep -v grep | awk '{print \$2}' | xargs kill -9"
    echo $cmd
    ssh 10.62.0.$i "ps -ef | grep tapir | grep -v grep | awk '{print \$2}' | xargs kill -9"
    ssh 10.62.0.$i "ps -ef | grep origin | grep -v grep | awk '{print \$2}' | xargs kill -9"
    ssh 10.62.0.$i "ps -ef | grep run | grep -v grep | awk '{print \$2}' | xargs kill -9"
done