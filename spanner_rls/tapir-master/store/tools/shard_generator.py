import os

for i in range(300):
    filename = f"shard/shard{i}.config"
    with open(filename, "w+") as f:
        f.write("f 1\n")
        for j in range(3):
            f.write("replica 10.62.0.{}:{}\n".format(160 + i % 10, 5100 + 3 * i + j))

filename = f"shard/shard.tss.config"
with open(filename, "w+") as f:
    f.write("f 1\n")
    for j in range(3):
        f.write("replica 10.62.0.160:{}\n".format(5000 + j))