import sys

start, end = -1.0, -1.0

duration = float(sys.argv[2])
warmup = duration/3.0

tLatency = []
tcLatency = []
tiLatency = []
sLatency = []
scLatency = []
siLatency = []
fLatency = []
fcLatency = []
fiLatency = []

tExtra = 0.0
tcExtra = 0.0
tiExtra = 0.0
sExtra = 0.0
scExtra = 0.0
siExtra = 0.0
fExtra = 0.0
fcExtra = 0.0
fiExtra = 0.0

xLatency = []

for line in open(sys.argv[1]):
  if line.startswith('#') or line.strip() == "":
    continue

  line = line.strip().split()
  if not line[0].isdigit() or len(line) < 4:
    continue

  if start == -1:
    start = float(line[2]) + warmup
    end = start + warmup

  fts = float(line[2])
  
  if fts < start:
    continue

  if fts > end:
    continue

  latency = int(line[3])
  status = int(line[4])
  ttype = -1
  try:
    ttype = int(line[5])
    extra = int(line[6])
  except:
    extra = 0

  if status == 1 and ttype == 2:
    xLatency.append(latency)

  tLatency.append(latency) 
  tExtra += extra

  if status == 1:
    tiLatency.append(latency) 
    tiExtra += extra
    sLatency.append(latency)
    sExtra += extra
    siLatency.append(latency)
    siExtra += extra
  elif status == 2:
    tcLatency.append(latency) 
    tcExtra += extra
    fLatency.append(latency)
    fExtra += extra
    fcLatency.append(latency)
    fcExtra += extra
  elif status == 3:
    tcLatency.append(latency) 
    tcExtra += extra
    sLatency.append(latency)
    sExtra += extra
    scLatency.append(latency)
    scExtra += extra
  else:
    tiLatency.append(latency) 
    tiExtra += extra
    fLatency.append(latency)
    fExtra += extra
    fiLatency.append(latency)
    fiExtra += extra

if len(tLatency) == 0:
  print("Zero completed transactions..")
  sys.exit()

tLatency.sort()
tiLatency.sort()
tcLatency.sort()
sLatency.sort()
siLatency.sort()
scLatency.sort()
fLatency.sort()

print ("Transactions(All/Success):%d/%d " %(len(tLatency), len(sLatency)))
print ("Abort Rate:%f" %((len(tLatency)-len(sLatency))/len(tLatency)))
print ("Throughput (All/Success)(ms):%f/%f" %(len(tLatency)/(end-start), len(sLatency)/(end-start)))
#print ("Average Latency (all): %f" %(sum(tLatency)/float(len(tLatency))))
print ("Median Latency (All/sucess)(ms):%f/%f " %(tLatency[len(tLatency)//2]/1000, sLatency[len(sLatency)//2]/1000))
print ("90%%tile Latency (All/sucess)(ms):%f/%f " %(tLatency[(len(tLatency) * 90)//100]/1000, sLatency[(len(sLatency) * 90)//100]/1000))
print ("99%%tile Latency (All/sucess)(ms):%f/%f " %(tLatency[(len(tLatency) * 99)//100]/1000, sLatency[(len(sLatency) * 99)//100]/1000))
print ("99.9%%tile Latency (All/sucess)(ms):%f/%f " %(tLatency[(len(tLatency) * 999)//1000]/1000, sLatency[(len(sLatency) * 999)//1000]/1000))
#print ("Average Latency (success): %f" %(sum(sLatency)/float(len(sLatency))))
#print ("Median Latency (success): ", sLatency[len(sLatency)//2])
#print ("90%tile Latency (success): ", sLatency[(len(sLatency) * 90)//100])
#print ("99%tile Latency (success): ", sLatency[(len(sLatency) * 99)//100])
#print ("99.9%tile Latency (success): ", sLatency[(len(sLatency) * 999)//1000])

print()
print ("cross")
print ("Transactions(All/Success):%d/%d " %(len(tcLatency), len(scLatency)))
print ("Abort Rate:%f" %(len(fcLatency)/len(tcLatency)))
print ("Throughput (All/Success)(ms):%f/%f" %(len(tcLatency)/(end-start), len(scLatency)/(end-start)))
#print ("cross Average Latency (success): %f" %(sum(scLatency)/float(len(scLatency))))
print ("Median Latency (All/sucess)(ms):%f/%f " %(tcLatency[len(tcLatency)//2]/1000, scLatency[len(scLatency)//2]/1000))
print ("90%%tile Latency (All/sucess)(ms):%f/%f " %(tcLatency[(len(tcLatency) * 90)//100]/1000, scLatency[(len(scLatency) * 90)//100]/1000))
print ("99%%tile Latency (All/sucess)(ms):%f/%f " %(tcLatency[(len(tcLatency) * 99)//100]/1000, scLatency[(len(scLatency) * 99)//100]/1000))
print ("99.9%%tile Latency (All/sucess)(ms):%f/%f " %(tcLatency[(len(tcLatency) * 999)//1000]/1000, scLatency[(len(scLatency) * 999)//1000]/1000))

print()
print ("in")
print ("Transactions(All/Success):%d/%d " %(len(tiLatency), len(siLatency)))
print ("Abort Rate:%f" %(len(fiLatency)/len(tiLatency)))
print ("Throughput (All/Success)(ms):%f/%f" %(len(tiLatency)/(end-start), len(siLatency)/(end-start)))
#print ("in Average Latency (success): %f" %(sum(siLatency)/float(len(siLatency))))
print ("Median Latency (All/sucess)(ms):%f/%f " %(tiLatency[len(tiLatency)//2]/1000, siLatency[len(siLatency)//2]/1000))
print ("90%%tile Latency (All/sucess)(ms):%f/%f " %(tiLatency[(len(tiLatency) * 90)//100]/1000, siLatency[(len(siLatency) * 90)//100]/1000))
print ("99%%tile Latency (All/sucess)(ms):%f/%f " %(tiLatency[(len(tiLatency) * 99)//100]/1000, siLatency[(len(siLatency) * 99)//100]/1000))
print ("99.9%%tile Latency (All/sucess)(ms):%f/%f " %(tiLatency[(len(tiLatency) * 999)//1000]/1000, siLatency[(len(siLatency) * 999)//1000]/1000))

#print ("Extra (all): ", tExtra)
#print ("Extra (success): ", sExtra)
#if len(xLatency) > 0:
#  print ("X Transaction Latency: ", sum(xLatency)/float(len(xLatency)))
#if len(fLatency) > 0:
#  print ("Average Latency (failure): ", sum(fLatency)/float(len(tLatency)-len(sLatency)))
#  print ("Extra (failure): ", fExtra)
