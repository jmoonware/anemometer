# Plots the Davis 6410 wind calibration file
# and possible creats a .h include file for use in c programs 
# Found in https://github.com/kobuki/weewx-meteoRX/tree/master

import matplotlib.pyplot as plt
import numpy as np
import argparse 
import sys

ap = argparse.ArgumentParser()
ap.add_argument('-v','--velocity',help='Integer velocity in mph',default=None)
ap.add_argument('-hm','--heatmap',help='Plot heatmap',action='store_true',default=False)
ap.add_argument('-idx','--index',help='Plot angle ave/med vs array index',action='store_true',default=False)
ap.add_argument('-i','--includefile',help='Create a .h include file of data',default='')


clargs = ap.parse_args(sys.argv[1:])

with open('windcal.dat','rb') as f:
	cal = bytearray(f.read())

# reshape into numpy array for plotting
wcal = np.array(cal).reshape(int(len(cal)/256),256)

if clargs.heatmap:
	plt.imshow(wcal)
	plt.show()

if clargs.index:
	# plot the average velocity vs. array index 
	avg = [np.mean(x) for x in wcal]
	med = [np.median(x) for x in wcal]
	plt.plot(np.arange(len(wcal)),avg-np.arange(len(wcal)),label='avg')
	plt.plot(np.arange(len(wcal)),med-np.arange(len(wcal)),label='median')
	plt.legend()
	plt.xlabel("Array index")
	plt.ylabel("v_mean - index")
	plt.title("Nominally expect a difference of 1")
	plt.show()
	
# individual velocity traces
if clargs.velocity!=None:
	v_row = int(clargs.velocity)-1
	if (v_row >=0 and v_row < len(wcal)):
		plt.plot(np.arange(len(wcal[v_row,:]))*360/len(wcal[v_row,:]), wcal[v_row,:])
		plt.xlabel("Angle (deg)")
		plt.ylabel("V (LUT)")
		plt.title("V = {0}, row = {1}".format(clargs.velocity,v_row))
		plt.show()
	else:
		print("V out of range (0-{0})".format(len(wcal)))
	

# now possibly create a .h include file with the data 

if len(clargs.includefile) > 0:
	with open(clargs.includefile,'w') as f:
		# store as single char array
		f.write("unsigned char windcal[] = {\n")
		for angcal in wcal[:-1,:]:
			toks=['{0}'.format(x) for x in angcal]
			f.write(','.join(toks)+',\n')
		# last line is special
		toks = ['{0}'.format(x) for x in wcal[-1,:]]
		f.write(','.join(toks)) # no trailing comma
		f.write("\n};\n")
