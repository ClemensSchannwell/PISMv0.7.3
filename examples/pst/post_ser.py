#!/usr/bin/env python
from pylab import *
import os
import sys
from getopt import getopt, GetoptError
import pycdf

# description of where the transcripts are:
desc = [
('../pst_P3etc.out',34216,43806,1,15,0),  # 15km (coarse) grids
('../pst_P3etc.out',43808,53326,2,15,0),
('../pst_P3etc.out',53328,67690,3,15,0),
('../pst_P3etc.out',67692,77660,4,15,0),
('../pst.out',186263,207682,1,10,0),  # 10km grids
('../pst.out',207684,229045,2,10,0),
('../pst_P3etc.out',2,34214,3,10,0),
('../pst.out',229047,251562,4,10,0),
('../pst_P3etc.out',260878,282524,1,10,1),  # 10km grids w vert refine
('../pst_P3etc.out',282526,304142,2,10,1),
('../pst_P3etc.out',304144,336238,3,10,1),
('../pst_P3etc.out',336240,358826,4,10,1),
('../pst_P3etc.out',77662,115135,1,7.5,0),  # 7.5km (fine) grids
('../pst_P3etc.out',115137,154098,2,7.5,0),
('../pst_P3etc.out',154100,220091,3,7.5,0),
('../pst_P3etc.out',220093,260876,4,7.5,0),
('../mid_6july/pstfinish.pbs.o510189',4,87292,1,5,0),  # 5km (finest) grids
('../mid_6july/pstfinish.pbs.o510189',87296,174820,2,5,0),
('../mid_6july/pstfinish.pbs.o510189',174823,315350,3,5,0),
#('../mid_6july/pstfinish.pbs.o510189',,,1,5,0),  # P4 NOT COMPLETED;FIXME
('../mid_7july/pstP1cont.pbs.o510058',4,66949,1,10,2,20),  # P1cont
('../mid_7july/pstP1cont.pbs.o510058',66953,159004,1,10,2,40),
('../mid_7july/pstP1cont.pbs.o510058',159008,252293,1,10,2,60),
('../mid_7july/pstP1cont2.pbs.o511218',4,94013,1,10,2,80),
('../mid_7july/pstP1cont2.pbs.o511218',94017,188112,1,10,2,100),
#('../pst_P1cont.out',2,67385,1,10,2,20),  # P1cont *with -tempskip 10*
#('../pst_P1cont.out',67387,161674,1,10,2,40),
#('../pst_P1cont.out',161676,258755,1,10,2,60),
#('../pst_P1cont.out',258757,357322,1,10,2,80),
#('../pst_P1cont.out',357324,457109,1,10,2,100),
]


colors = ["red", "green", "yellow", "blue", "black", "cyan", "magenta"]

def getcolor(gridspacing):
  if (gridspacing == 5):
    return colors[0]
  elif (gridspacing == 7.5):
    return colors[1]
  elif (gridspacing == 10):
    return colors[2]
  elif (gridspacing == 15):
    return colors[3]
  else:
    print "INVALID HORIZONTAL GRID SPACING; ENDING"
    sys.exit(2)

P1widths = [70, 30, 100, 50]
P1styles = ['-',':','-.','--']
P2angles = [0,10,45]
P2styles = ['-',':','--']

P1contlines = []

numfigs=9

doExtract = False
try:
  opts, args = getopt(sys.argv[1:], "e:",["extract="])
  for opt, arg in opts:
    if opt in ("-e", "--extract"):
      doExtract = True
except GetoptError:
  print "Incorrect command line arguments. Exiting..."
  sys.exit(-1)


print "creating empty figures 1,..,%d" % numfigs
for j in range(1,numfigs+1):
  figure(j)

for k in desc:
  pre = "P%d_%dkm" % (k[3],k[4])
  if (k[5] == 1):
    pre += "VR"
  elif (k[5] == 2):
    pre += "_%dk" % k[6]
  print ""
  print "POST-PROCESSING STDOUT FROM EXPERIMENT %s:" % pre

  if (doExtract):
    extract = "cat %s | sed -n '%d,%dp' > %s.out" % (k[0],k[1],k[2],pre)
    print "extracting standard out: '%s'" % extract
    try:
      status = os.system(extract)
    except KeyboardInterrupt:  sys.exit(2)
    if status:  sys.exit(status)

    outname = "%s.ser.nc" % pre
    print "using series.py to create NetCDF time series"
    print " ------------------ series.py OUTPUT:"
    try:
      status = os.system("series.py -f %s.out -o %s" % (pre,outname))
    except KeyboardInterrupt:  sys.exit(2)
    if status:  sys.exit(status)
    print " ------------------"

  filename = "%s.ser.nc" % pre
  print "opening %s to make time series figures" % filename
  nc = pycdf.CDF(filename)
  time = nc.var("t").get()

  if ( (k[5] == 0) | (k[5] == 1) ):
    print "adding ivol to figure(%d)" % (k[3])
    figure(k[3])
    hold(True)
    var = nc.var("ivol").get()
    myls = "-"
    mylabel = "%.1f km grid" % k[4]
    if (k[5] == 1):
      myls = "--"
      mylabel += " (fine vert)"
    plot(time, var, linestyle=myls, linewidth=1.5, color=getcolor(k[4]), label=mylabel)
    hold(False)

  if ( (k[3] == 2) & (k[4] == 5) ):  # want only finest
    print "adding avdwn0,avdwn1,avdwn2 to figure(5)"
    figure(5)
    hold(True)
    for j in [0,1,2]:
      varname = "avdwn%d" % j
      var = nc.var(varname).get()
      mylabel = r"$%d^\circ$ strip" % P2angles[j]
      semilogy(time, var, linestyle=P2styles[j], linewidth=1.5, 
               color='black', label=mylabel)

  if ( (k[5] == 2) | ((k[3] == 1) & (k[4] == 10) & (k[5] == 0)) ):
    print "adding ivol from P1cont to figure(6)"
    figure(6)
    hold(True)
    var = nc.var("ivol").get()
    plot(time, var, linewidth=1.5, color='black')
    hold(False)
    print "adding avdwn013 (70,30,50km wide) from P1cont to figure(7)"
    figure(7)
    hold(True)
    for j in [0,1,3]:
      varname = "avdwn%d" % j
      var = nc.var(varname).get()
      myl = semilogy(time, var, linestyle=P1styles[j], linewidth=1.5, 
                     color='black')
      P1contlines.append(myl)
    print "adding avdwn2 (100km wide) from P1cont to figure(8)"
    figure(8)
    hold(True)
    varname = "avdwn2"
    var = nc.var(varname).get()
    myl = semilogy(time, var, linewidth=1.5, color='black')
    P1contlines.append(myl)

  if ((k[4] == 10) & (k[5] == 0)):
    print "adding ivol to figure(9)"
    figure(9)
    hold(True)
    var = nc.var("ivol").get()
    mylabel = "P%d" % k[3]
    plot(time, var, linewidth=1.5, color="black",
         linestyle=P1styles[k[3]-1], label=mylabel)
    hold(False)
    
  nc.close()
   
  
print ""
print "LABELING AND SAVING FIGURES (P*.png)"

def savepng(fignum,prefix):
  filename = prefix + ".png"
  print "saving figure(%d) as %s ..." % (fignum,filename)
  savefig(filename, dpi=300, facecolor='w', edgecolor='w')
    
# ivol figures
for j in [1,2,3,4]:
  figure(j)
  #title("results for experiment P%d" % (j))
  xlabel("t  (a)",fontsize=16)
  ylabel(r'volume  ($10^6$ $\mathrm{km}^3$)',fontsize=16)
  axis([0, 5000, 2.10, 2.22]) 
  xticks(arange(0,6000,1000),fontsize=14)
  yticks(arange(2.10,2.22,0.02),fontsize=14)
  if j in [1,2]:
    legend(loc='lower left')
  else:
    legend(loc='upper right')
  savepng(j,"P%d_vol" % j)
figure(9)
xlabel("t  (a)",fontsize=16)
ylabel(r'volume  ($10^6$ $\mathrm{km}^3$)',fontsize=16)
axis([0, 5000, 2.10, 2.22]) 
xticks(arange(0,6000,1000),fontsize=14)
yticks(arange(2.10,2.22,0.02),fontsize=14)
legend(loc='upper right')
savepng(9,"Pall_vol")

# P2 speed figure
figure(5)
xlabel("t  (a)",fontsize=16)
ylabel("average downstream speed  (m/a)",fontsize=16)
axis([0, 5000, 10, 1000]) 
xticks(arange(0,6000,1000),fontsize=14)
yticks([10,50,100,500,1000],('10','50','100','500','1000'),fontsize=14)
rcParams.update({'legend.fontsize': 14})
legend()
savepng(5,"P2_dwnspeeds")
hold(False)

# P1cont volume figure
figure(6)
xlabel("t  (a)",fontsize=16)
ylabel(r'volume  ($10^6$ $\mathrm{km}^3$)',fontsize=16)
axis([0, 100000, 2.10, 2.22]) 
xticks(arange(0,120000,20000),fontsize=14)
yticks(arange(2.10,2.22,0.02),fontsize=14)
savepng(6,"P1cont_vol")
hold(False)

# P1cont speed figure for 70,30,50km wide
figure(7)
xlabel("t  (a)",fontsize=16)
ylabel("average downstream speed  (m/a)",fontsize=16)
axis([0, 100000, 75, 200]) 
xticks(arange(0,120000,20000),fontsize=14)
yticks([75,100,150,200],('75','100','150','200'),fontsize=14)
rcParams.update({'legend.fontsize': 14})
legend(P1contlines[0:3],
       ( "%d km wide strip" % P1widths[0],"%d km wide strip" % P1widths[1],
         "%d km wide strip" % P1widths[3] ))
savepng(7,"P1cont_dwnspeeds")
hold(False)

# P1cont speed figure for 100km wide
figure(8)
xlabel("t  (a)",fontsize=16)
ylabel("average downstream speed  (m/a)",fontsize=16)
axis([0, 100000, 75, 200]) 
xticks(arange(0,120000,20000),fontsize=14)
yticks([75,100,150,200],('75','100','150','200'),fontsize=14)
rcParams.update({'legend.fontsize': 14})
legend(("100 km wide strip",))
savepng(8,"P1cont_dwnspeed100km")
hold(False)

  
print ""
print "AUTOCROPPING FIGURES (P*.png)"
# uses one of the ImageMagick tools (http://www.imagemagick.org/)
try:
  status = os.system("mogrify -verbose -trim +repage P*.png")
except KeyboardInterrupt:  sys.exit(2)
if status:  sys.exit(status)


exit()

