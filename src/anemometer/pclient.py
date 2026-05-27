import socket
import argparse
import sys
import struct
import logging
import logging.handlers

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',handlers=[logging.StreamHandler()],level=logging.NOTSET)

ap = argparse.ArgumentParser()

ap.add_argument('-d','--dest',help='x.x.x.x format IP address of destination',required=True)
ap.add_argument('-p','--port',help='Destination UDP port',required=False,default='8225')
ap.add_argument('-c','--command',help='Packet command byte',required=False,default=None)
ap.add_argument('-cv','--command_values',help='Command payload values (series of comma separated hex byte values',required=False,default="")
ap.add_argument('-n','--name',help='Command name',required=False,default='')
ap.add_argument('-nv','--value',help='Command value (if required)',required=False,default='')
ap.add_argument('-b','--base',help='Interpret command payload values as this base 16 is default, 10 is most likely other value ',required=False,default=16)
ap.add_argument('-ack','--ackbyte',help='Packet ack byte (for testing)',required=False,default=6)
ap.add_argument('-t','--timeout',help='Timeout in seconds (float format)',required=False,default="5.0")
ap.add_argument('-a','--all',help='Send every command and get responses',required=False,default=False,action='store_true')
ap.add_argument('-e','--echo',help='print everything',required=False,default=False,action='store_true')
ap.add_argument('-te','--test_environment',help='Import relative, used in test environment',required=False,default=False,action='store_true')

clargs = ap.parse_args(sys.argv[1:])

if clargs.test_environment:
	from picoxface import *
else:
	from anemometer.picoxface import *

sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

##########################################
# Script start 
##########################################

if clargs.all: # send it all!
	for c in pico_commands:
		print(c)
		print(parse_received(send_packet(sock,clargs.dest,clargs.port,clargs.timeout,pico_commands[c]['val'],pico_commands[c]['default_payload'],echo=clargs.echo)))

else:
	# given on command line
	command_payload_bytes=bytearray()
	if len(clargs.command_values) > 0:
		for cv in clargs.command_values.split(','):
			command_payload_bytes.append(int(cv,int(clargs.base)))
	
	rec = bytearray()
	
	# if specified with raw numerical values, send the packet
	if clargs.command!=None:
		rec = send_packet(sock,clargs.dest,clargs.port,clargs.timeout,clargs.command, command_payload_bytes,echo=clargs.echo)
	else:
		if len(clargs.name) > 0:
			# look for match in enum
			command_key=''
			for cn in pico_commands.keys():
				if clargs.name.upper() in cn:
					command_key=cn
			if len(command_key) > 0:
				print("Command is {0} ({1})".format(command_key,pico_commands[command_key]['val']))
				print(parse_received(send_packet(sock,clargs.dest,clargs.port,clargs.timeout,pico_commands[command_key]['val'],pack16(clargs.value),echo=clargs.echo)))
			else:
				raise ValueError("Unknown command {0}".format(clargs.name)) 
		else:
			raise ValueError("Specify command via --command [number] or --name [name]")
	
