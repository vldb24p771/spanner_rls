import random
import string
import sys

charset = string.ascii_uppercase + string.ascii_lowercase + string.digits

for i in range(int(sys.argv[1])):
  rkey = "".join(random.choice(charset) for j in range(64))
  #rkey = random.randint(1,10)
  print(rkey)
#print('f 1')
#for i in range(3):
#  if int(sys.argv[1]) < 5:
#    s1 = 51000 + int(sys.argv[1]) * 3 + i
#    print('replica 10.22.1.5:{}'.format(s1))
#  else:
#    s1 = 51000 + int(sys.argv[1]) * 3 + i
#    print('replica 10.22.1.6:{}'.format(s1))
