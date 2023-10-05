import subprocess
import yaml
CONTAINERS_FILE = "../../scripts/containers.yml"

containers = []
with open(CONTAINERS_FILE, 'r') as f:
    data = yaml.load(f)
    containers = data['containers']
    print(containers)

with open("/root/.ssh/known_hosts", 'a+') as f:
    for con in containers:
        cmd = ['ssh-keyscan', "-H", con]
        subprocess.call(cmd, stdout=f)
