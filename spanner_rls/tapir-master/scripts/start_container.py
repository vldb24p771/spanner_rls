import subprocess
import yaml


NETWORK_NAME = "rls_network"
CONTAINER_NAME_PREFIX = "spanner_rls_"
HOSTS_FILE = '/home/ruijie/share/tapir/tapir-master/scripts/hosts.yml'
MAX_REGIONS = 10
NETWORK_BASE = "10.62.0"
IMAGE_NAME = "haozesong/spanner_rls"
CONTAINERS_FILE = "./containers.yml"

hosts = [] 
with open (HOSTS_FILE, 'r') as f:
    data = yaml.load(f,Loader=yaml.FullLoader)
    hosts = data['hosts']

containers = []

for i in range(MAX_REGIONS):
    # each region takes a subnet
    host = hosts[i % len(hosts)]
    IP = NETWORK_BASE + "." + str(i*1+160)
    containers.append(IP)
    print(host, IP)
    container_name = CONTAINER_NAME_PREFIX + str(i)
    all_subnet = NETWORK_BASE + ".0"
    ssh_command = "docker run -t -d -v /home/ruijie/share/tapir/tapir-master:/tapir-master -v /home/ruijie/share/origin:/origin --cap-add NET_ADMIN --network " + NETWORK_NAME +  " --name " + container_name + " --ip " + IP + " " + IMAGE_NAME + " /tapir-master/scripts/tc_ssh.sh " + IP 
    print(ssh_command)
    subprocess.call(['ssh', host, ssh_command])

with open(CONTAINERS_FILE, 'w+') as f:
    data = {}
    data['containers'] = containers
    yaml.dump(data, f)
'''
docker run -d -v /home/ruijie/share/tapir/tapir-master:/tapir-master -v /home/ruijie/share/origin:/origin --cap-add NET_ADMIN --network rls_network --name ingress --ip 10.62.0.150 haozesong/spanner_rls /tapir-master/scripts/tc_ssh.sh 10.62.0.150 
docker run -t -d -v /home/ruijie/share/crdb:/root/crdb -v /home/ruijie/share/cockroach:/root/crdb_origin --cap-add NET_ADMIN --network rls_network --name crdb2 --ip 10.62.0.150 rjgong/crdb:v2



'''