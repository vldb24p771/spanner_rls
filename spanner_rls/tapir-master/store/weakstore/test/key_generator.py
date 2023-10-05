import random
import string
import sys

charset = string.ascii_uppercase + string.ascii_lowercase + string.digits

filename = "key.txt"

for i in range(int(sys.argv[1])):
  rkey = "".join(random.choice(charset) for j in range(64))
  with open(filename, 'a') as object:
    object.write(rkey + "\n")
  print(rkey)
