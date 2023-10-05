import yaml
import subprocess

NETWORK_NAME = "rls_network"
CONTAINER_NAME_PREFIX = "spanner_rls_"
HOSTS_FILE = '/home/ruijie/share/tapir/tapir-master/scripts/hosts.yml'
MAX_REGIONS = 10
NETWORK_BASE = "10.62.0"
IMAGE_NAME = "haozesong/spanner_rls"
CONTAINERS_FILE = "containers.yml"

hosts = [] 
with open (HOSTS_FILE, 'r') as f:
    data = yaml.load(f,Loader=yaml.FullLoader)
    hosts = data['hosts']

for h in hosts:
    print("stopping container on " + h)
    ssh_command = "docker stop $(docker ps -q)"
    print(ssh_command)
    subprocess.call(['ssh', h, ssh_command])

for h in hosts:
    print("stopping container on " + h)
    ssh_command = "docker rm $(docker ps -aq)"
    print(ssh_command)
    subprocess.call(['ssh', h, ssh_command])
